/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 MIPS Technologies, Inc.
 *     written by Ralf Baechle <ralf@linux-mips.org>
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <mtd/mtd-abi.h>

static struct mtd_partition malta_mtd_partitions[] = {
	{
		.name =		"YAMON",
		.offset =	0x0,
		.size =		0x100000,
		.mask_flags =	MTD_WRITEABLE
	}, {
		.name =		"User FS",
		.offset = 	0x100000,
		.size =		0x2e0000
	}, {
		.name =		"Board Config",
		.offset =	0x3e0000,
		.size =		0x020000,
		.mask_flags =	MTD_WRITEABLE
	}
};

static struct physmap_flash_data malta_flash_data = {
	.width		= 4,
	.nr_parts	= ARRAY_SIZE(malta_mtd_partitions),
	.parts		= malta_mtd_partitions
};

static struct resource malta_flash_resource = {
	.start		= 0x1e000000,
	.end		= 0x1e3fffff,
	.flags		= IORESOURCE_MEM
};

static struct platform_device malta_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &malta_flash_data,
	},
	.num_resources	= 1,
	.resource	= &malta_flash_resource,
};

static int __init malta_mtd_init(void)
{
	platform_device_register(&malta_flash);

	return 0;
}

module_init(malta_mtd_init)
