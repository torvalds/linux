/*
 * System Specific setup for Soekris net5501
 * At the moment this means setup of GPIO control of LEDs and buttons
 * on net5501 boards.
 *
 *
 * Copyright (C) 2008-2009 Tower Technologies
 * Written by Alessandro Zummo <a.zummo@towertech.it>
 *
 * Copyright (C) 2008 Constantin Baranov <const@mimas.ru>
 * Copyright (C) 2011 Ed Wildgoose <kernel@wildgooses.com>
 *                and Philip Prindeville <philipp@redfish-solutions.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>

#include <asm/geode.h>

#define BIOS_REGION_BASE		0xffff0000
#define BIOS_REGION_SIZE		0x00010000

static struct gpio_keys_button net5501_gpio_buttons[] = {
	{
		.code = KEY_RESTART,
		.gpio = 24,
		.active_low = 1,
		.desc = "Reset button",
		.type = EV_KEY,
		.wakeup = 0,
		.debounce_interval = 100,
		.can_disable = 0,
	}
};
static struct gpio_keys_platform_data net5501_buttons_data = {
	.buttons = net5501_gpio_buttons,
	.nbuttons = ARRAY_SIZE(net5501_gpio_buttons),
	.poll_interval = 20,
};

static struct platform_device net5501_buttons_dev = {
	.name = "gpio-keys-polled",
	.id = 1,
	.dev = {
		.platform_data = &net5501_buttons_data,
	}
};

static struct gpio_led net5501_leds[] = {
	{
		.name = "net5501:1",
		.gpio = 6,
		.default_trigger = "default-on",
		.active_low = 0,
	},
};

static struct gpio_led_platform_data net5501_leds_data = {
	.num_leds = ARRAY_SIZE(net5501_leds),
	.leds = net5501_leds,
};

static struct platform_device net5501_leds_dev = {
	.name = "leds-gpio",
	.id = -1,
	.dev.platform_data = &net5501_leds_data,
};

static struct platform_device *net5501_devs[] __initdata = {
	&net5501_buttons_dev,
	&net5501_leds_dev,
};

static void __init register_net5501(void)
{
	/* Setup LED control through leds-gpio driver */
	platform_add_devices(net5501_devs, ARRAY_SIZE(net5501_devs));
}

struct net5501_board {
	u16	offset;
	u16	len;
	char	*sig;
};

static struct net5501_board __initdata boards[] = {
	{ 0xb7b, 7, "net5501" },	/* net5501 v1.33/1.33c */
	{ 0xb1f, 7, "net5501" },	/* net5501 v1.32i */
};

static bool __init net5501_present(void)
{
	int i;
	unsigned char *rombase, *bios;
	bool found = false;

	rombase = ioremap(BIOS_REGION_BASE, BIOS_REGION_SIZE - 1);
	if (!rombase) {
		printk(KERN_ERR "%s: failed to get rombase\n", KBUILD_MODNAME);
		return found;
	}

	bios = rombase + 0x20;	/* null terminated */

	if (memcmp(bios, "comBIOS", 7))
		goto unmap;

	for (i = 0; i < ARRAY_SIZE(boards); i++) {
		unsigned char *model = rombase + boards[i].offset;

		if (!memcmp(model, boards[i].sig, boards[i].len)) {
			printk(KERN_INFO "%s: system is recognized as \"%s\"\n",
			       KBUILD_MODNAME, model);

			found = true;
			break;
		}
	}

unmap:
	iounmap(rombase);
	return found;
}

static int __init net5501_init(void)
{
	if (!is_geode())
		return 0;

	if (!net5501_present())
		return 0;

	register_net5501();

	return 0;
}

module_init(net5501_init);

MODULE_AUTHOR("Philip Prindeville <philipp@redfish-solutions.com>");
MODULE_DESCRIPTION("Soekris net5501 System Setup");
MODULE_LICENSE("GPL");
