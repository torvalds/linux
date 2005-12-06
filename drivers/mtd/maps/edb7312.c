/*
 * $Id: edb7312.c,v 1.14 2005/11/07 11:14:27 gleixner Exp $
 *
 * Handle mapping of the NOR flash on Cogent EDB7312 boards
 *
 * Copyright 2002 SYSGO Real-Time Solutions GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/config.h>

#ifdef CONFIG_MTD_PARTITIONS
#include <linux/mtd/partitions.h>
#endif

#define WINDOW_ADDR 0x00000000      /* physical properties of flash */
#define WINDOW_SIZE 0x01000000
#define BUSWIDTH    2
#define FLASH_BLOCKSIZE_MAIN	0x20000
#define FLASH_NUMBLOCKS_MAIN	128
/* can be "cfi_probe", "jedec_probe", "map_rom", NULL }; */
#define PROBETYPES { "cfi_probe", NULL }

#define MSG_PREFIX "EDB7312-NOR:"   /* prefix for our printk()'s */
#define MTDID      "edb7312-nor"    /* for mtdparts= partitioning */

static struct mtd_info *mymtd;

struct map_info edb7312nor_map = {
	.name = "NOR flash on EDB7312",
	.size = WINDOW_SIZE,
	.bankwidth = BUSWIDTH,
	.phys = WINDOW_ADDR,
};

#ifdef CONFIG_MTD_PARTITIONS

/*
 * MTD partitioning stuff
 */
static struct mtd_partition static_partitions[3] =
{
	{
		.name = "ARMboot",
		.size = 0x40000,
		.offset = 0
	},
	{
		.name = "Kernel",
		.size = 0x200000,
		.offset = 0x40000
	},
	{
		.name = "RootFS",
		.size = 0xDC0000,
		.offset = 0x240000
	},
};

static const char *probes[] = { "RedBoot", "cmdlinepart", NULL };

#endif

static int                   mtd_parts_nb = 0;
static struct mtd_partition *mtd_parts    = 0;

int __init init_edb7312nor(void)
{
	static const char *rom_probe_types[] = PROBETYPES;
	const char **type;
	const char *part_type = 0;

       	printk(KERN_NOTICE MSG_PREFIX "0x%08x at 0x%08x\n",
	       WINDOW_SIZE, WINDOW_ADDR);
	edb7312nor_map.virt = ioremap(WINDOW_ADDR, WINDOW_SIZE);

	if (!edb7312nor_map.virt) {
		printk(MSG_PREFIX "failed to ioremap\n");
		return -EIO;
	}

	simple_map_init(&edb7312nor_map);

	mymtd = 0;
	type = rom_probe_types;
	for(; !mymtd && *type; type++) {
		mymtd = do_map_probe(*type, &edb7312nor_map);
	}
	if (mymtd) {
		mymtd->owner = THIS_MODULE;

#ifdef CONFIG_MTD_PARTITIONS
		mtd_parts_nb = parse_mtd_partitions(mymtd, probes, &mtd_parts, MTDID);
		if (mtd_parts_nb > 0)
		  part_type = "detected";

		if (mtd_parts_nb == 0)
		{
			mtd_parts = static_partitions;
			mtd_parts_nb = ARRAY_SIZE(static_partitions);
			part_type = "static";
		}
#endif
		add_mtd_device(mymtd);
		if (mtd_parts_nb == 0)
		  printk(KERN_NOTICE MSG_PREFIX "no partition info available\n");
		else
		{
			printk(KERN_NOTICE MSG_PREFIX
			       "using %s partition definition\n", part_type);
			add_mtd_partitions(mymtd, mtd_parts, mtd_parts_nb);
		}
		return 0;
	}

	iounmap((void *)edb7312nor_map.virt);
	return -ENXIO;
}

static void __exit cleanup_edb7312nor(void)
{
	if (mymtd) {
		del_mtd_device(mymtd);
		map_destroy(mymtd);
	}
	if (edb7312nor_map.virt) {
		iounmap((void *)edb7312nor_map.virt);
		edb7312nor_map.virt = 0;
	}
}

module_init(init_edb7312nor);
module_exit(cleanup_edb7312nor);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marius Groeger <mag@sysgo.de>");
MODULE_DESCRIPTION("Generic configurable MTD map driver");
