/****************************************************************************/

/*
 *	uclinux.c -- generic memory mapped MTD driver for uclinux
 *
 *	(C) Copyright 2002, Greg Ungerer (gerg@snapgear.com)
 *
 * 	$Id: uclinux.c,v 1.10 2005/01/05 18:05:13 dwmw2 Exp $
 */

/****************************************************************************/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/root_dev.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>

/****************************************************************************/


/****************************************************************************/

struct map_info uclinux_ram_map = {
	.name = "RAM",
};

struct mtd_info *uclinux_ram_mtdinfo;

/****************************************************************************/

struct mtd_partition uclinux_romfs[] = {
	{ .name = "ROMfs" }
};

#define	NUM_PARTITIONS	(sizeof(uclinux_romfs) / sizeof(uclinux_romfs[0]))

/****************************************************************************/

int uclinux_point(struct mtd_info *mtd, loff_t from, size_t len,
	size_t *retlen, u_char **mtdbuf)
{
	struct map_info *map = mtd->priv;
	*mtdbuf = (u_char *) (map->virt + ((int) from));
	*retlen = len;
	return(0);
}

/****************************************************************************/

int __init uclinux_mtd_init(void)
{
	struct mtd_info *mtd;
	struct map_info *mapp;
	extern char _ebss;

	mapp = &uclinux_ram_map;
	mapp->phys = (unsigned long) &_ebss;
	mapp->size = PAGE_ALIGN(*((unsigned long *)((&_ebss) + 8)));
	mapp->bankwidth = 4;

	printk("uclinux[mtd]: RAM probe address=0x%x size=0x%x\n",
	       	(int) mapp->map_priv_2, (int) mapp->size);

	mapp->virt = ioremap_nocache(mapp->phys, mapp->size);

	if (mapp->virt == 0) {
		printk("uclinux[mtd]: ioremap_nocache() failed\n");
		return(-EIO);
	}

	simple_map_init(mapp);

	mtd = do_map_probe("map_ram", mapp);
	if (!mtd) {
		printk("uclinux[mtd]: failed to find a mapping?\n");
		iounmap(mapp->virt);
		return(-ENXIO);
	}
		
	mtd->owner = THIS_MODULE;
	mtd->point = uclinux_point;
	mtd->priv = mapp;

	uclinux_ram_mtdinfo = mtd;
	add_mtd_partitions(mtd, uclinux_romfs, NUM_PARTITIONS);

	printk("uclinux[mtd]: set %s to be root filesystem\n",
	     	uclinux_romfs[0].name);
	ROOT_DEV = MKDEV(MTD_BLOCK_MAJOR, 0);
	put_mtd_device(mtd);

	return(0);
}

/****************************************************************************/

void __exit uclinux_mtd_cleanup(void)
{
	if (uclinux_ram_mtdinfo) {
		del_mtd_partitions(uclinux_ram_mtdinfo);
		map_destroy(uclinux_ram_mtdinfo);
		uclinux_ram_mtdinfo = NULL;
	}
	if (uclinux_ram_map.map_priv_1) {
		iounmap((void *) uclinux_ram_map.virt);
		uclinux_ram_map.virt = 0;
	}
}

/****************************************************************************/

module_init(uclinux_mtd_init);
module_exit(uclinux_mtd_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Ungerer <gerg@snapgear.com>");
MODULE_DESCRIPTION("Generic RAM based MTD for uClinux");

/****************************************************************************/
