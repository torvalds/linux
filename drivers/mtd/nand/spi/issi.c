// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 ISSI
 *
 * Authors:
 *	Bill Lee <blee@issi.com>
 *	Jeff Kim <jekim@issi.com>
 * Co-Author:
 *	Han Xu <han.xu@nxp.com>
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_ISSI 0x9d

#define ISSI_STATUS_ECC_MASK		GENMASK(6, 4)
#define ISSI_STATUS_ECC_NO_BITFLIPS	0
#define ISSI_STATUS_ECC_1TO3_BITFLIPS	1
#define ISSI_STATUS_ECC_UNCOR_ERROR	2
#define ISSI_STATUS_ECC_4TO6_BITFLIPS	3
#define ISSI_STATUS_ECC_7TO8_BITFLIPS	5

/*
 * As per datasheet, die selection is done by the 7th bit of Drive
 * Strength Register (Address 0xD0).
 */
#define ISSI_DIE_SELECT_REG 0xD0
#define ISSI_SELECT_DIE_MASK BIT(7)
#define ISSI_SELECT_DIE(x) ((x) << 7)

static SPINAND_OP_VARIANTS(
	quadio_read_cache_variants,
	SPINAND_PAGE_READ_FROM_CACHE_1S_1S_4S_OP(0, 1, NULL, 0, 0),
	SPINAND_PAGE_READ_FROM_CACHE_1S_1S_2S_OP(0, 1, NULL, 0, 0),
	SPINAND_PAGE_READ_FROM_CACHE_FAST_1S_1S_1S_OP(0, 1, NULL, 0, 0),
	SPINAND_PAGE_READ_FROM_CACHE_1S_1S_1S_OP(0, 1, NULL, 0, 0));

static SPINAND_OP_VARIANTS(x4_write_cache_variants,
			   SPINAND_PROG_LOAD_1S_1S_4S_OP(true, 0, NULL, 0),
			   SPINAND_PROG_LOAD_1S_1S_1S_OP(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(x4_update_cache_variants,
			   SPINAND_PROG_LOAD_1S_1S_4S_OP(false, 0, NULL, 0),
			   SPINAND_PROG_LOAD_1S_1S_1S_OP(false, 0, NULL, 0));

static int issi_8_ooblayout_ecc(struct mtd_info *mtd, int section,
				struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = mtd->oobsize / 2;
	region->length = mtd->oobsize / 2;

	return 0;
}

static int issi_8_ooblayout_free(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	/* Reserve 2 bytes for the BBM. */
	region->offset = 2;
	region->length = (mtd->oobsize / 2) - 2;

	return 0;
}

static const struct mtd_ooblayout_ops issi_8_ooblayout = {
	.ecc = issi_8_ooblayout_ecc,
	.free = issi_8_ooblayout_free,
};

static int issi_select_target(struct spinand_device *spinand,
			      unsigned int target)
{
	int ret;
	u8 regval;

	if (target > 1)
		return -EINVAL;

	ret = spinand_read_reg_op(spinand, ISSI_DIE_SELECT_REG, &regval);
	if (ret)
		return ret;

	regval &= ~ISSI_SELECT_DIE_MASK;
	regval |= ISSI_SELECT_DIE(target);

	return spinand_write_reg_op(spinand, ISSI_DIE_SELECT_REG, regval);
}

static int issi_8_ecc_get_status(struct spinand_device *spinand, u8 status)
{
	switch (FIELD_GET(ISSI_STATUS_ECC_MASK, status)) {
	case ISSI_STATUS_ECC_NO_BITFLIPS:
		return 0;

	case ISSI_STATUS_ECC_1TO3_BITFLIPS:
		return 3;

	case ISSI_STATUS_ECC_4TO6_BITFLIPS:
		return 6;

	case ISSI_STATUS_ECC_7TO8_BITFLIPS:
		return 8;

	case ISSI_STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;
	default:
		break;
	}

	return -EINVAL;
}

static const struct spinand_info issi_spinand_table[] = {
	/* IS37/38SMW01G8B 1Gb 1.8V */
	SPINAND_INFO("IS37/38SMW01G8B",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x15),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&quadio_read_cache_variants,
					      &x4_write_cache_variants,
					      &x4_update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&issi_8_ooblayout, issi_8_ecc_get_status),
		     SPINAND_SELECT_TARGET(issi_select_target)),

	/* IS37/38SMW02G8B 2Gb 1.8V */
	SPINAND_INFO("IS37/38SMW02G8B",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x25),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&quadio_read_cache_variants,
					      &x4_write_cache_variants,
					      &x4_update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&issi_8_ooblayout, issi_8_ecc_get_status),
		     SPINAND_SELECT_TARGET(issi_select_target)),

	/* IS37/38SML04G8B 4Gb 3.3V */
	SPINAND_INFO("IS37/38SML04G8",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x34),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 2),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&quadio_read_cache_variants,
					      &x4_write_cache_variants,
					      &x4_update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&issi_8_ooblayout, issi_8_ecc_get_status),
		     SPINAND_SELECT_TARGET(issi_select_target)),

	/* IS37/38SMW04G8B 4Gb 1.8V */
	SPINAND_INFO("IS37/38SMW04G8B",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x35),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 2),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&quadio_read_cache_variants,
					      &x4_write_cache_variants,
					      &x4_update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&issi_8_ooblayout, issi_8_ecc_get_status),
		     SPINAND_SELECT_TARGET(issi_select_target)),
};

static int issi_spinand_init(struct spinand_device *spinand)
{
	return 0;
}

static const struct spinand_manufacturer_ops issi_spinand_manuf_ops = {
	.init = issi_spinand_init,
};

const struct spinand_manufacturer issi_spinand_manufacturer = {
	.id = SPINAND_MFR_ISSI,
	.name = "ISSI",
	.chips = issi_spinand_table,
	.nchips = ARRAY_SIZE(issi_spinand_table),
	.ops = &issi_spinand_manuf_ops,
};
