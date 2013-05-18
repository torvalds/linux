/*
 * MinnowBoard Linux platform driver
 * Copyright (c) 2013, Intel Corporation.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Author: Darren Hart <dvhart@linux.intel.com>
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/minnowboard.h>
#include "minnowboard-gpio.h"

/* VI-style direction keys seem like as good as anything */
#define GPIO_BTN0_KEY KEY_LEFT
#define GPIO_BTN1_KEY KEY_DOWN
#define GPIO_BTN2_KEY KEY_UP
#define GPIO_BTN3_KEY KEY_RIGHT

/* Timing in milliseconds */
#define GPIO_DEBOUNCE 1
#define BUTTON_POLL_INTERVAL 300

/* gpio-keys platform device structures */
static struct gpio_keys_button minnow_buttons[] = {
	{ .code = GPIO_BTN0_KEY, .gpio = GPIO_BTN0, .active_low = 1,
	  .desc = "minnow_btn0", .type = EV_KEY, .wakeup = 0,
	  .debounce_interval = GPIO_DEBOUNCE, .can_disable = true },
	{ .code = GPIO_BTN1_KEY, .gpio = GPIO_BTN1, .active_low = 1,
	  .desc = "minnow_btn1", .type = EV_KEY, .wakeup = 0,
	  .debounce_interval = GPIO_DEBOUNCE, .can_disable = true },
	{ .code = GPIO_BTN2_KEY, .gpio = GPIO_BTN2, .active_low = 1,
	  .desc = "minnow_btn2", .type = EV_KEY, .wakeup = 0,
	  .debounce_interval = GPIO_DEBOUNCE, .can_disable = true },
	{ .code = GPIO_BTN3_KEY, .gpio = GPIO_BTN3, .active_low = 1,
	  .desc = "minnow_btn3", .type = EV_KEY, .wakeup = 0,
	  .debounce_interval = GPIO_DEBOUNCE, .can_disable = true },
};

static const struct gpio_keys_platform_data minnow_buttons_platform_data = {
	.buttons = minnow_buttons,
	.nbuttons = ARRAY_SIZE(minnow_buttons),
	.poll_interval = BUTTON_POLL_INTERVAL,
	.rep = 1,
	.enable = NULL,
	.disable = NULL,
	.name = "minnow_buttons",
};

static struct platform_device minnow_gpio_buttons = {
	.name = "gpio-keys-polled",
	.id = -1,
	.dev = {
		.platform_data = (void *) &minnow_buttons_platform_data,
	},
};

static int __init minnow_keys_module_init(void)
{
	int err;

	err = -ENODEV;
	if (!minnow_detect())
		goto out;

	/* Export GPIO buttons to sysfs */
	err = platform_device_register(&minnow_gpio_buttons);
	if (err) {
		pr_err("Failed to register gpio-keys-polled platform device\n");
		goto out;
	}

 out:
	return err;
}

static void __exit minnow_keys_module_exit(void)
{
	platform_device_unregister(&minnow_gpio_buttons);
}

module_init(minnow_keys_module_init);
module_exit(minnow_keys_module_exit);

MODULE_LICENSE("GPL");
