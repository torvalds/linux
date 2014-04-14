/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>

static struct mtd_partition sead3_mtd_partitions[] = {
	{
		.name =		"User FS",
		.offset =	0x00000000,
		.size =		0x01fc0000,
	}, {
		.name =		"Board Config",
		.offset =	0x01fc0000,
		.size =		0x00040000,
		.mask_flags =	MTD_WRITEABLE
	},
};

static struct physmap_flash_data sead3_flash_data = {
	.width		= 4,
	.nr_parts	= ARRAY_SIZE(sead3_mtd_partitions),
	.parts		= sead3_mtd_partitions
};

static struct resource sead3_flash_resource = {
	.start		= 0x1c000000,
	.end		= 0x1dffffff,
	.flags		= IORESOURCE_MEM
};

static struct platform_device sead3_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &sead3_flash_data,
	},
	.num_resources	= 1,
	.resource	= &sead3_flash_resource,
};

static int __init sead3_mtd_init(void)
{
	platform_device_register(&sead3_flash);

	return 0;
}
device_initcall(sead3_mtd_init);
