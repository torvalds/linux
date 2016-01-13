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
#include <linux/module.h>
#include <linux/sizes.h>
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
	int ret;

	memset(&oob, -1, SM_OOB_SIZE);
	oob.block_status = 0x0F;

	/* As long as this function is called on erase block boundaries
		it will work correctly for 256 byte nand */
	ops.mode = MTD_OPS_PLACE_OOB;
	ops.ooboffs = 0;
	ops.ooblen = mtd->oobsize;
	ops.oobbuf = (void *)&oob;
	ops.datbuf = NULL;


	ret = mtd_write_oob(mtd, ofs, &ops);
	if (ret < 0 || ops.oobretlen != SM_OOB_SIZE) {
		printk(KERN_NOTICE
			"sm_common: can't mark sector at %i as bad\n",
								(int)ofs);
		return -EIO;
	}

	return 0;
}

static struct nand_flash_dev nand_smartmedia_flash_ids[] = {
	LEGACY_ID_NAND("SmartMedia 2MiB 3,3V ROM",   0x5d, 2,   SZ_8K, NAND_ROM),
	LEGACY_ID_NAND("SmartMedia 4MiB 3,3V",       0xe3, 4,   SZ_8K, 0),
	LEGACY_ID_NAND("SmartMedia 4MiB 3,3/5V",     0xe5, 4,   SZ_8K, 0),
	LEGACY_ID_NAND("SmartMedia 4MiB 5V",         0x6b, 4,   SZ_8K, 0),
	LEGACY_ID_NAND("SmartMedia 4MiB 3,3V ROM",   0xd5, 4,   SZ_8K, NAND_ROM),
	LEGACY_ID_NAND("SmartMedia 8MiB 3,3V",       0xe6, 8,   SZ_8K, 0),
	LEGACY_ID_NAND("SmartMedia 8MiB 3,3V ROM",   0xd6, 8,   SZ_8K, NAND_ROM),
	LEGACY_ID_NAND("SmartMedia 16MiB 3,3V",      0x73, 16,  SZ_16K, 0),
	LEGACY_ID_NAND("SmartMedia 16MiB 3,3V ROM",  0x57, 16,  SZ_16K, NAND_ROM),
	LEGACY_ID_NAND("SmartMedia 32MiB 3,3V",      0x75, 32,  SZ_16K, 0),
	LEGACY_ID_NAND("SmartMedia 32MiB 3,3V ROM",  0x58, 32,  SZ_16K, NAND_ROM),
	LEGACY_ID_NAND("SmartMedia 64MiB 3,3V",      0x76, 64,  SZ_16K, 0),
	LEGACY_ID_NAND("SmartMedia 64MiB 3,3V ROM",  0xd9, 64,  SZ_16K, NAND_ROM),
	LEGACY_ID_NAND("SmartMedia 128MiB 3,3V",     0x79, 128, SZ_16K, 0),
	LEGACY_ID_NAND("SmartMedia 128MiB 3,3V ROM", 0xda, 128, SZ_16K, NAND_ROM),
	LEGACY_ID_NAND("SmartMedia 256MiB 3, 3V",    0x71, 256, SZ_16K, 0),
	LEGACY_ID_NAND("SmartMedia 256MiB 3,3V ROM", 0x5b, 256, SZ_16K, NAND_ROM),
	{NULL}
};

static struct nand_flash_dev nand_xd_flash_ids[] = {
	LEGACY_ID_NAND("xD 16MiB 3,3V",  0x73, 16,   SZ_16K, 0),
	LEGACY_ID_NAND("xD 32MiB 3,3V",  0x75, 32,   SZ_16K, 0),
	LEGACY_ID_NAND("xD 64MiB 3,3V",  0x76, 64,   SZ_16K, 0),
	LEGACY_ID_NAND("xD 128MiB 3,3V", 0x79, 128,  SZ_16K, 0),
	LEGACY_ID_NAND("xD 256MiB 3,3V", 0x71, 256,  SZ_16K, NAND_BROKEN_XD),
	LEGACY_ID_NAND("xD 512MiB 3,3V", 0xdc, 512,  SZ_16K, NAND_BROKEN_XD),
	LEGACY_ID_NAND("xD 1GiB 3,3V",   0xd3, 1024, SZ_16K, NAND_BROKEN_XD),
	LEGACY_ID_NAND("xD 2GiB 3,3V",   0xd5, 2048, SZ_16K, NAND_BROKEN_XD),
	{NULL}
};

int sm_register_device(struct mtd_info *mtd, int smartmedia)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	int ret;

	chip->options |= NAND_SKIP_BBTSCAN;

	/* Scan for card properties */
	ret = nand_scan_ident(mtd, 1, smartmedia ?
		nand_smartmedia_flash_ids : nand_xd_flash_ids);

	if (ret)
		return ret;

	/* Bad block marker position */
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

	return mtd_device_register(mtd, NULL, 0);
}
EXPORT_SYMBOL_GPL(sm_register_device);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Maxim Levitsky <maximlevitsky@gmail.com>");
MODULE_DESCRIPTION("Common SmartMedia/xD functions");
