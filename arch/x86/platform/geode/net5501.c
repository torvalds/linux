// SPDX-License-Identifier: GPL-2.0-only
/*
 * System Specific setup for Soekris net5501
 * At the moment this means setup of GPIO control of LEDs and buttons
 * on net5501 boards.
 *
 * Copyright (C) 2008-2009 Tower Technologies
 * Written by Alessandro Zummo <a.zummo@towertech.it>
 *
 * Copyright (C) 2008 Constantin Baranov <const@mimas.ru>
 * Copyright (C) 2011 Ed Wildgoose <kernel@wildgooses.com>
 *                and Philip Prindeville <philipp@redfish-solutions.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/input.h>
#include <linux/gpio/machine.h>
#include <linux/gpio/property.h>

#include <asm/geode.h>

#include "geode-common.h"

#define BIOS_REGION_BASE		0xffff0000
#define BIOS_REGION_SIZE		0x00010000

static const struct geode_led net5501_leds[] __initconst = {
	{ 6, true },
};

static void __init register_net5501(void)
{
	geode_create_restart_key(24);
	geode_create_leds("net5501", net5501_leds, ARRAY_SIZE(net5501_leds));
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
device_initcall(net5501_init);
