// SPDX-License-Identifier: GPL-2.0
/*
 * Author:
 * Felix Matouschek <felix@matouschek.org>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_XTX	0x0B

#define XT26G0XA_STATUS_ECC_MASK	GENMASK(5, 2)
#define XT26G0XA_STATUS_ECC_NO_DETECTED	(0 << 2)
#define XT26G0XA_STATUS_ECC_8_CORRECTED	(3 << 4)
#define XT26G0XA_STATUS_ECC_UNCOR_ERROR	(2 << 4)

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 1, NULL, 0),
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

static int xt26g0xa_ooblayout_ecc(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 48;
	region->length = 16;

	return 0;
}

static int xt26g0xa_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 1;
	region->length = 47;

	return 0;
}

static const struct mtd_ooblayout_ops xt26g0xa_ooblayout = {
	.ecc = xt26g0xa_ooblayout_ecc,
	.free = xt26g0xa_ooblayout_free,
};

static int xt26g0xa_ecc_get_status(struct spinand_device *spinand,
					 u8 status)
{
	status = status & XT26G0XA_STATUS_ECC_MASK;

	switch (status) {
	case XT26G0XA_STATUS_ECC_NO_DETECTED:
		return 0;
	case XT26G0XA_STATUS_ECC_8_CORRECTED:
		return 8;
	case XT26G0XA_STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;
	default:
		break;
	}

	/* At this point values greater than (2 << 4) are invalid  */
	if (status > XT26G0XA_STATUS_ECC_UNCOR_ERROR)
		return -EINVAL;

	/* (1 << 2) through (7 << 2) are 1-7 corrected errors */
	return status >> 2;
}

static const struct spinand_info xtx_spinand_table[] = {
	SPINAND_INFO("XT26G01A",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xE1),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&xt26g0xa_ooblayout,
				     xt26g0xa_ecc_get_status)),
	SPINAND_INFO("XT26G02A",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xE2),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&xt26g0xa_ooblayout,
				     xt26g0xa_ecc_get_status)),
	SPINAND_INFO("XT26G04A",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xE3),
		     NAND_MEMORG(1, 2048, 64, 128, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&xt26g0xa_ooblayout,
				     xt26g0xa_ecc_get_status)),
};

static const struct spinand_manufacturer_ops xtx_spinand_manuf_ops = {
};

const struct spinand_manufacturer xtx_spinand_manufacturer = {
	.id = SPINAND_MFR_XTX,
	.name = "XTX",
	.chips = xtx_spinand_table,
	.nchips = ARRAY_SIZE(xtx_spinand_table),
	.ops = &xtx_spinand_manuf_ops,
};
