// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp32/rom/gpio.h"
#include "soc/gpio_pins.h"
#include "esp32/rom/spi_flash.h"
#include "bootloader_config.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_common.h"
#include "sdkconfig.h"
#include "esp_image_format.h"
#include "soc/gpio_struct.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_sig_map.h"

static const char* TAG = "boot";

static int select_partition_number (bootloader_state_t *bs);
static int selected_boot_partition(const bootloader_state_t *bs);
/*
 * We arrive here after the ROM bootloader finished loading this second stage bootloader from flash.
 * The hardware is mostly uninitialized, flash cache is down and the app CPU is in reset.
 * We do have a stack, so we can do the initialization in C.
 */
void __attribute__((noreturn)) call_start_cpu0()
{
	//	Set GPIO 0 to low level ASAP.
    const uint32_t GPIO_PIN_MUX_REG[GPIO_PIN_COUNT] = {
        IO_MUX_GPIO0_REG,
        IO_MUX_GPIO1_REG,
        IO_MUX_GPIO2_REG,
        IO_MUX_GPIO3_REG,
        IO_MUX_GPIO4_REG,
        IO_MUX_GPIO5_REG,
        IO_MUX_GPIO6_REG,
        IO_MUX_GPIO7_REG,
        IO_MUX_GPIO8_REG,
        IO_MUX_GPIO9_REG,
        IO_MUX_GPIO10_REG,
        IO_MUX_GPIO11_REG,
        IO_MUX_GPIO12_REG,
        IO_MUX_GPIO13_REG,
        IO_MUX_GPIO14_REG,
        IO_MUX_GPIO15_REG,
        IO_MUX_GPIO16_REG,
        IO_MUX_GPIO17_REG,
        IO_MUX_GPIO18_REG,
        IO_MUX_GPIO19_REG,
        0,
        IO_MUX_GPIO21_REG,
        IO_MUX_GPIO22_REG,
        IO_MUX_GPIO23_REG,
        0,
        IO_MUX_GPIO25_REG,
        IO_MUX_GPIO26_REG,
        IO_MUX_GPIO27_REG,
        0,
        0,
        0,
        0,
        IO_MUX_GPIO32_REG,
        IO_MUX_GPIO33_REG,
        IO_MUX_GPIO34_REG,
        IO_MUX_GPIO35_REG,
        IO_MUX_GPIO36_REG,
        IO_MUX_GPIO37_REG,
        IO_MUX_GPIO38_REG,
        IO_MUX_GPIO39_REG,
    };
	const int gpio_num = 0;
	uint32_t io_reg = GPIO_PIN_MUX_REG[gpio_num];
    GPIO.enable_w1ts = (0x1 << gpio_num);						//	enable output.
    gpio_matrix_out(gpio_num, SIG_GPIO_OUT_IDX, false, false);	//	also enable output.
	PIN_FUNC_SELECT(io_reg, PIN_FUNC_GPIO);						//	select pin func
	GPIO.out_w1tc = (1 << gpio_num);							//	set level = 0
	
	//	normal boot loarder
	
	
    // 1. Hardware initialization
    if (bootloader_init() != ESP_OK) {
        bootloader_reset();
    }

    // 2. Select the number of boot partition
    bootloader_state_t bs = { 0 };
    int boot_index = select_partition_number(&bs);
    if (boot_index == INVALID_INDEX) {
        bootloader_reset();
    }

    // 3. Load the app image for booting
    bootloader_utility_load_boot_image(&bs, boot_index);
}

// Select the number of boot partition
static int select_partition_number (bootloader_state_t *bs)
{
    // 1. Load partition table
    if (!bootloader_utility_load_partition_table(bs)) {
        ESP_LOGE(TAG, "load partition table error!");
        return INVALID_INDEX;
    }

    // 2. Select the number of boot partition
    return selected_boot_partition(bs);
}

/*
 * Selects a boot partition.
 * The conditions for switching to another firmware are checked.
 */
static int selected_boot_partition(const bootloader_state_t *bs)
{
    int boot_index = bootloader_utility_get_selected_boot_partition(bs);
    if (boot_index == INVALID_INDEX) {
        return boot_index; // Unrecoverable failure (not due to corrupt ota data or bad partition contents)
    } else {
        // Factory firmware.
#ifdef CONFIG_BOOTLOADER_FACTORY_RESET
        if (bootloader_common_check_long_hold_gpio(CONFIG_BOOTLOADER_NUM_PIN_FACTORY_RESET, CONFIG_BOOTLOADER_HOLD_TIME_GPIO) == 1) {
            ESP_LOGI(TAG, "Detect a condition of the factory reset");
            bool ota_data_erase = false;
#ifdef CONFIG_BOOTLOADER_OTA_DATA_ERASE
            ota_data_erase = true;
#endif
            const char *list_erase = CONFIG_BOOTLOADER_DATA_FACTORY_RESET;
            ESP_LOGI(TAG, "Data partitions to erase: %s", list_erase);
            if (bootloader_common_erase_part_type_data(list_erase, ota_data_erase) == false) {
                ESP_LOGE(TAG, "Not all partitions were erased");
            }
            return bootloader_utility_get_selected_boot_partition(bs);
        }
#endif
       // TEST firmware.
#ifdef CONFIG_BOOTLOADER_APP_TEST
        if (bootloader_common_check_long_hold_gpio(CONFIG_BOOTLOADER_NUM_PIN_APP_TEST, CONFIG_BOOTLOADER_HOLD_TIME_GPIO) == 1) {
            ESP_LOGI(TAG, "Detect a boot condition of the test firmware");
            if (bs->test.offset != 0) {
                boot_index = TEST_APP_INDEX;
                return boot_index;
            } else {
                ESP_LOGE(TAG, "Test firmware is not found in partition table");
                return INVALID_INDEX;
            }
        }
#endif
        // Customer implementation.
        // if (gpio_pin_1 == true && ...){
        //     boot_index = required_boot_partition;
        // } ...
    }
    return boot_index;
}
