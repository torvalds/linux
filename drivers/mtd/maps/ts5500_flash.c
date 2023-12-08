// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ts5500_flash.c -- MTD map driver for Technology Systems TS-5500 board
 *
 * Copyright (C) 2004 Sean Young <sean@mess.org>
 *
 * Note:
 * - In order for detection to work, jumper 3 must be set.
 * - Drive A and B use the resident flash disk (RFD) flash translation layer.
 * - If you have created your own jffs file system and the bios overwrites
 *   it during boot, try disabling Drive A: and B: in the boot order.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mtd/map.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/types.h>


#define WINDOW_ADDR	0x09400000
#define WINDOW_SIZE	0x00200000

static struct map_info ts5500_map = {
	.name = "TS-5500 Flash",
	.size = WINDOW_SIZE,
	.bankwidth = 1,
	.phys = WINDOW_ADDR
};

static const struct mtd_partition ts5500_partitions[] = {
	{
		.name = "Drive A",
		.offset = 0,
		.size = 0x0e0000
	},
	{
		.name = "BIOS",
		.offset = 0x0e0000,
		.size = 0x020000,
	},
	{
		.name = "Drive B",
		.offset = 0x100000,
		.size = 0x100000
	}
};

#define NUM_PARTITIONS ARRAY_SIZE(ts5500_partitions)

static struct mtd_info *mymtd;

static int __init init_ts5500_map(void)
{
	int rc = 0;

	ts5500_map.virt = ioremap(ts5500_map.phys, ts5500_map.size);

	if (!ts5500_map.virt) {
		printk(KERN_ERR "Failed to ioremap\n");
		rc = -EIO;
		goto err2;
	}

	simple_map_init(&ts5500_map);

	mymtd = do_map_probe("jedec_probe", &ts5500_map);
	if (!mymtd)
		mymtd = do_map_probe("map_rom", &ts5500_map);

	if (!mymtd) {
		rc = -ENXIO;
		goto err1;
	}

	mymtd->owner = THIS_MODULE;
	mtd_device_register(mymtd, ts5500_partitions, NUM_PARTITIONS);

	return 0;

err1:
	iounmap(ts5500_map.virt);
err2:
	return rc;
}

static void __exit cleanup_ts5500_map(void)
{
	if (mymtd) {
		mtd_device_unregister(mymtd);
		map_destroy(mymtd);
	}

	if (ts5500_map.virt) {
		iounmap(ts5500_map.virt);
		ts5500_map.virt = NULL;
	}
}

module_init(init_ts5500_map);
module_exit(cleanup_ts5500_map);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sean Young <sean@mess.org>");
MODULE_DESCRIPTION("MTD map driver for Technology Systems TS-5500 board");

