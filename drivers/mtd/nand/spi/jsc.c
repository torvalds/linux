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

#define SPINAND_MFR_JSC		0xBF

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

static int js28u1gqscahg_ooblayout_ecc(struct mtd_info *mtd, int section,
				       struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = mtd->oobsize / 2;
	region->length = mtd->oobsize / 2;

	return 0;
}

static int js28u1gqscahg_ooblayout_free(struct mtd_info *mtd, int section,
					struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	/* Reserve 2 bytes for the BBM. */
	region->offset = 2;
	region->length = mtd->oobsize / 2 - 2;

	return 0;
}

static const struct mtd_ooblayout_ops js28u1gqscahg_ooblayout = {
	.ecc = js28u1gqscahg_ooblayout_ecc,
	.free = js28u1gqscahg_ooblayout_free,
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
static int js28u1gqscahg_ecc_get_status(struct spinand_device *spinand,
					u8 status)
{
	u8 eccsr = (status & GENMASK(6, 4)) >> 2;

	if (eccsr <= 7)
		return eccsr;
	else if (eccsr == 12)
		return 8;
	else
		return -EBADMSG;
}

static const struct spinand_info jsc_spinand_table[] = {
	SPINAND_INFO("JS28U1GQSCAHG-83",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x21),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&js28u1gqscahg_ooblayout, js28u1gqscahg_ecc_get_status)),
};

static const struct spinand_manufacturer_ops jsc_spinand_manuf_ops = {
};

const struct spinand_manufacturer jsc_spinand_manufacturer = {
	.id = SPINAND_MFR_JSC,
	.name = "JSC",
	.chips = jsc_spinand_table,
	.nchips = ARRAY_SIZE(jsc_spinand_table),
	.ops = &jsc_spinand_manuf_ops,
};
