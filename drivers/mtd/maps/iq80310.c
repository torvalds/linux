/*
 * $Id: iq80310.c,v 1.20 2004/11/04 13:24:15 gleixner Exp $
 *
 * Mapping for the Intel XScale IQ80310 evaluation board
 *
 * Author:	Nicolas Pitre
 * Copyright:	(C) 2001 MontaVista Software Inc.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>


#define WINDOW_ADDR 	0
#define WINDOW_SIZE 	8*1024*1024
#define BUSWIDTH 	1

static struct mtd_info *mymtd;

static struct map_info iq80310_map = {
	.name = "IQ80310 flash",
	.size = WINDOW_SIZE,
	.bankwidth = BUSWIDTH,
	.phys = WINDOW_ADDR
};

static struct mtd_partition iq80310_partitions[4] = {
	{
		.name =		"Firmware",
		.size =		0x00080000,
		.offset =	0,
		.mask_flags =	MTD_WRITEABLE  /* force read-only */
	},{
		.name =		"Kernel",
		.size =		0x000a0000,
		.offset =	0x00080000,
	},{
		.name =		"Filesystem",
		.size =		0x00600000,
		.offset =	0x00120000
	},{
		.name =		"RedBoot",
		.size =		0x000e0000,
		.offset =	0x00720000,
		.mask_flags =	MTD_WRITEABLE
	}
};

static struct mtd_info *mymtd;
static struct mtd_partition *parsed_parts;
static const char *probes[] = { "RedBoot", "cmdlinepart", NULL };

static int __init init_iq80310(void)
{
	struct mtd_partition *parts;
	int nb_parts = 0;
	int parsed_nr_parts = 0;
	int ret;

	iq80310_map.virt = ioremap(WINDOW_ADDR, WINDOW_SIZE);
	if (!iq80310_map.virt) {
		printk("Failed to ioremap\n");
		return -EIO;
	}
	simple_map_init(&iq80310_map);

	mymtd = do_map_probe("cfi_probe", &iq80310_map);
	if (!mymtd) {
		iounmap((void *)iq80310_map.virt);
		return -ENXIO;
	}
	mymtd->owner = THIS_MODULE;

	ret = parse_mtd_partitions(mymtd, probes, &parsed_parts, 0);

	if (ret > 0)
		parsed_nr_parts = ret;

	if (parsed_nr_parts > 0) {
		parts = parsed_parts;
		nb_parts = parsed_nr_parts;
	} else {
		parts = iq80310_partitions;
		nb_parts = ARRAY_SIZE(iq80310_partitions);
	}
	add_mtd_partitions(mymtd, parts, nb_parts);
	return 0;
}

static void __exit cleanup_iq80310(void)
{
	if (mymtd) {
		del_mtd_partitions(mymtd);
		map_destroy(mymtd);
		if (parsed_parts)
			kfree(parsed_parts);
	}
	if (iq80310_map.virt)
		iounmap((void *)iq80310_map.virt);
}

module_init(init_iq80310);
module_exit(cleanup_iq80310);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nicolas Pitre <nico@cam.org>");
MODULE_DESCRIPTION("MTD map driver for Intel XScale IQ80310 evaluation board");
