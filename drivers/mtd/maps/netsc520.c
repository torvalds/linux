// SPDX-License-Identifier: GPL-2.0-or-later
/* netsc520.c -- MTD map driver for AMD NetSc520 Demonstration Board
 *
 * Copyright (C) 2001 Mark Langsdorf (mark.langsdorf@amd.com)
 *	based on sc520cdp.c by Sysgo Real-Time Solutions GmbH
 *
 * The NetSc520 is a demonstration board for the Elan Sc520 processor available
 * from AMD.  It has a single back of 16 megs of 32-bit Flash ROM and another
 * 16 megs of SDRAM.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>


/*
** The single, 16 megabyte flash bank is divided into four virtual
** partitions.  The first partition is 768 KiB and is intended to
** store the kernel image loaded by the bootstrap loader.  The second
** partition is 256 KiB and holds the BIOS image.  The third
** partition is 14.5 MiB and is intended for the flash file system
** image.  The last partition is 512 KiB and contains another copy
** of the BIOS image and the reset vector.
**
** Only the third partition should be mounted.  The first partition
** should not be mounted, but it can erased and written to using the
** MTD character routines.  The second and fourth partitions should
** not be touched - it is possible to corrupt the BIOS image by
** mounting these partitions, and potentially the board will not be
** recoverable afterwards.
*/

/* partition_info gives details on the logical partitions that the split the
 * single flash device into. If the size if zero we use up to the end of the
 * device. */
static const struct mtd_partition partition_info[] = {
    {
	    .name = "NetSc520 boot kernel",
	    .offset = 0,
	    .size = 0xc0000
    },
    {
	    .name = "NetSc520 Low BIOS",
	    .offset = 0xc0000,
	    .size = 0x40000
    },
    {
	    .name = "NetSc520 file system",
	    .offset = 0x100000,
	    .size = 0xe80000
    },
    {
	    .name = "NetSc520 High BIOS",
	    .offset = 0xf80000,
	    .size = 0x80000
    },
};
#define NUM_PARTITIONS ARRAY_SIZE(partition_info)

#define WINDOW_SIZE	0x00100000
#define WINDOW_ADDR	0x00200000

static struct map_info netsc520_map = {
	.name = "netsc520 Flash Bank",
	.size = WINDOW_SIZE,
	.bankwidth = 4,
	.phys = WINDOW_ADDR,
};

#define NUM_FLASH_BANKS	ARRAY_SIZE(netsc520_map)

static struct mtd_info *mymtd;

static int __init init_netsc520(void)
{
	printk(KERN_NOTICE "NetSc520 flash device: 0x%Lx at 0x%Lx\n",
			(unsigned long long)netsc520_map.size,
			(unsigned long long)netsc520_map.phys);
	netsc520_map.virt = ioremap_nocache(netsc520_map.phys, netsc520_map.size);

	if (!netsc520_map.virt) {
		printk("Failed to ioremap_nocache\n");
		return -EIO;
	}

	simple_map_init(&netsc520_map);

	mymtd = do_map_probe("cfi_probe", &netsc520_map);
	if(!mymtd)
		mymtd = do_map_probe("map_ram", &netsc520_map);
	if(!mymtd)
		mymtd = do_map_probe("map_rom", &netsc520_map);

	if (!mymtd) {
		iounmap(netsc520_map.virt);
		return -ENXIO;
	}

	mymtd->owner = THIS_MODULE;
	mtd_device_register(mymtd, partition_info, NUM_PARTITIONS);
	return 0;
}

static void __exit cleanup_netsc520(void)
{
	if (mymtd) {
		mtd_device_unregister(mymtd);
		map_destroy(mymtd);
	}
	if (netsc520_map.virt) {
		iounmap(netsc520_map.virt);
		netsc520_map.virt = NULL;
	}
}

module_init(init_netsc520);
module_exit(cleanup_netsc520);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Langsdorf <mark.langsdorf@amd.com>");
MODULE_DESCRIPTION("MTD map driver for AMD NetSc520 Demonstration Board");
