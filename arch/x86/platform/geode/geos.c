/*
 * System Specific setup for Traverse Technologies GEOS.
 * At the moment this means setup of GPIO control of LEDs.
 *
 * Copyright (C) 2008 Constantin Baranov <const@mimas.ru>
 * Copyright (C) 2011 Ed Wildgoose <kernel@wildgooses.com>
 *                and Philip Prindeville <philipp@redfish-solutions.com>
 *
 * TODO: There are large similarities with leds-net5501.c
 * by Alessandro Zummo <a.zummo@towertech.it>
 * In the future leds-net5501.c should be migrated over to platform
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/dmi.h>

#include <asm/geode.h>

static struct gpio_keys_button geos_gpio_buttons[] = {
	{
		.code = KEY_RESTART,
		.gpio = 3,
		.active_low = 1,
		.desc = "Reset button",
		.type = EV_KEY,
		.wakeup = 0,
		.debounce_interval = 100,
		.can_disable = 0,
	}
};
static struct gpio_keys_platform_data geos_buttons_data = {
	.buttons = geos_gpio_buttons,
	.nbuttons = ARRAY_SIZE(geos_gpio_buttons),
	.poll_interval = 20,
};

static struct platform_device geos_buttons_dev = {
	.name = "gpio-keys-polled",
	.id = 1,
	.dev = {
		.platform_data = &geos_buttons_data,
	}
};

static struct gpio_led geos_leds[] = {
	{
		.name = "geos:1",
		.gpio = 6,
		.default_trigger = "default-on",
		.active_low = 1,
	},
	{
		.name = "geos:2",
		.gpio = 25,
		.default_trigger = "default-off",
		.active_low = 1,
	},
	{
		.name = "geos:3",
		.gpio = 27,
		.default_trigger = "default-off",
		.active_low = 1,
	},
};

static struct gpio_led_platform_data geos_leds_data = {
	.num_leds = ARRAY_SIZE(geos_leds),
	.leds = geos_leds,
};

static struct platform_device geos_leds_dev = {
	.name = "leds-gpio",
	.id = -1,
	.dev.platform_data = &geos_leds_data,
};

static struct platform_device *geos_devs[] __initdata = {
	&geos_buttons_dev,
	&geos_leds_dev,
};

static void __init register_geos(void)
{
	/* Setup LED control through leds-gpio driver */
	platform_add_devices(geos_devs, ARRAY_SIZE(geos_devs));
}

static int __init geos_init(void)
{
	const char *vendor, *product;

	if (!is_geode())
		return 0;

	vendor = dmi_get_system_info(DMI_SYS_VENDOR);
	if (!vendor || strcmp(vendor, "Traverse Technologies"))
		return 0;

	product = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (!product || strcmp(product, "Geos"))
		return 0;

	printk(KERN_INFO "%s: system is recognized as \"%s %s\"\n",
	       KBUILD_MODNAME, vendor, product);

	register_geos();

	return 0;
}
device_initcall(geos_init);
