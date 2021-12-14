// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_BIWIN		0xBC

#define BIWIN_CFG_BUF_READ		BIT(3)
#define BIWIN_STATUS_ECC_HAS_BITFLIPS_T	(3 << 4)

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

static int bwjx08k_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 12;
	region->length = 4;

	return 0;
}

static int bwjx08k_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 2;
	region->length = 10;

	return 0;
}

static const struct mtd_ooblayout_ops bwjx08k_ooblayout = {
	.ecc = bwjx08k_ooblayout_ecc,
	.free = bwjx08k_ooblayout_free,
};

static int bwjx08k_ecc_get_status(struct spinand_device *spinand,
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

/* Another set for the same id[2] devices in one series */
static const struct spinand_info biwin_spinand_table[] = {
	SPINAND_INFO("BWJX08K",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xB3),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&bwjx08k_ooblayout,
				     bwjx08k_ecc_get_status)),
};

static const struct spinand_manufacturer_ops biwin_spinand_manuf_ops = {
};

const struct spinand_manufacturer biwin_spinand_manufacturer = {
	.id = SPINAND_MFR_BIWIN,
	.name = "BIWIN",
	.chips = biwin_spinand_table,
	.nchips = ARRAY_SIZE(biwin_spinand_table),
	.ops = &biwin_spinand_manuf_ops,
};
