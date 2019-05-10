/*
 * Copyright © 2009 - Maxim Levitsky
 * Common routines & support for xD format
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/mtd/rawnand.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include "sm_common.h"

static int oob_sm_ooblayout_ecc(struct mtd_info *mtd, int section,
				struct mtd_oob_region *oobregion)
{
	if (section > 1)
		return -ERANGE;

	oobregion->length = 3;
	oobregion->offset = ((section + 1) * 8) - 3;

	return 0;
}

static int oob_sm_ooblayout_free(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *oobregion)
{
	switch (section) {
	case 0:
		/* reserved */
		oobregion->offset = 0;
		oobregion->length = 4;
		break;
	case 1:
		/* LBA1 */
		oobregion->offset = 6;
		oobregion->length = 2;
		break;
	case 2:
		/* LBA2 */
		oobregion->offset = 11;
		oobregion->length = 2;
		break;
	default:
		return -ERANGE;
	}

	return 0;
}

static const struct mtd_ooblayout_ops oob_sm_ops = {
	.ecc = oob_sm_ooblayout_ecc,
	.free = oob_sm_ooblayout_free,
};

/* NOTE: This layout is is not compatabable with SmartMedia, */
/* because the 256 byte devices have page depenent oob layout */
/* However it does preserve the bad block markers */
/* If you use smftl, it will bypass this and work correctly */
/* If you not, then you break SmartMedia compliance anyway */

static int oob_sm_small_ooblayout_ecc(struct mtd_info *mtd, int section,
				      struct mtd_oob_region *oobregion)
{
	if (section)
		return -ERANGE;

	oobregion->length = 3;
	oobregion->offset = 0;

	return 0;
}

static int oob_sm_small_ooblayout_free(struct mtd_info *mtd, int section,
				       struct mtd_oob_region *oobregion)
{
	switch (section) {
	case 0:
		/* reserved */
		oobregion->offset = 3;
		oobregion->length = 2;
		break;
	case 1:
		/* LBA1 */
		oobregion->offset = 6;
		oobregion->length = 2;
		break;
	default:
		return -ERANGE;
	}

	return 0;
}

static const struct mtd_ooblayout_ops oob_sm_small_ops = {
	.ecc = oob_sm_small_ooblayout_ecc,
	.free = oob_sm_small_ooblayout_free,
};

static int sm_block_markbad(struct nand_chip *chip, loff_t ofs)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
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
		pr_notice("sm_common: can't mark sector at %i as bad\n",
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

static int sm_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	/* Bad block marker position */
	chip->badblockpos = 0x05;
	chip->badblockbits = 7;
	chip->legacy.block_markbad = sm_block_markbad;

	/* ECC layout */
	if (mtd->writesize == SM_SECTOR_SIZE)
		mtd_set_ooblayout(mtd, &oob_sm_ops);
	else if (mtd->writesize == SM_SMALL_PAGE)
		mtd_set_ooblayout(mtd, &oob_sm_small_ops);
	else
		return -ENODEV;

	return 0;
}

static const struct nand_controller_ops sm_controller_ops = {
	.attach_chip = sm_attach_chip,
};

int sm_register_device(struct mtd_info *mtd, int smartmedia)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct nand_flash_dev *flash_ids;
	int ret;

	chip->options |= NAND_SKIP_BBTSCAN;

	/* Scan for card properties */
	chip->legacy.dummy_controller.ops = &sm_controller_ops;
	flash_ids = smartmedia ? nand_smartmedia_flash_ids : nand_xd_flash_ids;
	ret = nand_scan_with_ids(chip, 1, flash_ids);
	if (ret)
		return ret;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret)
		nand_cleanup(chip);

	return ret;
}
EXPORT_SYMBOL_GPL(sm_register_device);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Maxim Levitsky <maximlevitsky@gmail.com>");
MODULE_DESCRIPTION("Common SmartMedia/xD functions");
