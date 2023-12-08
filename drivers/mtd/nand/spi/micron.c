// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2017 Micron Technology, Inc.
 *
 * Authors:
 *	Peter Pan <peterpandong@micron.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_MICRON		0x2c

#define MICRON_STATUS_ECC_MASK		GENMASK(7, 4)
#define MICRON_STATUS_ECC_NO_BITFLIPS	(0 << 4)
#define MICRON_STATUS_ECC_1TO3_BITFLIPS	(1 << 4)
#define MICRON_STATUS_ECC_4TO6_BITFLIPS	(3 << 4)
#define MICRON_STATUS_ECC_7TO8_BITFLIPS	(5 << 4)

#define MICRON_CFG_CR			BIT(0)

/*
 * As per datasheet, die selection is done by the 6th bit of Die
 * Select Register (Address 0xD0).
 */
#define MICRON_DIE_SELECT_REG	0xD0

#define MICRON_SELECT_DIE(x)	((x) << 6)

static SPINAND_OP_VARIANTS(quadio_read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 2, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(x4_write_cache_variants,
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(x4_update_cache_variants,
		SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
		SPINAND_PROG_LOAD(false, 0, NULL, 0));

/* Micron  MT29F2G01AAAED Device */
static SPINAND_OP_VARIANTS(x4_read_cache_variants,
			   SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
			   SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
			   SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
			   SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(x1_write_cache_variants,
			   SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(x1_update_cache_variants,
			   SPINAND_PROG_LOAD(false, 0, NULL, 0));

static int micron_8_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = mtd->oobsize / 2;
	region->length = mtd->oobsize / 2;

	return 0;
}

static int micron_8_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	/* Reserve 2 bytes for the BBM. */
	region->offset = 2;
	region->length = (mtd->oobsize / 2) - 2;

	return 0;
}

static const struct mtd_ooblayout_ops micron_8_ooblayout = {
	.ecc = micron_8_ooblayout_ecc,
	.free = micron_8_ooblayout_free,
};

static int micron_4_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	struct spinand_device *spinand = mtd_to_spinand(mtd);

	if (section >= spinand->base.memorg.pagesize /
			mtd->ecc_step_size)
		return -ERANGE;

	region->offset = (section * 16) + 8;
	region->length = 8;

	return 0;
}

static int micron_4_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	struct spinand_device *spinand = mtd_to_spinand(mtd);

	if (section >= spinand->base.memorg.pagesize /
			mtd->ecc_step_size)
		return -ERANGE;

	if (section) {
		region->offset = 16 * section;
		region->length = 8;
	} else {
		/* section 0 has two bytes reserved for the BBM */
		region->offset = 2;
		region->length = 6;
	}

	return 0;
}

static const struct mtd_ooblayout_ops micron_4_ooblayout = {
	.ecc = micron_4_ooblayout_ecc,
	.free = micron_4_ooblayout_free,
};

static int micron_select_target(struct spinand_device *spinand,
				unsigned int target)
{
	struct spi_mem_op op = SPINAND_SET_FEATURE_OP(MICRON_DIE_SELECT_REG,
						      spinand->scratchbuf);

	if (target > 1)
		return -EINVAL;

	*spinand->scratchbuf = MICRON_SELECT_DIE(target);

	return spi_mem_exec_op(spinand->spimem, &op);
}

static int micron_8_ecc_get_status(struct spinand_device *spinand,
				   u8 status)
{
	switch (status & MICRON_STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	case MICRON_STATUS_ECC_1TO3_BITFLIPS:
		return 3;

	case MICRON_STATUS_ECC_4TO6_BITFLIPS:
		return 6;

	case MICRON_STATUS_ECC_7TO8_BITFLIPS:
		return 8;

	default:
		break;
	}

	return -EINVAL;
}

