/*
 * $Id: beech-mtd.c,v 1.11 2005/11/07 11:14:26 gleixner Exp $
 *
 * drivers/mtd/maps/beech-mtd.c MTD mappings and partition tables for
 *                              IBM 405LP Beech boards.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Copyright (C) 2002, International Business Machines Corporation
 * All Rights Reserved.
 *
 * Bishop Brock
 * IBM Research, Austin Center for Low-Power Computing
 * bcbrock@us.ibm.com
 * March 2002
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#include <asm/ibm4xx.h>

#define NAME     "Beech Linux Flash"
#define PADDR    BEECH_BIGFLASH_PADDR
#define SIZE     BEECH_BIGFLASH_SIZE
#define BUSWIDTH 1

/* Flash memories on these boards are memory resources, accessed big-endian. */


static struct map_info beech_mtd_map = {
	.name =		NAME,
	.size =		SIZE,
	.bankwidth =	BUSWIDTH,
	.phys =		PADDR
};

static struct mtd_info *beech_mtd;

static struct mtd_partition beech_partitions[2] = {
	{
	      .name = "Linux Kernel",
	      .size = BEECH_KERNEL_SIZE,
	      .offset = BEECH_KERNEL_OFFSET
	}, {
	      .name = "Free Area",
	      .size = BEECH_FREE_AREA_SIZE,
	      .offset = BEECH_FREE_AREA_OFFSET
	}
};

static int __init
init_beech_mtd(void)
{
	int err;

	printk("%s: 0x%08x at 0x%08x\n", NAME, SIZE, PADDR);

	beech_mtd_map.virt = ioremap(PADDR, SIZE);

	if (!beech_mtd_map.virt) {
		printk("%s: failed to ioremap 0x%x\n", NAME, PADDR);
		return -EIO;
	}

	simple_map_init(&beech_mtd_map);

	printk("%s: probing %d-bit flash bus\n", NAME, BUSWIDTH * 8);
	beech_mtd = do_map_probe("cfi_probe", &beech_mtd_map);

	if (!beech_mtd) {
		iounmap(beech_mtd_map.virt);
		return -ENXIO;
	}

	beech_mtd->owner = THIS_MODULE;

	err = add_mtd_partitions(beech_mtd, beech_partitions, 2);
	if (err) {
		printk("%s: add_mtd_partitions failed\n", NAME);
		iounmap(beech_mtd_map.virt);
	}

	return err;
}

static void __exit
cleanup_beech_mtd(void)
{
	if (beech_mtd) {
		del_mtd_partitions(beech_mtd);
		map_destroy(beech_mtd);
		iounmap((void *) beech_mtd_map.virt);
	}
}

module_init(init_beech_mtd);
module_exit(cleanup_beech_mtd);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bishop Brock <bcbrock@us.ibm.com>");
MODULE_DESCRIPTION("MTD map and partitions for IBM 405LP Beech boards");
