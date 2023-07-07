// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_SKYHIGH		0x01

#define SKYHIGH_STATUS_ECC_1_2_BITFLIPS	(1 << 4)
#define SKYHIGH_STATUS_ECC_3_4_BITFLIPS	(2 << 4)
#define SKYHIGH_STATUS_ECC_UNCOR_ERROR	(3 << 4)

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
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static int s35ml04g3_ooblayout_ecc(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	return -ERANGE;
}

static int s35ml04g3_ooblayout_free(struct mtd_info *mtd, int section,
				    struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = mtd->oobsize - 2;

	return 0;
}

static const struct mtd_ooblayout_ops s35ml04g3_ooblayout = {
	.ecc = s35ml04g3_ooblayout_ecc,
	.free = s35ml04g3_ooblayout_free,
};


static int s35ml0xg3_ecc_get_status(struct spinand_device *spinand,
				   u8 status)
{
	struct nand_device *nand = spinand_to_nand(spinand);

	switch (status & STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case SKYHIGH_STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	case SKYHIGH_STATUS_ECC_1_2_BITFLIPS:
		return 2;

	default:
		return nanddev_get_ecc_requirements(nand)->strength;
	}

	return -EINVAL;
}

static const struct spinand_info skyhigh_spinand_table[] = {
	SPINAND_INFO("S35ML01G3",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x15),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 2, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&s35ml04g3_ooblayout, s35ml0xg3_ecc_get_status)),
	SPINAND_INFO("S35ML02G3",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x25),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 2, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&s35ml04g3_ooblayout, s35ml0xg3_ecc_get_status)),
	SPINAND_INFO("S35ML04G3",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x35),
		     NAND_MEMORG(1, 2048, 128, 64, 4096, 80, 2, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&s35ml04g3_ooblayout, s35ml0xg3_ecc_get_status)),
};

static const struct spinand_manufacturer_ops skyhigh_spinand_manuf_ops = {
};

const struct spinand_manufacturer skyhigh_spinand_manufacturer = {
	.id = SPINAND_MFR_SKYHIGH,
	.name = "skyhigh",
	.chips = skyhigh_spinand_table,
	.nchips = ARRAY_SIZE(skyhigh_spinand_table),
	.ops = &skyhigh_spinand_manuf_ops,
};