static const struct spinand_info micron_spinand_table[] = {
	/* M79A 2Gb 3.3V */
	SPINAND_INFO("MT29F2G01ABAGD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x24),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 2, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&quadio_read_cache_variants,
					      &x4_write_cache_variants,
					      &x4_update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&micron_8_ooblayout,
				     micron_8_ecc_get_status)),
	/* M79A 2Gb 1.8V */
	SPINAND_INFO("MT29F2G01ABBGD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x25),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 2, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&quadio_read_cache_variants,
					      &x4_write_cache_variants,
					      &x4_update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&micron_8_ooblayout,
				     micron_8_ecc_get_status)),
	/* M78A 1Gb 3.3V */
	SPINAND_INFO("MT29F1G01ABAFD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x14),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&quadio_read_cache_variants,
					      &x4_write_cache_variants,
					      &x4_update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&micron_8_ooblayout,
				     micron_8_ecc_get_status)),
	/* M78A 1Gb 1.8V */
	SPINAND_INFO("MT29F1G01ABAFD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x15),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&quadio_read_cache_variants,
					      &x4_write_cache_variants,
					      &x4_update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&micron_8_ooblayout,
				     micron_8_ecc_get_status)),
	/* M79A 4Gb 3.3V */
	SPINAND_INFO("MT29F4G01ADAGD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x36),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 80, 2, 1, 2),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&quadio_read_cache_variants,
					      &x4_write_cache_variants,
					      &x4_update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&micron_8_ooblayout,
				     micron_8_ecc_get_status),
		     SPINAND_SELECT_TARGET(micron_select_target)),
	/* M70A 4Gb 3.3V */
	SPINAND_INFO("MT29F4G01ABAFD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x34),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&quadio_read_cache_variants,
					      &x4_write_cache_variants,
					      &x4_update_cache_variants),
		     SPINAND_HAS_CR_FEAT_BIT,
		     SPINAND_ECCINFO(&micron_8_ooblayout,
				     micron_8_ecc_get_status)),
	/* M70A 4Gb 1.8V */
	SPINAND_INFO("MT29F4G01ABBFD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x35),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&quadio_read_cache_variants,
					      &x4_write_cache_variants,
					      &x4_update_cache_variants),
		     SPINAND_HAS_CR_FEAT_BIT,
		     SPINAND_ECCINFO(&micron_8_ooblayout,
				     micron_8_ecc_get_status)),
	/* M70A 8Gb 3.3V */
	SPINAND_INFO("MT29F8G01ADAFD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x46),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 1, 1, 2),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&quadio_read_cache_variants,
					      &x4_write_cache_variants,
					      &x4_update_cache_variants),
		     SPINAND_HAS_CR_FEAT_BIT,
		     SPINAND_ECCINFO(&micron_8_ooblayout,
				     micron_8_ecc_get_status),
		     SPINAND_SELECT_TARGET(micron_select_target)),
	/* M70A 8Gb 1.8V */
	SPINAND_INFO("MT29F8G01ADBFD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x47),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 1, 1, 2),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&quadio_read_cache_variants,
					      &x4_write_cache_variants,
					      &x4_update_cache_variants),
		     SPINAND_HAS_CR_FEAT_BIT,
		     SPINAND_ECCINFO(&micron_8_ooblayout,
				     micron_8_ecc_get_status),
		     SPINAND_SELECT_TARGET(micron_select_target)),
	/* M69A 2Gb 3.3V */
	SPINAND_INFO("MT29F2G01AAAED",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x9F),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 80, 2, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&x4_read_cache_variants,
					      &x1_write_cache_variants,
					      &x1_update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&micron_4_ooblayout, NULL)),
};

static int micron_spinand_init(struct spinand_device *spinand)
{
	/*
	 * M70A device series enable Continuous Read feature at Power-up,
	 * which is not supported. Disable this bit to avoid any possible
	 * failure.
	 */
	if (spinand->flags & SPINAND_HAS_CR_FEAT_BIT)
		return spinand_upd_cfg(spinand, MICRON_CFG_CR, 0);

	return 0;
}

static const struct spinand_manufacturer_ops micron_spinand_manuf_ops = {
	.init = micron_spinand_init,
};

const struct spinand_manufacturer micron_spinand_manufacturer = {
	.id = SPINAND_MFR_MICRON,
	.name = "Micron",
	.chips = micron_spinand_table,
	.nchips = ARRAY_SIZE(micron_spinand_table),
	.ops = &micron_spinand_manuf_ops,
};
