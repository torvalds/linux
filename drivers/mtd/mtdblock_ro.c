// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Simple read-only (writable only for RAM) mtdblock driver
 *
 * Copyright © 2001-2010 David Woodhouse <dwmw2@infradead.org>
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/blktrans.h>
#include <linux/module.h>
#include <linux/major.h>

static int mtdblock_readsect(struct mtd_blktrans_dev *dev,
			      unsigned long block, char *buf)
{
	size_t retlen;

	if (mtd_read(dev->mtd, (block * 512), 512, &retlen, buf))
		return 1;
	return 0;
}

static int mtdblock_writesect(struct mtd_blktrans_dev *dev,
			      unsigned long block, char *buf)
{
	size_t retlen;

	if (mtd_write(dev->mtd, (block * 512), 512, &retlen, buf))
		return 1;
	return 0;
}

static void mtdblock_add_mtd(struct mtd_blktrans_ops *tr, struct mtd_info *mtd)
{
	struct mtd_blktrans_dev *dev = kzalloc(sizeof(*dev), GFP_KERNEL);

	if (!dev)
		return;

	dev->mtd = mtd;
	dev->devnum = mtd->index;

	dev->size = mtd->size >> 9;
	dev->tr = tr;
	dev->readonly = 1;

	if (mtd_type_is_nand(mtd))
		pr_warn("%s: MTD device '%s' is NAND, please consider using UBI block devices instead.\n",
			tr->name, mtd->name);

	if (add_mtd_blktrans_dev(dev))
		kfree(dev);
}

static void mtdblock_remove_dev(struct mtd_blktrans_dev *dev)
{
	del_mtd_blktrans_dev(dev);
}

static struct mtd_blktrans_ops mtdblock_tr = {
	.name		= "mtdblock",
	.major		= MTD_BLOCK_MAJOR,
	.part_bits	= 0,
	.blksize 	= 512,
	.readsect	= mtdblock_readsect,
	.writesect	= mtdblock_writesect,
	.add_mtd	= mtdblock_add_mtd,
	.remove_dev	= mtdblock_remove_dev,
	.owner		= THIS_MODULE,
};

module_mtd_blktrans(mtdblock_tr);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("Simple read-only block device emulation access to MTD devices");
