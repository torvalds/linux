// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd
 *
 * Authors:
 *	Dingqiang Lin <jon.lin@rock-chips.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_GSTO		0x52

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

static int gss0xgsak1_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 32;
	region->length = 32;

	return 0;
}

static int gss0xgsak1_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = 30;

	return 0;
}

static const struct mtd_ooblayout_ops gss0xgsak1_ooblayout = {
	.ecc = gss0xgsak1_ooblayout_ecc,
	.free = gss0xgsak1_ooblayout_free,
};

static const struct spinand_info gsto_spinand_table[] = {
	SPINAND_INFO("GSS01GSAK1",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xBA, 0x13),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 10, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&gss0xgsak1_ooblayout, NULL)),
	SPINAND_INFO("GSS02GSAK1",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xBA, 0x23),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 20, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&gss0xgsak1_ooblayout, NULL)),
};

static const struct spinand_manufacturer_ops gsto_spinand_manuf_ops = {
};

const struct spinand_manufacturer gsto_spinand_manufacturer = {
	.id = SPINAND_MFR_GSTO,
	.name = "GSTO",
	.chips = gsto_spinand_table,
	.nchips = ARRAY_SIZE(gsto_spinand_table),
	.ops = &gsto_spinand_manuf_ops,
};
