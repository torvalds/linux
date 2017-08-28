/*
 * Handle mapping of the NOR flash on implementa A7 boards
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
#include <linux/mtd/partitions.h>

#define WINDOW_ADDR0 0x00000000      /* physical properties of flash */
#define WINDOW_SIZE0 0x00800000
#define WINDOW_ADDR1 0x10000000      /* physical properties of flash */
#define WINDOW_SIZE1 0x00800000
#define NUM_FLASHBANKS 2
#define BUSWIDTH     4

#define MSG_PREFIX "impA7:"   /* prefix for our printk()'s */
#define MTDID      "impa7-%d"  /* for mtdparts= partitioning */

static struct mtd_info *impa7_mtd[NUM_FLASHBANKS];

static const char * const rom_probe_types[] = { "jedec_probe", NULL };

static struct map_info impa7_map[NUM_FLASHBANKS] = {
	{
		.name = "impA7 NOR Flash Bank #0",
		.size = WINDOW_SIZE0,
		.bankwidth = BUSWIDTH,
	},
	{
		.name = "impA7 NOR Flash Bank #1",
		.size = WINDOW_SIZE1,
		.bankwidth = BUSWIDTH,
	},
};

/*
 * MTD partitioning stuff
 */
static const struct mtd_partition partitions[] =
{
	{
		.name = "FileSystem",
		.size = 0x800000,
		.offset = 0x00000000
	},
};

static int __init init_impa7(void)
{
	const char * const *type;
	int i;
	static struct { u_long addr; u_long size; } pt[NUM_FLASHBANKS] = {
	  { WINDOW_ADDR0, WINDOW_SIZE0 },
	  { WINDOW_ADDR1, WINDOW_SIZE1 },
        };
	int devicesfound = 0;

	for(i=0; i<NUM_FLASHBANKS; i++)
	{
		printk(KERN_NOTICE MSG_PREFIX "probing 0x%08lx at 0x%08lx\n",
		       pt[i].size, pt[i].addr);

		impa7_map[i].phys = pt[i].addr;
		impa7_map[i].virt = ioremap(pt[i].addr, pt[i].size);
		if (!impa7_map[i].virt) {
			printk(MSG_PREFIX "failed to ioremap\n");
			return -EIO;
		}
		simple_map_init(&impa7_map[i]);

		impa7_mtd[i] = NULL;
		type = rom_probe_types;
		for(; !impa7_mtd[i] && *type; type++) {
			impa7_mtd[i] = do_map_probe(*type, &impa7_map[i]);
		}

		if (impa7_mtd[i]) {
			impa7_mtd[i]->owner = THIS_MODULE;
			devicesfound++;
			mtd_device_parse_register(impa7_mtd[i], NULL, NULL,
						  partitions,
						  ARRAY_SIZE(partitions));
		} else {
			iounmap((void __iomem *)impa7_map[i].virt);
		}
	}
	return devicesfound == 0 ? -ENXIO : 0;
}

static void __exit cleanup_impa7(void)
{
	int i;
	for (i=0; i<NUM_FLASHBANKS; i++) {
		if (impa7_mtd[i]) {
			mtd_device_unregister(impa7_mtd[i]);
			map_destroy(impa7_mtd[i]);
			iounmap((void __iomem *)impa7_map[i].virt);
			impa7_map[i].virt = NULL;
		}
	}
}

module_init(init_impa7);
module_exit(cleanup_impa7);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pavel Bartusek <pba@sysgo.de>");
MODULE_DESCRIPTION("MTD map driver for implementa impA7");
