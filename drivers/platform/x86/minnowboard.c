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

#define pr_fmt(fmt) "MinnowBoard: " fmt

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/gpio-sch.h>
#include <linux/delay.h>
#include <linux/minnowboard.h>
#include "minnowboard-gpio.h"

static int minnow_hwid_val = -1;

/* leds-gpio platform device structures */
static const struct gpio_led minnow_leds[] = {
	{ .name = "minnow_led0", .gpio = GPIO_LED0, .active_low = 0,
	  .retain_state_suspended = 1, .default_state = LEDS_GPIO_DEFSTATE_ON,
	  .default_trigger = "heartbeat"},
	{ .name = "minnow_led1", .gpio = GPIO_LED1, .active_low = 0,
	  .retain_state_suspended = 1, .default_state = LEDS_GPIO_DEFSTATE_ON,
	  .default_trigger = "mmc0"},
};

static struct gpio_led_platform_data minnow_leds_platform_data = {
	.num_leds = ARRAY_SIZE(minnow_leds),
	.leds = (void *) minnow_leds,
};

static struct platform_device minnow_gpio_leds = {
	.name =	"leds-gpio",
	.id = -1,
	.dev = {
		.platform_data = &minnow_leds_platform_data,
	},
};

static struct gpio hwid_gpios[] = {
	{ GPIO_HWID0, GPIOF_DIR_IN | GPIOF_EXPORT, "minnow_gpio_hwid0" },
	{ GPIO_HWID1, GPIOF_DIR_IN | GPIOF_EXPORT, "minnow_gpio_hwid1" },
	{ GPIO_HWID2, GPIOF_DIR_IN | GPIOF_EXPORT, "minnow_gpio_hwid2" },
};

int minnow_hwid(void)
{
	/* This should never be called prior to minnow_init_module() */
	WARN_ON_ONCE(minnow_hwid_val == -1);
	return minnow_hwid_val;
}
EXPORT_SYMBOL_GPL(minnow_hwid);

bool minnow_detect(void)
{
	const char *cmp;

	cmp = dmi_get_system_info(DMI_BOARD_NAME);
	if (cmp && strstr(cmp, "MinnowBoard"))
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(minnow_detect);

bool minnow_lvds_detect(void)
{
	return !!gpio_get_value(GPIO_LVDS_DETECT);
}
EXPORT_SYMBOL_GPL(minnow_lvds_detect);


static int __init minnow_module_init(void)
{
	int err, val, i;

	err = -ENODEV;
	if (!minnow_detect())
		goto out;

#ifdef MODULE
/* Load any implicit dependencies that are not built-in */
#ifdef CONFIG_LPC_SCH_MODULE
	if (request_module("lpc_sch"))
		goto out;
#endif
#ifdef CONFIG_GPIO_SCH_MODULE
	if (request_module("gpio-sch"))
		goto out;
#endif
#ifdef CONFIG_GPIO_PCH_MODULE
	if (request_module("gpio-pch"))
		goto out;
#endif
#endif

	/* HWID GPIOs */
	err = gpio_request_array(hwid_gpios, ARRAY_SIZE(hwid_gpios));
	if (err) {
		pr_err("Failed to request hwid GPIO lines\n");
		goto out;
	}
	minnow_hwid_val = (!!gpio_get_value(GPIO_HWID0)) |
			  (!!gpio_get_value(GPIO_HWID1) << 1) |
			  (!!gpio_get_value(GPIO_HWID2) << 2);

	pr_info("Hardware ID: %d\n", minnow_hwid_val);

	err = gpio_request_one(GPIO_LVDS_DETECT, GPIOF_DIR_IN | GPIOF_EXPORT,
			       "minnow_lvds_detect");
	if (err) {
		pr_err("Failed to request LVDS_DETECT GPIO line (%d)\n",
		       GPIO_LVDS_DETECT);
		goto out;
	}

	/* Disable the GPIO lines if LVDS is detected */
	val = minnow_lvds_detect() ? 1 : 0;
	pr_info("Aux GPIO lines %s\n", val ? "Disabled" : "Enabled");
	for (i = 0; i < 5; i++)
		sch_gpio_resume_set_enable(i, !val);

	/* GPIO LEDs */
	err = platform_device_register(&minnow_gpio_leds);
	if (err) {
		pr_err("Failed to register leds-gpio platform device\n");
		goto out_lvds;
	}
	goto out;

 out_lvds:
	gpio_free(GPIO_LVDS_DETECT);

 out:
	return err;
}

static void __exit minnow_module_exit(void)
{
	gpio_free(GPIO_LVDS_DETECT);
	platform_device_unregister(&minnow_gpio_leds);
}

module_init(minnow_module_init);
module_exit(minnow_module_exit);

MODULE_LICENSE("GPL");
