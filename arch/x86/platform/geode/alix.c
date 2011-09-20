/*
 * System Specific setup for PCEngines ALIX.
 * At the moment this means setup of GPIO control of LEDs
 * on Alix.2/3/6 boards.
 *
 *
 * Copyright (C) 2008 Constantin Baranov <const@mimas.ru>
 * Copyright (C) 2011 Ed Wildgoose <kernel@wildgooses.com>
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
#include <linux/module.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <asm/geode.h>

static int force = 0;
module_param(force, bool, 0444);
/* FIXME: Award bios is not automatically detected as Alix platform */
MODULE_PARM_DESC(force, "Force detection as ALIX.2/ALIX.3 platform");

static struct gpio_led alix_leds[] = {
	{
		.name = "alix:1",
		.gpio = 6,
		.default_trigger = "default-on",
		.active_low = 1,
	},
	{
		.name = "alix:2",
		.gpio = 25,
		.default_trigger = "default-off",
		.active_low = 1,
	},
	{
		.name = "alix:3",
		.gpio = 27,
		.default_trigger = "default-off",
		.active_low = 1,
	},
};

static struct gpio_led_platform_data alix_leds_data = {
	.num_leds = ARRAY_SIZE(alix_leds),
	.leds = alix_leds,
};

static struct platform_device alix_leds_dev = {
	.name = "leds-gpio",
	.id = -1,
	.dev.platform_data = &alix_leds_data,
};

static void __init register_alix(void)
{
	/* Setup LED control through leds-gpio driver */
	platform_device_register(&alix_leds_dev);
}

static int __init alix_present(unsigned long bios_phys,
				const char *alix_sig,
				size_t alix_sig_len)
{
	const size_t bios_len = 0x00010000;
	const char *bios_virt;
	const char *scan_end;
	const char *p;
	char name[64];

	if (force) {
		printk(KERN_NOTICE "%s: forced to skip BIOS test, "
		       "assume system is ALIX.2/ALIX.3\n",
		       KBUILD_MODNAME);
		return 1;
	}

	bios_virt = phys_to_virt(bios_phys);
	scan_end = bios_virt + bios_len - (alix_sig_len + 2);
	for (p = bios_virt; p < scan_end; p++) {
		const char *tail;
		char *a;

		if (memcmp(p, alix_sig, alix_sig_len) != 0)
			continue;

		memcpy(name, p, sizeof(name));

		/* remove the first \0 character from string */
		a = strchr(name, '\0');
		if (a)
			*a = ' ';

		/* cut the string at a newline */
		a = strchr(name, '\r');
		if (a)
			*a = '\0';

		tail = p + alix_sig_len;
		if ((tail[0] == '2' || tail[0] == '3')) {
			printk(KERN_INFO
			       "%s: system is recognized as \"%s\"\n",
			       KBUILD_MODNAME, name);
			return 1;
		}
	}

	return 0;
}

static int __init alix_init(void)
{
	const char tinybios_sig[] = "PC Engines ALIX.";
	const char coreboot_sig[] = "PC Engines\0ALIX.";

	if (!is_geode())
		return 0;

	if (alix_present(0xf0000, tinybios_sig, sizeof(tinybios_sig) - 1) ||
	    alix_present(0x500, coreboot_sig, sizeof(coreboot_sig) - 1))
		register_alix();

	return 0;
}

module_init(alix_init);

MODULE_AUTHOR("Ed Wildgoose <kernel@wildgooses.com>");
MODULE_DESCRIPTION("PCEngines ALIX System Setup");
MODULE_LICENSE("GPL");
