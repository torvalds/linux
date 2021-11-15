// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co., Ltd
 *
 * Authors:
 *	Dingqiang Lin <jon.lin@rock-chips.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_XTX			0x0B

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
		SPINAND_PROG_LOAD(false, 0, NULL, 0));

static int xt26g0xa_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 48;
	region->length = 16;

	return 0;
}

static int xt26g0xa_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = mtd->oobsize - 18;

	return 0;
}

static const struct mtd_ooblayout_ops xt26g0xa_ooblayout = {
	.ecc = xt26g0xa_ooblayout_ecc,
	.free = xt26g0xa_ooblayout_free,
};

static int xt26g01b_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	return -ERANGE;
}

static int xt26g01b_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = mtd->oobsize - 2;

	return 0;
}

static const struct mtd_ooblayout_ops xt26g01b_ooblayout = {
	.ecc = xt26g01b_ooblayout_ecc,
	.free = xt26g01b_ooblayout_free,
};

static int xt26g02b_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 8;
	region->length = 8;

	return 0;
}

static int xt26g02b_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 2;
	region->length = 6;

	return 0;
}

static const struct mtd_ooblayout_ops xt26g02b_ooblayout = {
	.ecc = xt26g02b_ooblayout_ecc,
	.free = xt26g02b_ooblayout_free,
};

static int xt26g01c_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = mtd->oobsize / 2;
	region->length = mtd->oobsize / 2;

	return 0;
}

static int xt26g01c_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = mtd->oobsize / 2 - 2;

	return 0;
}

static const struct mtd_ooblayout_ops xt26g01c_ooblayout = {
	.ecc = xt26g01c_ooblayout_ecc,
	.free = xt26g01c_ooblayout_free,
};

/*
 * ecc bits: 0xC0[2,5]
 * [0x0000], No bit errors were detected;
 * [0x0001, 0x0111], Bit errors were detected and corrected. Not
 *	reach Flipping Bits;
 * [0x1000], Multiple bit errors were detected and
 *	not corrected.
 * [0x1100], Bit error count equals the bit flip
 *	detectionthreshold
 * else, reserved
 */
static int xt26g0xa_ecc_get_status(struct spinand_device *spinand,
				   u8 status)
{
	u8 eccsr = (status & GENMASK(5, 2)) >> 2;

	if (eccsr <= 7)
		return eccsr;
	else if (eccsr == 12)
		return 8;
	else
		return -EBADMSG;
}

/*
 * ecc bits: 0xC0[4,6]
 * [0x0], No bit errors were detected;
 * [0x001, 0x011], Bit errors were detected and corrected. Not
 *	reach Flipping Bits;
 * [0x100], Bit error count equals the bit flip
 *	detectionthreshold
 * [0x101, 0x110], Reserved;
 * [0x111], Multiple bit errors were detected and
 *	not corrected.
 */
static int xt26g02b_ecc_get_status(struct spinand_device *spinand,
				   u8 status)
{
	u8 eccsr = (status & GENMASK(6, 4)) >> 4;

	if (eccsr <= 4)
		return eccsr;
	else
		return -EBADMSG;
}

/*
 * ecc bits: 0xC0[4,7]
 * [0b0000], No bit errors were detected;
 * [0b0001, 0b0111], 1-7 Bit errors were detected and corrected. Not
 *	reach Flipping Bits;
 * [0b1000], 8 Bit errors were detected and corrected. Bit error count
 *	equals the bit flip detectionthreshold;
 * [0b1111], Bit errors greater than ECC capability(8 bits) and not corrected;
 * others, Reserved.
 */
static int xt26g01c_ecc_get_status(struct spinand_device *spinand,
				   u8 status)
{
	u8 eccsr = (status & GENMASK(7, 4)) >> 4;

	if (eccsr <= 8)
		return eccsr;
	else
		return -EBADMSG;
}

static const struct spinand_info xtx_spinand_table[] = {
	SPINAND_INFO("XT26G01A",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xE1),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&xt26g0xa_ooblayout,
				     xt26g0xa_ecc_get_status)),
	SPINAND_INFO("XT26G02A",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xE2),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&xt26g0xa_ooblayout,
				     xt26g0xa_ecc_get_status)),
	SPINAND_INFO("XT26G04A",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xE3),
		     NAND_MEMORG(1, 2048, 64, 128, 2048, 80, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&xt26g0xa_ooblayout,
				     xt26g0xa_ecc_get_status)),
	SPINAND_INFO("XT26G01B",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xF1),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&xt26g01b_ooblayout,
				     xt26g0xa_ecc_get_status)),
	SPINAND_INFO("XT26G02B",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xF2),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&xt26g02b_ooblayout,
				     xt26g02b_ecc_get_status)),
	SPINAND_INFO("XT26G01C",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x11),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&xt26g01c_ooblayout,
				     xt26g01c_ecc_get_status)),
	SPINAND_INFO("XT26G02C",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x12),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&xt26g0xa_ooblayout,
				     xt26g01c_ecc_get_status)),
	SPINAND_INFO("XT26G04C",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x13),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 80, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&xt26g01c_ooblayout,
				     xt26g01c_ecc_get_status)),
	SPINAND_INFO("XT26G11C",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x15),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&xt26g01c_ooblayout,
				     xt26g01c_ecc_get_status)),
};

static const struct spinand_manufacturer_ops xtx_spinand_manuf_ops = {
};

const struct spinand_manufacturer xtx_spinand_manufacturer = {
	.id = SPINAND_MFR_XTX,
	.name = "xtx",
	.chips = xtx_spinand_table,
	.nchips = ARRAY_SIZE(xtx_spinand_table),
	.ops = &xtx_spinand_manuf_ops,
};
