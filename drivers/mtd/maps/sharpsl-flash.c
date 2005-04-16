/*
 * sharpsl-flash.c
 * 
 * Copyright (C) 2001 Lineo Japan, Inc.
 * Copyright (C) 2002  SHARP
 *
 * $Id: sharpsl-flash.c,v 1.2 2004/11/24 20:38:06 rpurdie Exp $
 *
 * based on rpxlite.c,v 1.15 2001/10/02 15:05:14 dwmw2 Exp
 *          Handle mapping of the flash on the RPX Lite and CLLF boards
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
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#define WINDOW_ADDR 0x00000000
#define WINDOW_SIZE 0x01000000
#define BANK_WIDTH 2

static struct mtd_info *mymtd;

struct map_info sharpsl_map = {
	.name = "sharpsl-flash",
	.size = WINDOW_SIZE,
	.bankwidth = BANK_WIDTH,
	.phys = WINDOW_ADDR
};

static struct mtd_partition sharpsl_partitions[1] = {
	{
		name:		"Filesystem",
		size:		0x006d0000,
		offset:		0x00120000
	}
};

#define NB_OF(x)  (sizeof(x)/sizeof(x[0]))

int __init init_sharpsl(void)
{
	struct mtd_partition *parts;
	int nb_parts = 0;
	char *part_type = "static";

	printk(KERN_NOTICE "Sharp SL series flash device: %x at %x\n", WINDOW_SIZE, WINDOW_ADDR);
	sharpsl_map.virt = ioremap(WINDOW_ADDR, WINDOW_SIZE);
	if (!sharpsl_map.virt) {
		printk("Failed to ioremap\n");
		return -EIO;
	}
	mymtd = do_map_probe("map_rom", &sharpsl_map);
	if (!mymtd) {
		iounmap(sharpsl_map.virt);
		return -ENXIO;
	}

	mymtd->owner = THIS_MODULE;

	parts = sharpsl_partitions;
	nb_parts = NB_OF(sharpsl_partitions);

	printk(KERN_NOTICE "Using %s partision definition\n", part_type);
	add_mtd_partitions(mymtd, parts, nb_parts);

	return 0;
}

static void __exit cleanup_sharpsl(void)
{
	if (mymtd) {
		del_mtd_partitions(mymtd);
		map_destroy(mymtd);
	}
	if (sharpsl_map.virt) {
		iounmap(sharpsl_map.virt);
		sharpsl_map.virt = 0;
	}
}

module_init(init_sharpsl);
module_exit(cleanup_sharpsl);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SHARP (Original: Arnold Christensen <AKC@pel.dk>)");
MODULE_DESCRIPTION("MTD map driver for SHARP SL series");
