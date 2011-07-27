/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/input.h>

#include <lantiq.h>

#include "../machtypes.h"
#include "devices.h"

static struct mtd_partition easy50601_partitions[] = {
	{
		.name	= "uboot",
		.offset	= 0x0,
		.size	= 0x10000,
	},
	{
		.name	= "uboot_env",
		.offset	= 0x10000,
		.size	= 0x10000,
	},
	{
		.name	= "linux",
		.offset	= 0x20000,
		.size	= 0xE0000,
	},
	{
		.name	= "rootfs",
		.offset	= 0x100000,
		.size	= 0x300000,
	},
};

static struct physmap_flash_data easy50601_flash_data = {
	.nr_parts	= ARRAY_SIZE(easy50601_partitions),
	.parts		= easy50601_partitions,
};

static void __init easy50601_init(void)
{
	ltq_register_nor(&easy50601_flash_data);
}

MIPS_MACHINE(LTQ_MACH_EASY50601,
			"EASY50601",
			"EASY50601 Eval Board",
			easy50601_init);
