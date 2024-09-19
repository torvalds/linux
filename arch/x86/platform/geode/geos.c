// SPDX-License-Identifier: GPL-2.0-only
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
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/dmi.h>

#include <asm/geode.h>

#include "geode-common.h"

static const struct geode_led geos_leds[] __initconst = {
	{ 6, true },
	{ 25, false },
	{ 27, false },
};

static void __init register_geos(void)
{
	geode_create_restart_key(3);
	geode_create_leds("geos", geos_leds, ARRAY_SIZE(geos_leds));
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
