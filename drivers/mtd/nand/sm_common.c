/*
 * Copyright © 2009 - Maxim Levitsky
 * Common routines & support for xD format
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/mtd/nand.h>
#include "sm_common.h"

static struct nand_ecclayout nand_oob_sm = {
	.eccbytes = 6,
	.eccpos = {8, 9, 10, 13, 14, 15},
	.oobfree = {
		{.offset = 0 , .length = 4}, /* reserved */
		{.offset = 6 , .length = 2}, /* LBA1 */
		{.offset = 11, .length = 2}  /* LBA2 */
	}
};

/* NOTE: This layout is is not compatabable with SmartMedia, */
/* because the 256 byte devices have page depenent oob layout */
/* However it does preserve the bad block markers */
/* If you use smftl, it will bypass this and work correctly */
/* If you not, then you break SmartMedia compliance anyway */

static struct nand_ecclayout nand_oob_sm_small = {
	.eccbytes = 3,
	.eccpos = {0, 1, 2},
	.oobfree = {
		{.offset = 3 , .length = 2}, /* reserved */
		{.offset = 6 , .length = 2}, /* LBA1 */
	}
};


static int sm_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	struct mtd_oob_ops ops;
	struct sm_oob oob;
	int ret, error = 0;

	memset(&oob, -1, SM_OOB_SIZE);
	oob.block_status = 0x0F;

	/* As long as this function is called on erase block boundaries
		it will work correctly for 256 byte nand */
	ops.mode = MTD_OOB_PLACE;
	ops.ooboffs = 0;
	ops.ooblen = mtd->oobsize;
	ops.oobbuf = (void *)&oob;
	ops.datbuf = NULL;


	ret = mtd->write_oob(mtd, ofs, &ops);
	if (ret < 0 || ops.oobretlen != SM_OOB_SIZE) {
		printk(KERN_NOTICE
			"sm_common: can't mark sector at %i as bad\n",
								(int)ofs);
		error = -EIO;
	} else
		mtd->ecc_stats.badblocks++;

	return error;
}


int sm_register_device(struct mtd_info *mtd)
{
	struct nand_chip *chip = (struct nand_chip *)mtd->priv;
	int ret;

	chip->options |= NAND_SKIP_BBTSCAN | NAND_SMARTMEDIA;

	/* Scan for card properties */
	ret = nand_scan_ident(mtd, 1, NULL);

	if (ret)
		return ret;

	/* Bad block marker postion */
	chip->badblockpos = 0x05;
	chip->badblockbits = 7;
	chip->block_markbad = sm_block_markbad;

	/* ECC layout */
	if (mtd->writesize == SM_SECTOR_SIZE)
		chip->ecc.layout = &nand_oob_sm;
	else if (mtd->writesize == SM_SMALL_PAGE)
		chip->ecc.layout = &nand_oob_sm_small;
	else
		return -ENODEV;

	ret = nand_scan_tail(mtd);

	if (ret)
		return ret;

	return add_mtd_device(mtd);
}
EXPORT_SYMBOL_GPL(sm_register_device);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Maxim Levitsky <maximlevitsky@gmail.com>");
MODULE_DESCRIPTION("Common SmartMedia/xD functions");
