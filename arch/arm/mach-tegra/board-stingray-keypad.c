/*
 * arch/arm/mach-tegra/board-stingray-keypad.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/keyreset.h>
#include <linux/gpio_event.h>
#include <linux/gpio.h>
#include <asm/mach-types.h>

#include "board-stingray.h"
#include "gpio-names.h"

static unsigned int stingray_row_gpios[] = {
	TEGRA_GPIO_PR0,
	TEGRA_GPIO_PR1
};
static unsigned int stingray_col_gpios[] = {
	TEGRA_GPIO_PQ0
};

#define KEYMAP_INDEX(col, row) ((col)*ARRAY_SIZE(stingray_row_gpios) + (row))

static const unsigned short stingray_p3_keymap[ARRAY_SIZE(stingray_col_gpios) *
					     ARRAY_SIZE(stingray_row_gpios)] = {
	[KEYMAP_INDEX(0, 0)] = KEY_VOLUMEUP,
	[KEYMAP_INDEX(0, 1)] = KEY_VOLUMEDOWN
};

static struct gpio_event_matrix_info stingray_keypad_matrix_info = {
	.info.func = gpio_event_matrix_func,
	.keymap = stingray_p3_keymap,
	.output_gpios = stingray_col_gpios,
	.input_gpios = stingray_row_gpios,
	.noutputs = ARRAY_SIZE(stingray_col_gpios),
	.ninputs = ARRAY_SIZE(stingray_row_gpios),
	.settle_time.tv.nsec = 40 * NSEC_PER_USEC,
	.poll_time.tv.nsec = 20 * NSEC_PER_MSEC,
	.flags = GPIOKPF_LEVEL_TRIGGERED_IRQ | GPIOKPF_REMOVE_PHANTOM_KEYS |
		 GPIOKPF_PRINT_UNMAPPED_KEYS /*| GPIOKPF_PRINT_MAPPED_KEYS*/
};

static struct gpio_event_direct_entry stingray_keypad_switch_map[] = {
};

static struct gpio_event_input_info stingray_keypad_switch_info = {
	.info.func = gpio_event_input_func,
	.flags = 0,
	.type = EV_SW,
	.keymap = stingray_keypad_switch_map,
	.keymap_size = ARRAY_SIZE(stingray_keypad_switch_map)
};

static struct gpio_event_info *stingray_keypad_info[] = {
	&stingray_keypad_matrix_info.info,
	&stingray_keypad_switch_info.info,
};

static struct gpio_event_platform_data stingray_keypad_data = {
	.name = "stingray-keypad",
	.info = stingray_keypad_info,
	.info_count = ARRAY_SIZE(stingray_keypad_info)
};

static struct platform_device stingray_keypad_device = {
	.name = GPIO_EVENT_DEV_NAME,
	.id = 0,
	.dev		= {
		.platform_data	= &stingray_keypad_data,
	},
};

int stingray_log_reset(void)
{
	pr_warn("Hard reset buttons pushed\n");
	return 0;
}

static struct keyreset_platform_data stingray_reset_keys_pdata = {
	.reset_fn = stingray_log_reset,
	.keys_down = {
		KEY_END,
		KEY_VOLUMEUP,
		0
	},
};

struct platform_device stingray_keyreset_device = {
	.name	= KEYRESET_NAME,
	.dev	= {
		.platform_data = &stingray_reset_keys_pdata,
	},
};


int __init stingray_keypad_init(void)
{
	tegra_gpio_enable(TEGRA_GPIO_PR0);
	tegra_gpio_enable(TEGRA_GPIO_PR1);
	tegra_gpio_enable(TEGRA_GPIO_PQ0);
	platform_device_register(&stingray_keyreset_device);
	return platform_device_register(&stingray_keypad_device);
}
