/*
 * arch/arm/mach-tegra/board-olympus-keypad.c
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
#include <linux/gpio_event.h>
#include <linux/keyreset.h>
#include <linux/gpio.h>
#include <asm/mach-types.h>
#include "gpio-names.h"

static unsigned int olympus_row_gpios[] = {
	TEGRA_GPIO_PR0,
	TEGRA_GPIO_PR1,
	TEGRA_GPIO_PR2
};
static unsigned int olympus_col_gpios[] = {
	TEGRA_GPIO_PQ0,
	TEGRA_GPIO_PQ1,
	TEGRA_GPIO_PQ2
};

#define KEYMAP_INDEX(col, row) ((col)*ARRAY_SIZE(olympus_row_gpios) + (row))

static const unsigned short olympus_p3_keymap[ARRAY_SIZE(olympus_col_gpios) *
					     ARRAY_SIZE(olympus_row_gpios)] = {
	[KEYMAP_INDEX(0, 0)] = KEY_VOLUMEUP,
	[KEYMAP_INDEX(1, 0)] = KEY_CAMERA_FOCUS,
	[KEYMAP_INDEX(2, 0)] = KEY_MENU,
	[KEYMAP_INDEX(0, 1)] = KEY_VOLUMEDOWN,
	[KEYMAP_INDEX(1, 1)] = KEY_CAMERA,
	[KEYMAP_INDEX(2, 1)] = KEY_HOME,
	[KEYMAP_INDEX(0, 2)] = KEY_AUX,
	[KEYMAP_INDEX(1, 2)] = KEY_SEARCH,
	[KEYMAP_INDEX(2, 2)] = KEY_BACK,
};

static struct gpio_event_matrix_info olympus_keypad_matrix_info = {
	.info.func = gpio_event_matrix_func,
	.keymap = olympus_p3_keymap,
	.output_gpios = olympus_col_gpios,
	.input_gpios = olympus_row_gpios,
	.noutputs = ARRAY_SIZE(olympus_col_gpios),
	.ninputs = ARRAY_SIZE(olympus_row_gpios),
	.settle_time.tv.nsec = 40 * NSEC_PER_USEC,
	.poll_time.tv.nsec = 20 * NSEC_PER_MSEC,
	.flags = GPIOKPF_LEVEL_TRIGGERED_IRQ | GPIOKPF_REMOVE_PHANTOM_KEYS |
		 GPIOKPF_PRINT_UNMAPPED_KEYS /*| GPIOKPF_PRINT_MAPPED_KEYS*/
};

static struct gpio_event_direct_entry olympus_keypad_switch_map[] = {
};

static struct gpio_event_input_info olympus_keypad_switch_info = {
	.info.func = gpio_event_input_func,
	.flags = 0,
	.type = EV_SW,
	.keymap = olympus_keypad_switch_map,
	.keymap_size = ARRAY_SIZE(olympus_keypad_switch_map)
};

static struct gpio_event_info *olympus_keypad_info[] = {
	&olympus_keypad_matrix_info.info,
	&olympus_keypad_switch_info.info,
};

static struct gpio_event_platform_data olympus_keypad_data = {
	.name = "olympus-keypad",
	.info = olympus_keypad_info,
	.info_count = ARRAY_SIZE(olympus_keypad_info)
};

static struct platform_device olympus_keypad_device = {
	.name = GPIO_EVENT_DEV_NAME,
	.id = 0,
	.dev		= {
		.platform_data	= &olympus_keypad_data,
	},
};

static int olympus_reset_keys_up[] = {
	BTN_MOUSE,		/* XXX */
        0
};

static struct keyreset_platform_data olympus_reset_keys_pdata = {
	.keys_up = olympus_reset_keys_up,
	.keys_down = {
		KEY_LEFTSHIFT,
		KEY_LEFTALT,
		KEY_BACKSPACE,
		0
	},
};

static struct platform_device olympus_reset_keys_device = {
         .name = KEYRESET_NAME,
         .dev.platform_data = &olympus_reset_keys_pdata,
};

int __init olympus_keypad_init(void)
{
	tegra_gpio_enable(TEGRA_GPIO_PR0);
	tegra_gpio_enable(TEGRA_GPIO_PR1);
	tegra_gpio_enable(TEGRA_GPIO_PR2);
	tegra_gpio_enable(TEGRA_GPIO_PQ0);
	tegra_gpio_enable(TEGRA_GPIO_PQ1);
	tegra_gpio_enable(TEGRA_GPIO_PQ2);
	platform_device_register(&olympus_reset_keys_device);
	return platform_device_register(&olympus_keypad_device);
}
