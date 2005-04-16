/*
 *	pnc2000.c - mapper for Photron PNC-2000 board.
 *
 * Copyright (C) 2000 Crossnet Co. <info@crossnet.co.jp>
 *
 * This code is GPL
 *
 * $Id: pnc2000.c,v 1.17 2004/11/16 18:29:02 dwmw2 Exp $
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>


#define WINDOW_ADDR 0xbf000000
#define WINDOW_SIZE 0x00400000

/* 
 * MAP DRIVER STUFF
 */


static struct map_info pnc_map = {
	.name = "PNC-2000",
	.size = WINDOW_SIZE,
	.bankwidth = 4,
	.phys = 0xFFFFFFFF,
	.virt = (void __iomem *)WINDOW_ADDR,
};


/*
 * MTD 'PARTITIONING' STUFF 
 */
static struct mtd_partition pnc_partitions[3] = {
	{
		.name = "PNC-2000 boot firmware",
		.size = 0x20000,
		.offset = 0
	},
	{
		.name = "PNC-2000 kernel",
		.size = 0x1a0000,
		.offset = 0x20000
	},
	{
		.name = "PNC-2000 filesystem",
		.size = 0x240000,
		.offset = 0x1c0000
	}
};

/* 
 * This is the master MTD device for which all the others are just
 * auto-relocating aliases.
 */
static struct mtd_info *mymtd;

static int __init init_pnc2000(void)
{
	printk(KERN_NOTICE "Photron PNC-2000 flash mapping: %x at %x\n", WINDOW_SIZE, WINDOW_ADDR);

	simple_map_init(&pnc_map);

	mymtd = do_map_probe("cfi_probe", &pnc_map);
	if (mymtd) {
		mymtd->owner = THIS_MODULE;
		return add_mtd_partitions(mymtd, pnc_partitions, 3);
	}

	return -ENXIO;
}

static void __exit cleanup_pnc2000(void)
{
	if (mymtd) {
		del_mtd_partitions(mymtd);
		map_destroy(mymtd);
	}
}

module_init(init_pnc2000);
module_exit(cleanup_pnc2000);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Crossnet Co. <info@crossnet.co.jp>");
MODULE_DESCRIPTION("MTD map driver for Photron PNC-2000 board");
