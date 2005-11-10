/*
 * $Id:  $
 *
 * Map driver for the Mainstone developer platform.
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
#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/mainstone.h>


#define ROM_ADDR	0x00000000
#define FLASH_ADDR	0x04000000

#define WINDOW_SIZE 	0x04000000

static void mainstone_map_inval_cache(struct map_info *map, unsigned long from,
				      ssize_t len)
{
	consistent_sync((char *)map->cached + from, len, DMA_FROM_DEVICE);
}

static struct map_info mainstone_maps[2] = { {
	.size =		WINDOW_SIZE,
	.phys =		PXA_CS0_PHYS,
	.inval_cache = 	mainstone_map_inval_cache,
}, {
	.size =		WINDOW_SIZE,
	.phys =		PXA_CS1_PHYS,
	.inval_cache = 	mainstone_map_inval_cache,
} };

static struct mtd_partition mainstone_partitions[] = {
	{
		.name =		"Bootloader",
		.size =		0x00040000,
		.offset =	0,
		.mask_flags =	MTD_WRITEABLE  /* force read-only */
	},{
		.name =		"Kernel",
		.size =		0x00400000,
		.offset =	0x00040000,
	},{
		.name =		"Filesystem",
		.size =		MTDPART_SIZ_FULL,
		.offset =	0x00440000
	}
};

static struct mtd_info *mymtds[2];
static struct mtd_partition *parsed_parts[2];
static int nr_parsed_parts[2];

static const char *probes[] = { "RedBoot", "cmdlinepart", NULL };

static int __init init_mainstone(void)
{
	int SW7 = 0;  /* FIXME: get from SCR (Mst doc section 3.2.1.1) */
	int ret = 0, i;

	mainstone_maps[0].bankwidth = (BOOT_DEF & 1) ? 2 : 4;
	mainstone_maps[1].bankwidth = 4;

	/* Compensate for SW7 which swaps the flash banks */
	mainstone_maps[SW7].name = "processor flash";
	mainstone_maps[SW7 ^ 1].name = "main board flash";

	printk(KERN_NOTICE "Mainstone configured to boot from %s\n",
	       mainstone_maps[0].name);

	for (i = 0; i < 2; i++) {
		mainstone_maps[i].virt = ioremap(mainstone_maps[i].phys,
						 WINDOW_SIZE);
		if (!mainstone_maps[i].virt) {
			printk(KERN_WARNING "Failed to ioremap %s\n",
			       mainstone_maps[i].name);
			if (!ret)
				ret = -ENOMEM;
			continue;
		}
		mainstone_maps[i].cached =
			ioremap_cached(mainstone_maps[i].phys, WINDOW_SIZE);
		if (!mainstone_maps[i].cached)
			printk(KERN_WARNING "Failed to ioremap cached %s\n",
			       mainstone_maps[i].name);
		simple_map_init(&mainstone_maps[i]);

		printk(KERN_NOTICE
		       "Probing %s at physical address 0x%08lx"
		       " (%d-bit bankwidth)\n",
		       mainstone_maps[i].name, mainstone_maps[i].phys,
		       mainstone_maps[i].bankwidth * 8);

		mymtds[i] = do_map_probe("cfi_probe", &mainstone_maps[i]);

		if (!mymtds[i]) {
			iounmap((void *)mainstone_maps[i].virt);
			if (mainstone_maps[i].cached)
				iounmap(mainstone_maps[i].cached);
			if (!ret)
				ret = -EIO;
			continue;
		}
		mymtds[i]->owner = THIS_MODULE;

		ret = parse_mtd_partitions(mymtds[i], probes,
					   &parsed_parts[i], 0);

		if (ret > 0)
			nr_parsed_parts[i] = ret;
	}

	if (!mymtds[0] && !mymtds[1])
		return ret;

	for (i = 0; i < 2; i++) {
		if (!mymtds[i]) {
			printk(KERN_WARNING "%s is absent. Skipping\n",
			       mainstone_maps[i].name);
		} else if (nr_parsed_parts[i]) {
			add_mtd_partitions(mymtds[i], parsed_parts[i],
					   nr_parsed_parts[i]);
		} else if (!i) {
			printk("Using static partitions on %s\n",
			       mainstone_maps[i].name);
			add_mtd_partitions(mymtds[i], mainstone_partitions,
					   ARRAY_SIZE(mainstone_partitions));
		} else {
			printk("Registering %s as whole device\n",
			       mainstone_maps[i].name);
			add_mtd_device(mymtds[i]);
		}
	}
	return 0;
}

static void __exit cleanup_mainstone(void)
{
	int i;
	for (i = 0; i < 2; i++) {
		if (!mymtds[i])
			continue;

		if (nr_parsed_parts[i] || !i)
			del_mtd_partitions(mymtds[i]);
		else
			del_mtd_device(mymtds[i]);

		map_destroy(mymtds[i]);
		iounmap((void *)mainstone_maps[i].virt);
		if (mainstone_maps[i].cached)
			iounmap(mainstone_maps[i].cached);
		kfree(parsed_parts[i]);
	}
}

module_init(init_mainstone);
module_exit(cleanup_mainstone);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nicolas Pitre <nico@cam.org>");
MODULE_DESCRIPTION("MTD map driver for Intel Mainstone");
