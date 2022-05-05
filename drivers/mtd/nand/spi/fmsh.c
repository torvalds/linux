// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020-2021 Rockchip Electronics Co., Ltd
 *
 * Authors:
 *	Dingqiang Lin <jon.lin@rock-chips.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_FMSH		0xA1

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

static const struct mtd_ooblayout_ops fm25s01a_ooblayout = {
	.ecc = fm25s01a_ooblayout_ecc,
	.free = fm25s01a_ooblayout_free,
};

static int fm25s01_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 64;
	region->length = 64;

	return 0;
}

static int fm25s01_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = 62;

	return 0;
}

static const struct mtd_ooblayout_ops fm25s01_ooblayout = {
	.ecc = fm25s01_ooblayout_ecc,
	.free = fm25s01_ooblayout_free,
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
	SPINAND_INFO("FM25S02A",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xE5),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 2, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     1,
		     SPINAND_ECCINFO(&fm25s01a_ooblayout, NULL)),
	SPINAND_INFO("FM25S01",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xA1),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&fm25s01_ooblayout, NULL)),
	SPINAND_INFO("FM25LS01",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xA5),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&fm25s01_ooblayout, NULL)),
};

static const struct spinand_manufacturer_ops fmsh_spinand_manuf_ops = {
};

const struct spinand_manufacturer fmsh_spinand_manufacturer = {
	.id = SPINAND_MFR_FMSH,
	.name = "FMSH",
	.chips = fmsh_spinand_table,
	.nchips = ARRAY_SIZE(fmsh_spinand_table),
	.ops = &fmsh_spinand_manuf_ops,
};
