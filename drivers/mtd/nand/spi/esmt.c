// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Grandstream Networks, Inc
 *
 * Authors:
 *	Carl <xjxia@grandstream.cn>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_ESMT		0xC8

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 2, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
		SPINAND_PROG_LOAD(false, 0, NULL, 0));

static int f50lxx41x_ooblayout_ecc(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 8;
	region->length = 8;

	return 0;
}

static int f50lxx41x_ooblayout_free(struct mtd_info *mtd, int section,
				    struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 2;
	region->length = 6;

	return 0;
}

static const struct mtd_ooblayout_ops f50lxx41x_ooblayout = {
	.ecc = f50lxx41x_ooblayout_ecc,
	.free = f50lxx41x_ooblayout_free,
};

static int f50l2g41ka_ooblayout_ecc(struct mtd_info *mtd, int section,
				    struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = mtd->oobsize / 2;
	region->length = mtd->oobsize / 2;

	return 0;
}

static int f50l2g41ka_ooblayout_free(struct mtd_info *mtd, int section,
				     struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = mtd->oobsize / 2 - 2;

	return 0;
}

static const struct mtd_ooblayout_ops f50l2g41ka_ooblayout = {
	.ecc = f50l2g41ka_ooblayout_ecc,
	.free = f50l2g41ka_ooblayout_free,
};

/*
 * ecc bits: 0xC0[4,6]
 * [0b000], No bit errors were detected;
 * [0b001] and [0b011], 1~6 Bit errors were detected and corrected. Not
 *	reach Flipping Bits;
 * [0b101], Bit error count equals the bit flip
 *	detection threshold
 * [0b010], Multiple bit errors were detected and
 *	not corrected.
 * others, Reserved.
 */
static int f50l2g41ka_ecc_ecc_get_status(struct spinand_device *spinand,
					 u8 status)
{
	struct nand_device *nand = spinand_to_nand(spinand);
	u8 eccsr = (status & GENMASK(6, 4)) >> 4;

	if (eccsr <= 1 || eccsr == 3)
		return eccsr;
	else if (eccsr == 5)
		return nanddev_get_ecc_requirements(nand)->strength;
	else
		return -EBADMSG;
}

static const struct spinand_info esmt_spinand_table[] = {
	SPINAND_INFO("F50L1G41LB",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x01),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&f50lxx41x_ooblayout, NULL)),
	SPINAND_INFO("F50L2G41KA",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x41, 0x7F),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&f50l2g41ka_ooblayout, f50l2g41ka_ecc_ecc_get_status)),
};

static const struct spinand_manufacturer_ops esmt_spinand_manuf_ops = {
};

const struct spinand_manufacturer esmt_spinand_manufacturer = {
	.id = SPINAND_MFR_ESMT,
	.name = "esmt",
	.chips = esmt_spinand_table,
	.nchips = ARRAY_SIZE(esmt_spinand_table),
	.ops = &esmt_spinand_manuf_ops,
};
