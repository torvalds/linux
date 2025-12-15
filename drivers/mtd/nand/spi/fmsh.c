// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020-2021 Rockchip Electronics Co., Ltd.
 *
 * Author: Dingqiang Lin <jon.lin@rock-chips.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define FM25S01BI3_STATUS_ECC_MASK		(7 << 4)
	#define FM25S01BI3_STATUS_ECC_NO_BITFLIPS	(0 << 4)
	#define FM25S01BI3_STATUS_ECC_1_3_BITFLIPS	(1 << 4)
	#define FM25S01BI3_STATUS_ECC_UNCOR_ERROR	(2 << 4)
	#define FM25S01BI3_STATUS_ECC_4_6_BITFLIPS	(3 << 4)
	#define FM25S01BI3_STATUS_ECC_7_8_BITFLIPS	(5 << 4)

#define SPINAND_MFR_FMSH		0xA1

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_1S_4S_4S_OP(0, 2, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_4S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_2S_2S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_2S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_FAST_1S_1S_1S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_1S_OP(0, 1, NULL, 0, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_1S_1S_4S_OP(true, 0, NULL, 0),
		SPINAND_PROG_LOAD_1S_1S_1S_OP(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_1S_1S_4S_OP(false, 0, NULL, 0),
		SPINAND_PROG_LOAD_1S_1S_1S_OP(false, 0, NULL, 0));

static int fm25s01a_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	return -ERANGE;
}

static int fm25s01a_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = 62;

	return 0;
}

static int fm25s01bi3_ecc_get_status(struct spinand_device *spinand,
				     u8 status)
{
	switch (status & FM25S01BI3_STATUS_ECC_MASK) {
	case FM25S01BI3_STATUS_ECC_NO_BITFLIPS:
		return 0;

	case FM25S01BI3_STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	case FM25S01BI3_STATUS_ECC_1_3_BITFLIPS:
		return 3;

	case FM25S01BI3_STATUS_ECC_4_6_BITFLIPS:
		return 6;

	case FM25S01BI3_STATUS_ECC_7_8_BITFLIPS:
		return 8;

	default:
		break;
	}

	return -EINVAL;
}

static int fm25s01bi3_ooblayout_ecc(struct mtd_info *mtd, int section,
				    struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 64;
	region->length = 64;

	return 0;
}

static int fm25s01bi3_ooblayout_free(struct mtd_info *mtd, int section,
				     struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 4;
	region->length = 12;

	return 0;
}

static const struct mtd_ooblayout_ops fm25s01a_ooblayout = {
	.ecc = fm25s01a_ooblayout_ecc,
	.free = fm25s01a_ooblayout_free,
};

static const struct mtd_ooblayout_ops fm25s01bi3_ooblayout = {
	.ecc = fm25s01bi3_ooblayout_ecc,
	.free = fm25s01bi3_ooblayout_free,
};

static const struct spinand_info fmsh_spinand_table[] = {
	SPINAND_INFO("FM25S01A",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xE4),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&fm25s01a_ooblayout, NULL)),
	SPINAND_INFO("FM25S01BI3",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xd4),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&fm25s01bi3_ooblayout,
				      fm25s01bi3_ecc_get_status)),
};

static const struct spinand_manufacturer_ops fmsh_spinand_manuf_ops = {
};

const struct spinand_manufacturer fmsh_spinand_manufacturer = {
	.id = SPINAND_MFR_FMSH,
	.name = "Fudan Micro",
	.chips = fmsh_spinand_table,
	.nchips = ARRAY_SIZE(fmsh_spinand_table),
	.ops = &fmsh_spinand_manuf_ops,
};
