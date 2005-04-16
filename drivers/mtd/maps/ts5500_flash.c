/*
 * ts5500_flash.c -- MTD map driver for Technology Systems TS-5500 board
 *
 * Copyright (C) 2004 Sean Young <sean@mess.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 *
 * Note:
 * - In order for detection to work, jumper 3 must be set.
 * - Drive A and B use a proprietary FTL from General Software which isn't 
 *   supported as of yet so standard drives can't be mounted; you can create 
 *   your own (e.g. jffs) file system.
 * - If you have created your own jffs file system and the bios overwrites 
 *   it during boot, try disabling Drive A: and B: in the boot order.
 *
 * $Id: ts5500_flash.c,v 1.2 2004/11/28 09:40:40 dwmw2 Exp $
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>

#ifdef CONFIG_MTD_PARTITIONS
#include <linux/mtd/partitions.h>
#endif

#define WINDOW_ADDR	0x09400000
#define WINDOW_SIZE	0x00200000

static struct map_info ts5500_map = {
	.name = "TS-5500 Flash",
	.size = WINDOW_SIZE,
	.bankwidth = 1,
	.phys = WINDOW_ADDR
};

#ifdef CONFIG_MTD_PARTITIONS
static struct mtd_partition ts5500_partitions[] = {
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

#define NUM_PARTITIONS (sizeof(ts5500_partitions)/sizeof(struct mtd_partition))

#endif

static struct mtd_info *mymtd;

static int __init init_ts5500_map(void)
{
	int rc = 0;

	ts5500_map.virt = ioremap_nocache(ts5500_map.phys, ts5500_map.size);

	if(!ts5500_map.virt) {
		printk(KERN_ERR "Failed to ioremap_nocache\n");
		rc = -EIO;
		goto err_out_ioremap;
	}

	simple_map_init(&ts5500_map);

	mymtd = do_map_probe("jedec_probe", &ts5500_map);
	if(!mymtd)
		mymtd = do_map_probe("map_rom", &ts5500_map);

	if(!mymtd) {
		rc = -ENXIO;
		goto err_out_map;
	}

	mymtd->owner = THIS_MODULE;
#ifdef CONFIG_MTD_PARTITIONS
	add_mtd_partitions(mymtd, ts5500_partitions, NUM_PARTITIONS);
#else	
	add_mtd_device(mymtd);
#endif

	return 0;

err_out_map:
	map_destroy(mymtd);
err_out_ioremap:
	iounmap(ts5500_map.virt);

	return rc;
}

static void __exit cleanup_ts5500_map(void)
{
	if (mymtd) {
#ifdef CONFIG_MTD_PARTITIONS
		del_mtd_partitions(mymtd);
#else
		del_mtd_device(mymtd);
#endif
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
MODULE_DESCRIPTION("MTD map driver for Techology Systems TS-5500 board");

