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

#define SPINAND_MFR_HYF		0xC9

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

static int hyf1gq4upacae_ooblayout_ecc(struct mtd_info *mtd, int section,
				       struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 64;
	region->length = 64;

	return 0;
}

static int hyf1gq4upacae_ooblayout_free(struct mtd_info *mtd, int section,
					struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 1;
	region->length = 63;

	return 0;
}

static const struct mtd_ooblayout_ops hyf1gq4upacae_ooblayout = {
	.ecc = hyf1gq4upacae_ooblayout_ecc,
	.free = hyf1gq4upacae_ooblayout_free,
};

static int hyf1gq4udacae_ooblayout_ecc(struct mtd_info *mtd, int section,
				       struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 8;
	region->length = 8;

	return 0;
}

static int hyf1gq4udacae_ooblayout_free(struct mtd_info *mtd, int section,
					struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 4;
	region->length = 4;

	return 0;
}

static const struct mtd_ooblayout_ops hyf1gq4udacae_ooblayout = {
	.ecc = hyf1gq4udacae_ooblayout_ecc,
	.free = hyf1gq4udacae_ooblayout_free,
};

static int hyf2gq4uaacae_ooblayout_ecc(struct mtd_info *mtd, int section,
				       struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (32 * section) + 8;
	region->length = 24;

	return 0;
}

static int hyf2gq4uaacae_ooblayout_free(struct mtd_info *mtd, int section,
					struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = 16 * section;
	region->length = 8;

	return 0;
}

static const struct mtd_ooblayout_ops hyf2gq4uaacae_ooblayout = {
	.ecc = hyf2gq4uaacae_ooblayout_ecc,
	.free = hyf2gq4uaacae_ooblayout_free,
};

static int hyf1gq4udacae_ecc_get_status(struct spinand_device *spinand,
					u8 status)
{
	struct nand_device *nand = spinand_to_nand(spinand);

	switch (status & STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	case STATUS_ECC_HAS_BITFLIPS:
		return 1;

	default:
		return nanddev_get_ecc_requirements(nand)->strength;
	}

	return -EINVAL;
}

static const struct spinand_info hyf_spinand_table[] = {
	SPINAND_INFO("HYF1GQ4UPACAE",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xA1),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&hyf1gq4upacae_ooblayout, NULL)),
	SPINAND_INFO("HYF1GQ4UDACAE",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x21),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&hyf1gq4udacae_ooblayout,
				     hyf1gq4udacae_ecc_get_status)),
	SPINAND_INFO("HYF1GQ4UDACAE",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x22),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&hyf1gq4udacae_ooblayout,
				     hyf1gq4udacae_ecc_get_status)),
	SPINAND_INFO("HYF2GQ4UAACAE",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x52),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(14, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&hyf2gq4uaacae_ooblayout,
				     hyf1gq4udacae_ecc_get_status)),
	SPINAND_INFO("HYF2GQ4UHCCAE",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x5A),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(14, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&hyf2gq4uaacae_ooblayout,
				     hyf1gq4udacae_ecc_get_status)),
	SPINAND_INFO("HYF4GQ4UAACBE",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xD4),
		     NAND_MEMORG(1, 4096, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&hyf2gq4uaacae_ooblayout,
				     hyf1gq4udacae_ecc_get_status)),
};

static const struct spinand_manufacturer_ops hyf_spinand_manuf_ops = {
};

const struct spinand_manufacturer hyf_spinand_manufacturer = {
	.id = SPINAND_MFR_HYF,
	.name = "hyf",
	.chips = hyf_spinand_table,
	.nchips = ARRAY_SIZE(hyf_spinand_table),
	.ops = &hyf_spinand_manuf_ops,
};
