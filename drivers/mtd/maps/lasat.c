/*
 * Flash device on Lasat 100 and 200 boards
 *
 * (C) 2002 Brian Murphy <brian@murphy.dk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * $Id: lasat.c,v 1.9 2004/11/04 13:24:15 gleixner Exp $
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/config.h>
#include <asm/lasat/lasat.h>

static struct mtd_info *lasat_mtd;

static struct mtd_partition partition_info[LASAT_MTD_LAST];
static char *lasat_mtd_partnames[] = {"Bootloader", "Service", "Normal", "Filesystem", "Config"};

static void lasat_set_vpp(struct map_info *map, int vpp)
{
	if (vpp)
	    *lasat_misc->flash_wp_reg |= 1 << lasat_misc->flash_wp_bit;
	else
	    *lasat_misc->flash_wp_reg &= ~(1 << lasat_misc->flash_wp_bit);
}

static struct map_info lasat_map = {
	.name = "LASAT flash",
	.bankwidth = 4,
	.set_vpp = lasat_set_vpp
};

static int __init init_lasat(void)
{
	int i;
	/* since we use AMD chips and set_vpp is not implimented
	 * for these (yet) we still have to permanently enable flash write */
	printk(KERN_NOTICE "Unprotecting flash\n");
	ENABLE_VPP((&lasat_map));

	lasat_map.phys = lasat_flash_partition_start(LASAT_MTD_BOOTLOADER);
	lasat_map.virt = ioremap_nocache(
		        lasat_map.phys, lasat_board_info.li_flash_size);
	lasat_map.size = lasat_board_info.li_flash_size;

	simple_map_init(&lasat_map);

	for (i=0; i < LASAT_MTD_LAST; i++)
		partition_info[i].name = lasat_mtd_partnames[i];

	lasat_mtd = do_map_probe("cfi_probe", &lasat_map);

	if (!lasat_mtd)
	    lasat_mtd = do_map_probe("jedec_probe", &lasat_map);

	if (lasat_mtd) {
		u32 size, offset = 0;

		lasat_mtd->owner = THIS_MODULE;

		for (i=0; i < LASAT_MTD_LAST; i++) {
			size = lasat_flash_partition_size(i);
			partition_info[i].size = size;
			partition_info[i].offset = offset;
			offset += size;
		}

		add_mtd_partitions( lasat_mtd, partition_info, LASAT_MTD_LAST );
		return 0;
	}

	return -ENXIO;
}

static void __exit cleanup_lasat(void)
{
	if (lasat_mtd) {
		del_mtd_partitions(lasat_mtd);
		map_destroy(lasat_mtd);
	}
	if (lasat_map.virt) {
		lasat_map.virt = 0;
	}
}

module_init(init_lasat);
module_exit(cleanup_lasat);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Brian Murphy <brian@murphy.dk>");
MODULE_DESCRIPTION("Lasat Safepipe/Masquerade MTD map driver");
