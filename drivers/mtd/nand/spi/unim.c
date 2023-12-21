// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd
 *
 * Authors:
 *	Dingqiang Lin <jon.lin@rock-chips.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_UNIM_ZL		0xA1
#define SPINAND_MFR_UNIM		0xB0

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

static int tx25g01_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 8;
	region->length = 8;

	return 0;
}

static int tx25g01_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 2;
	region->length = 6;

	return 0;
}

static const struct mtd_ooblayout_ops tx25g01_ooblayout = {
	.ecc = tx25g01_ooblayout_ecc,
	.free = tx25g01_ooblayout_free,
};


static int um19a0xisw_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	return -ERANGE;
}

static int um19a0xisw_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = 62;

	return 0;
}

static const struct mtd_ooblayout_ops um19a0xisw_ooblayout = {
	.ecc = um19a0xisw_ooblayout_ecc,
	.free = um19a0xisw_ooblayout_free,
};

static int um19a1xisw_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 64;
	region->length = 64;

	return 0;
}

static int um19a1xisw_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = 62;

	return 0;
}

static const struct mtd_ooblayout_ops um19a1xisw_ooblayout = {
	.ecc = um19a1xisw_ooblayout_ecc,
	.free = um19a1xisw_ooblayout_free,
};

/*
 * ecc bits: 0xC0[4,6]
 * [0b000], No bit errors were detected;
 * [0b001, 0b011], 1~3 Bit errors were detected and corrected. Not
 *	reach Flipping Bits;
 * [0b100], Bit error count equals the bit flip
 *	detection threshold
 * others, Reserved.
 */
static int tx25g01_ecc_get_status(struct spinand_device *spinand,
				  u8 status)
{
	struct nand_device *nand = spinand_to_nand(spinand);
	u8 eccsr = (status & GENMASK(6, 4)) >> 4;

	if (eccsr < 4)
		return eccsr;
	else if (eccsr == 4)
		return nanddev_get_ecc_requirements(nand)->strength;
	else
		return -EBADMSG;
}

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
static int um19axxisw_ecc_ecc_get_status(struct spinand_device *spinand,
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

static const struct spinand_info unim_zl_spinand_table[] = {
	SPINAND_INFO("TX25G01",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xF1),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&tx25g01_ooblayout, tx25g01_ecc_get_status)),
};

static const struct spinand_info unim_spinand_table[] = {
	SPINAND_INFO("UM19A1HISW",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x24),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&um19a1xisw_ooblayout, um19axxisw_ecc_ecc_get_status)),
	SPINAND_INFO("UM19A0HCSW",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x14),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&um19a0xisw_ooblayout, um19axxisw_ecc_ecc_get_status)),
	SPINAND_INFO("UM19A0LCSW",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x15),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&um19a0xisw_ooblayout, um19axxisw_ecc_ecc_get_status)),
	SPINAND_INFO("UM19A1LISW",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x25),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&um19a1xisw_ooblayout, um19axxisw_ecc_ecc_get_status)),
};

static const struct spinand_manufacturer_ops unim_spinand_manuf_ops = {
};

const struct spinand_manufacturer unim_zl_spinand_manufacturer = {
	.id = SPINAND_MFR_UNIM_ZL,
	.name = "UNIM_ZL",
	.chips = unim_zl_spinand_table,
	.nchips = ARRAY_SIZE(unim_zl_spinand_table),
	.ops = &unim_spinand_manuf_ops,
};

const struct spinand_manufacturer unim_spinand_manufacturer = {
	.id = SPINAND_MFR_UNIM,
	.name = "UNIM",
	.chips = unim_spinand_table,
	.nchips = ARRAY_SIZE(unim_spinand_table),
	.ops = &unim_spinand_manuf_ops,
};
