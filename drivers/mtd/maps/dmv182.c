
/*
 * drivers/mtd/maps/svme182.c
 *
 * Flash map driver for the Dy4 SVME182 board
 *
 * $Id: dmv182.c,v 1.6 2005/11/07 11:14:26 gleixner Exp $
 *
 * Copyright 2003-2004, TimeSys Corporation
 *
 * Based on the SVME181 flash map, by Tom Nelson, Dot4, Inc. for TimeSys Corp.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/errno.h>

/*
 * This driver currently handles only the 16MiB user flash bank 1 on the
 * board.  It does not provide access to bank 0 (contains the Dy4 FFW), bank 2
 * (VxWorks boot), or the optional 48MiB expansion flash.
 *
 * scott.wood@timesys.com: On the newer boards with 128MiB flash, it
 * now supports the first 96MiB (the boot flash bank containing FFW
 * is excluded).  The VxWorks loader is in partition 1.
 */

#define FLASH_BASE_ADDR 0xf0000000
#define FLASH_BANK_SIZE (128*1024*1024)

MODULE_AUTHOR("Scott Wood, TimeSys Corporation <scott.wood@timesys.com>");
MODULE_DESCRIPTION("User-programmable flash device on the Dy4 SVME182 board");
MODULE_LICENSE("GPL");

static struct map_info svme182_map = {
	.name		= "Dy4 SVME182",
	.bankwidth	= 32,
	.size		=  128 * 1024 * 1024
};

#define BOOTIMAGE_PART_SIZE		((6*1024*1024)-RESERVED_PART_SIZE)

// Allow 6MiB for the kernel
#define NEW_BOOTIMAGE_PART_SIZE  (6 * 1024 * 1024)
// Allow 1MiB for the bootloader
#define NEW_BOOTLOADER_PART_SIZE (1024 * 1024)
// Use the remaining 9MiB at the end of flash for the RFS
#define NEW_RFS_PART_SIZE        (0x01000000 - NEW_BOOTLOADER_PART_SIZE - \
                                  NEW_BOOTIMAGE_PART_SIZE)

static struct mtd_partition svme182_partitions[] = {
	// The Lower PABS is only 128KiB, but the partition code doesn't
	// like partitions that don't end on the largest erase block
	// size of the device, even if all of the erase blocks in the
	// partition are small ones.  The hardware should prevent
	// writes to the actual PABS areas.
	{
		name:       "Lower PABS and CPU 0 bootloader or kernel",
		size:       6*1024*1024,
		offset:     0,
	},
	{
		name:       "Root Filesystem",
		size:       10*1024*1024,
		offset:     MTDPART_OFS_NXTBLK
	},
	{
		name:       "CPU1 Bootloader",
		size:       1024*1024,
		offset:     MTDPART_OFS_NXTBLK,
	},
	{
		name:       "Extra",
		size:       110*1024*1024,
		offset:     MTDPART_OFS_NXTBLK
	},
	{
		name:       "Foundation Firmware and Upper PABS",
		size:       1024*1024,
		offset:     MTDPART_OFS_NXTBLK,
		mask_flags: MTD_WRITEABLE // read-only
	}
};

static struct mtd_info *this_mtd;

static int __init init_svme182(void)
{
	struct mtd_partition *partitions;
	int num_parts = ARRAY_SIZE(svme182_partitions);

	partitions = svme182_partitions;

	svme182_map.virt = ioremap(FLASH_BASE_ADDR, svme182_map.size);

	if (svme182_map.virt == 0) {
		printk("Failed to ioremap FLASH memory area.\n");
		return -EIO;
	}

	simple_map_init(&svme182_map);

	this_mtd = do_map_probe("cfi_probe", &svme182_map);
	if (!this_mtd)
	{
		iounmap((void *)svme182_map.virt);
		return -ENXIO;
	}

	printk(KERN_NOTICE "SVME182 flash device: %dMiB at 0x%08x\n",
		   this_mtd->size >> 20, FLASH_BASE_ADDR);

	this_mtd->owner = THIS_MODULE;
	add_mtd_partitions(this_mtd, partitions, num_parts);

	return 0;
}

static void __exit cleanup_svme182(void)
{
	if (this_mtd)
	{
		del_mtd_partitions(this_mtd);
		map_destroy(this_mtd);
	}

	if (svme182_map.virt)
	{
		iounmap((void *)svme182_map.virt);
		svme182_map.virt = 0;
	}

	return;
}

module_init(init_svme182);
module_exit(cleanup_svme182);
