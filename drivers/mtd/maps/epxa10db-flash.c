/*
 * Flash memory access on EPXA based devices
 *
 * (C) 2000 Nicolas Pitre <nico@cam.org>
 *  Copyright (C) 2001 Altera Corporation
 *  Copyright (C) 2001 Red Hat, Inc.
 *
 * $Id: epxa10db-flash.c,v 1.15 2005/11/07 11:14:27 gleixner Exp $
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#include <asm/hardware.h>

#ifdef CONFIG_EPXA10DB
#define BOARD_NAME "EPXA10DB"
#else
#define BOARD_NAME "EPXA1DB"
#endif

static int nr_parts = 0;
static struct mtd_partition *parts;

static struct mtd_info *mymtd;

static int epxa_default_partitions(struct mtd_info *master, struct mtd_partition **pparts);


static struct map_info epxa_map = {
	.name =		"EPXA flash",
	.size =		FLASH_SIZE,
	.bankwidth =	2,
	.phys =		FLASH_START,
};

static const char *probes[] = { "RedBoot", "afs", NULL };

static int __init epxa_mtd_init(void)
{
	int i;

	printk(KERN_NOTICE "%s flash device: 0x%x at 0x%x\n", BOARD_NAME, FLASH_SIZE, FLASH_START);

	epxa_map.virt = ioremap(FLASH_START, FLASH_SIZE);
	if (!epxa_map.virt) {
		printk("Failed to ioremap %s flash\n",BOARD_NAME);
		return -EIO;
	}
	simple_map_init(&epxa_map);

	mymtd = do_map_probe("cfi_probe", &epxa_map);
	if (!mymtd) {
		iounmap((void *)epxa_map.virt);
		return -ENXIO;
	}

	mymtd->owner = THIS_MODULE;

	/* Unlock the flash device. */
	if(mymtd->unlock){
		for (i=0; i<mymtd->numeraseregions;i++){
			int j;
			for(j=0;j<mymtd->eraseregions[i].numblocks;j++){
				mymtd->unlock(mymtd,mymtd->eraseregions[i].offset + j * mymtd->eraseregions[i].erasesize,mymtd->eraseregions[i].erasesize);
			}
		}
	}

#ifdef CONFIG_MTD_PARTITIONS
	nr_parts = parse_mtd_partitions(mymtd, probes, &parts, 0);

	if (nr_parts > 0) {
		add_mtd_partitions(mymtd, parts, nr_parts);
		return 0;
	}
#endif
	/* No recognised partitioning schemes found - use defaults */
	nr_parts = epxa_default_partitions(mymtd, &parts);
	if (nr_parts > 0) {
		add_mtd_partitions(mymtd, parts, nr_parts);
		return 0;
	}

	/* If all else fails... */
	add_mtd_device(mymtd);
	return 0;
}

static void __exit epxa_mtd_cleanup(void)
{
	if (mymtd) {
		if (nr_parts)
			del_mtd_partitions(mymtd);
		else
			del_mtd_device(mymtd);
		map_destroy(mymtd);
	}
	if (epxa_map.virt) {
		iounmap((void *)epxa_map.virt);
		epxa_map.virt = 0;
	}
}


/*
 * This will do for now, once we decide which bootldr we're finally
 * going to use then we'll remove this function and do it properly
 *
 * Partions are currently (as offsets from base of flash):
 * 0x00000000 - 0x003FFFFF - bootloader (!)
 * 0x00400000 - 0x00FFFFFF - Flashdisk
 */

static int __init epxa_default_partitions(struct mtd_info *master, struct mtd_partition **pparts)
{
	struct mtd_partition *parts;
	int ret, i;
	int npartitions = 0;
	char *names;
	const char *name = "jffs";

	printk("Using default partitions for %s\n",BOARD_NAME);
	npartitions=1;
	parts = kmalloc(npartitions*sizeof(*parts)+strlen(name), GFP_KERNEL);
	memzero(parts,npartitions*sizeof(*parts)+strlen(name));
	if (!parts) {
		ret = -ENOMEM;
		goto out;
	}
	i=0;
	names = (char *)&parts[npartitions];
	parts[i].name = names;
	names += strlen(name) + 1;
	strcpy(parts[i].name, name);

#ifdef CONFIG_EPXA10DB
	parts[i].size = FLASH_SIZE-0x00400000;
	parts[i].offset = 0x00400000;
#else
	parts[i].size = FLASH_SIZE-0x00180000;
	parts[i].offset = 0x00180000;
#endif

 out:
	*pparts = parts;
	return npartitions;
}


module_init(epxa_mtd_init);
module_exit(epxa_mtd_cleanup);

MODULE_AUTHOR("Clive Davies");
MODULE_DESCRIPTION("Altera epxa mtd flash map");
MODULE_LICENSE("GPL");
