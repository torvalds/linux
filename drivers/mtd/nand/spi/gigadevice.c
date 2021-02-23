// SPDX-License-Identifier: GPL-2.0
/*
 * Author:
 *	Chuanhong Guo <gch981213@gmail.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_GIGADEVICE			0xC8

#define GD5FXGQ4XA_STATUS_ECC_1_7_BITFLIPS	(1 << 4)
#define GD5FXGQ4XA_STATUS_ECC_8_BITFLIPS	(3 << 4)

#define GD5FXGQ4UEXXG_REG_STATUS2		0xf0

#define GD5FXGQ4UXFXXG_STATUS_ECC_MASK		(7 << 4)
#define GD5FXGQ4UXFXXG_STATUS_ECC_NO_BITFLIPS	(0 << 4)
#define GD5FXGQ4UXFXXG_STATUS_ECC_1_3_BITFLIPS	(1 << 4)
#define GD5FXGQ4UXFXXG_STATUS_ECC_UNCOR_ERROR	(7 << 4)

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(read_cache_variants_f,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP_3A(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP_3A(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP_3A(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP_3A(false, 0, 0, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
		SPINAND_PROG_LOAD(false, 0, NULL, 0));

static int gd5fxgq4xa_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 8;
	region->length = 8;

	return 0;
}

static int gd5fxgq4xa_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	if (section) {
		region->offset = 16 * section;
		region->length = 8;
	} else {
		/* section 0 has one byte reserved for bad block mark */
		region->offset = 1;
		region->length = 7;
	}
	return 0;
}

static const struct mtd_ooblayout_ops gd5fxgq4xa_ooblayout = {
	.ecc = gd5fxgq4xa_ooblayout_ecc,
	.free = gd5fxgq4xa_ooblayout_free,
};

static int gd5fxgq4xa_ecc_get_status(struct spinand_device *spinand,
					 u8 status)
{
	switch (status & STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case GD5FXGQ4XA_STATUS_ECC_1_7_BITFLIPS:
		/* 1-7 bits are flipped. return the maximum. */
		return 7;

	case GD5FXGQ4XA_STATUS_ECC_8_BITFLIPS:
		return 8;

	case STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	default:
		break;
	}

	return -EINVAL;
}

static int gd5fxgq4_variant2_ooblayout_ecc(struct mtd_info *mtd, int section,
				       struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 64;
	region->length = 64;

	return 0;
}

static int gd5fxgq4_variant2_ooblayout_free(struct mtd_info *mtd, int section,
					struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	/* Reserve 1 bytes for the BBM. */
	region->offset = 1;
	region->length = 63;

	return 0;
}

static const struct mtd_ooblayout_ops gd5fxgq4_variant2_ooblayout = {
	.ecc = gd5fxgq4_variant2_ooblayout_ecc,
	.free = gd5fxgq4_variant2_ooblayout_free,
};

static int gd5fxgq4xc_ooblayout_256_ecc(struct mtd_info *mtd, int section,
					struct mtd_oob_region *oobregion)
{
	if (section)
		return -ERANGE;

	oobregion->offset = 128;
	oobregion->length = 128;

	return 0;
}

static int gd5fxgq4xc_ooblayout_256_free(struct mtd_info *mtd, int section,
					 struct mtd_oob_region *oobregion)
{
	if (section)
		return -ERANGE;

	oobregion->offset = 1;
	oobregion->length = 127;

	return 0;
}

static const struct mtd_ooblayout_ops gd5fxgq4xc_oob_256_ops = {
	.ecc = gd5fxgq4xc_ooblayout_256_ecc,
	.free = gd5fxgq4xc_ooblayout_256_free,
};

static int gd5fxgq4uexxg_ecc_get_status(struct spinand_device *spinand,
					u8 status)
{
	u8 status2;
	struct spi_mem_op op = SPINAND_GET_FEATURE_OP(GD5FXGQ4UEXXG_REG_STATUS2,
						      &status2);
	int ret;

	switch (status & STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case GD5FXGQ4XA_STATUS_ECC_1_7_BITFLIPS:
		/*
		 * Read status2 register to determine a more fine grained
		 * bit error status
		 */
		ret = spi_mem_exec_op(spinand->spimem, &op);
		if (ret)
			return ret;

		/*
		 * 4 ... 7 bits are flipped (1..4 can't be detected, so
		 * report the maximum of 4 in this case
		 */
		/* bits sorted this way (3...0): ECCS1,ECCS0,ECCSE1,ECCSE0 */
		return ((status & STATUS_ECC_MASK) >> 2) |
			((status2 & STATUS_ECC_MASK) >> 4);

	case GD5FXGQ4XA_STATUS_ECC_8_BITFLIPS:
		return 8;

	case STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	default:
		break;
	}

	return -EINVAL;
}

static int gd5fxgq4ufxxg_ecc_get_status(struct spinand_device *spinand,
					u8 status)
{
	switch (status & GD5FXGQ4UXFXXG_STATUS_ECC_MASK) {
	case GD5FXGQ4UXFXXG_STATUS_ECC_NO_BITFLIPS:
		return 0;

	case GD5FXGQ4UXFXXG_STATUS_ECC_1_3_BITFLIPS:
		return 3;

	case GD5FXGQ4UXFXXG_STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	default: /* (2 << 4) through (6 << 4) are 4-8 corrected errors */
		return ((status & GD5FXGQ4UXFXXG_STATUS_ECC_MASK) >> 4) + 2;
	}

	return -EINVAL;
}

static const struct spinand_info gigadevice_spinand_table[] = {
	SPINAND_INFO("GD5F1GQ4xA",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xf1),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&gd5fxgq4xa_ooblayout,
				     gd5fxgq4xa_ecc_get_status)),
	SPINAND_INFO("GD5F2GQ4xA",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xf2),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&gd5fxgq4xa_ooblayout,
				     gd5fxgq4xa_ecc_get_status)),
	SPINAND_INFO("GD5F4GQ4xA",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xf4),
		     NAND_MEMORG(1, 2048, 64, 64, 4096, 80, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&gd5fxgq4xa_ooblayout,
				     gd5fxgq4xa_ecc_get_status)),
	SPINAND_INFO("GD5F4GQ4RC",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE, 0xa4, 0x68),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_f,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&gd5fxgq4xc_oob_256_ops,
				     gd5fxgq4ufxxg_ecc_get_status)),
	SPINAND_INFO("GD5F4GQ4UC",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE, 0xb4, 0x68),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_f,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&gd5fxgq4xc_oob_256_ops,
				     gd5fxgq4ufxxg_ecc_get_status)),
	SPINAND_INFO("GD5F1GQ4UExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xd1),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&gd5fxgq4_variant2_ooblayout,
				     gd5fxgq4uexxg_ecc_get_status)),
	SPINAND_INFO("GD5F1GQ4UFxxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE, 0xb1, 0x48),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_f,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&gd5fxgq4_variant2_ooblayout,
				     gd5fxgq4ufxxg_ecc_get_status)),
};

static const struct spinand_manufacturer_ops gigadevice_spinand_manuf_ops = {
};

const struct spinand_manufacturer gigadevice_spinand_manufacturer = {
	.id = SPINAND_MFR_GIGADEVICE,
	.name = "GigaDevice",
	.chips = gigadevice_spinand_table,
	.nchips = ARRAY_SIZE(gigadevice_spinand_table),
	.ops = &gigadevice_spinand_manuf_ops,
};
