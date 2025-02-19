// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 SkyHigh Memory Limited
 *
 * Author: Takahiro Kuwano <takahiro.kuwano@infineon.com>
 * Co-Author: KR Kim <kr.kim@skyhighmemory.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_SKYHIGH			0x01
#define SKYHIGH_STATUS_ECC_1TO2_BITFLIPS	(1 << 4)
#define SKYHIGH_STATUS_ECC_3TO6_BITFLIPS	(2 << 4)
#define SKYHIGH_STATUS_ECC_UNCOR_ERROR		(3 << 4)
#define SKYHIGH_CONFIG_PROTECT_EN		BIT(1)

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 4, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 2, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_FAST_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
		SPINAND_PROG_LOAD(false, 0, NULL, 0));

static int skyhigh_spinand_ooblayout_ecc(struct mtd_info *mtd, int section,
					 struct mtd_oob_region *region)
{
	/* ECC bytes are stored in hidden area. */
	return -ERANGE;
}

static int skyhigh_spinand_ooblayout_free(struct mtd_info *mtd, int section,
					  struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	/* ECC bytes are stored in hidden area. Reserve 2 bytes for the BBM. */
	region->offset = 2;
	region->length = mtd->oobsize - 2;

	return 0;
}

static const struct mtd_ooblayout_ops skyhigh_spinand_ooblayout = {
	.ecc = skyhigh_spinand_ooblayout_ecc,
	.free = skyhigh_spinand_ooblayout_free,
};

static int skyhigh_spinand_ecc_get_status(struct spinand_device *spinand,
					  u8 status)
{
	switch (status & STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case SKYHIGH_STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	case SKYHIGH_STATUS_ECC_1TO2_BITFLIPS:
		return 2;

	case SKYHIGH_STATUS_ECC_3TO6_BITFLIPS:
		return 6;

	default:
		break;
	}

	return -EINVAL;
}

static const struct spinand_info skyhigh_spinand_table[] = {
	SPINAND_INFO("S35ML01G301",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x15),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(6, 32),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_NO_RAW_ACCESS,
		     SPINAND_ECCINFO(&skyhigh_spinand_ooblayout,
				     skyhigh_spinand_ecc_get_status)),
	SPINAND_INFO("S35ML01G300",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x14),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(6, 32),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_NO_RAW_ACCESS,
		     SPINAND_ECCINFO(&skyhigh_spinand_ooblayout,
				     skyhigh_spinand_ecc_get_status)),
	SPINAND_INFO("S35ML02G300",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x25),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 2, 1, 1),
		     NAND_ECCREQ(6, 32),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_NO_RAW_ACCESS,
		     SPINAND_ECCINFO(&skyhigh_spinand_ooblayout,
				     skyhigh_spinand_ecc_get_status)),
	SPINAND_INFO("S35ML04G300",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x35),
		     NAND_MEMORG(1, 2048, 128, 64, 4096, 80, 2, 1, 1),
		     NAND_ECCREQ(6, 32),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_NO_RAW_ACCESS,
		     SPINAND_ECCINFO(&skyhigh_spinand_ooblayout,
				     skyhigh_spinand_ecc_get_status)),
};

static int skyhigh_spinand_init(struct spinand_device *spinand)
{
	/*
	 * Config_Protect_En (bit 1 in Block Lock register) must be set to 1
	 * before writing other bits. Do it here before core unlocks all blocks
	 * by writing block protection bits.
	 */
	return spinand_write_reg_op(spinand, REG_BLOCK_LOCK,
				    SKYHIGH_CONFIG_PROTECT_EN);
}

static const struct spinand_manufacturer_ops skyhigh_spinand_manuf_ops = {
	.init = skyhigh_spinand_init,
};

const struct spinand_manufacturer skyhigh_spinand_manufacturer = {
	.id = SPINAND_MFR_SKYHIGH,
	.name = "SkyHigh",
	.chips = skyhigh_spinand_table,
	.nchips = ARRAY_SIZE(skyhigh_spinand_table),
	.ops = &skyhigh_spinand_manuf_ops,
};
