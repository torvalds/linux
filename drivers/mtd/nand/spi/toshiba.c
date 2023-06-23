// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 exceet electronics GmbH
 * Copyright (c) 2018 Kontron Electronics GmbH
 *
 * Author: Frieder Schrempf <frieder.schrempf@kontron.de>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

/* Kioxia is new name of Toshiba memory. */
#define SPINAND_MFR_TOSHIBA		0x98
#define TOSH_STATUS_ECC_HAS_BITFLIPS_T	(3 << 4)

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_x4_variants,
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_x4_variants,
		SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
		SPINAND_PROG_LOAD(false, 0, NULL, 0));

/**
 * Backward compatibility for 1st generation Serial NAND devices
 * which don't support Quad Program Load operation.
 */
static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD(false, 0, NULL, 0));

static int tx58cxgxsxraix_ooblayout_ecc(struct mtd_info *mtd, int section,
					struct mtd_oob_region *region)
{
	if (section > 0)
		return -ERANGE;

	region->offset = mtd->oobsize / 2;
	region->length = mtd->oobsize / 2;

	return 0;
}

static int tx58cxgxsxraix_ooblayout_free(struct mtd_info *mtd, int section,
					 struct mtd_oob_region *region)
{
	if (section > 0)
		return -ERANGE;

	/* 2 bytes reserved for BBM */
	region->offset = 2;
	region->length = (mtd->oobsize / 2) - 2;

	return 0;
}

static const struct mtd_ooblayout_ops tx58cxgxsxraix_ooblayout = {
	.ecc = tx58cxgxsxraix_ooblayout_ecc,
	.free = tx58cxgxsxraix_ooblayout_free,
};

static int tx58cxgxsxraix_ecc_get_status(struct spinand_device *spinand,
					 u8 status)
{
	struct nand_device *nand = spinand_to_nand(spinand);
	u8 mbf = 0;
	struct spi_mem_op op = SPINAND_GET_FEATURE_OP(0x30, spinand->scratchbuf);

	switch (status & STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	case STATUS_ECC_HAS_BITFLIPS:
	case TOSH_STATUS_ECC_HAS_BITFLIPS_T:
		/*
		 * Let's try to retrieve the real maximum number of bitflips
		 * in order to avoid forcing the wear-leveling layer to move
		 * data around if it's not necessary.
		 */
		if (spi_mem_exec_op(spinand->spimem, &op))
			return nanddev_get_ecc_requirements(nand)->strength;

		mbf = *(spinand->scratchbuf) >> 4;

		if (WARN_ON(mbf > nanddev_get_ecc_requirements(nand)->strength || !mbf))
			return nanddev_get_ecc_requirements(nand)->strength;

		return mbf;

	default:
		break;
	}

	return -EINVAL;
}

static const struct spinand_info toshiba_spinand_table[] = {
	/* 3.3V 1Gb (1st generation) */
	SPINAND_INFO("TC58CVG0S3HRAIG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xC2),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&tx58cxgxsxraix_ooblayout,
				     tx58cxgxsxraix_ecc_get_status)),
	/* 3.3V 2Gb (1st generation) */
	SPINAND_INFO("TC58CVG1S3HRAIG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xCB),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&tx58cxgxsxraix_ooblayout,
				     tx58cxgxsxraix_ecc_get_status)),
	/* 3.3V 4Gb (1st generation) */
	SPINAND_INFO("TC58CVG2S0HRAIG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xCD),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&tx58cxgxsxraix_ooblayout,
				     tx58cxgxsxraix_ecc_get_status)),
	/* 1.8V 1Gb (1st generation) */
	SPINAND_INFO("TC58CYG0S3HRAIG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xB2),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&tx58cxgxsxraix_ooblayout,
				     tx58cxgxsxraix_ecc_get_status)),
	/* 1.8V 2Gb (1st generation) */
	SPINAND_INFO("TC58CYG1S3HRAIG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xBB),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&tx58cxgxsxraix_ooblayout,
				     tx58cxgxsxraix_ecc_get_status)),
	/* 1.8V 4Gb (1st generation) */
	SPINAND_INFO("TC58CYG2S0HRAIG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xBD),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&tx58cxgxsxraix_ooblayout,
				     tx58cxgxsxraix_ecc_get_status)),

	/*
	 * 2nd generation serial nand has HOLD_D which is equivalent to
	 * QE_BIT.
	 */
	/* 3.3V 1Gb (2nd generation) */
	SPINAND_INFO("TC58CVG0S3HRAIJ",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xE2),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_x4_variants,
					      &update_cache_x4_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&tx58cxgxsxraix_ooblayout,
				     tx58cxgxsxraix_ecc_get_status)),
	/* 3.3V 2Gb (2nd generation) */
	SPINAND_INFO("TC58CVG1S3HRAIJ",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xEB),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_x4_variants,
					      &update_cache_x4_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&tx58cxgxsxraix_ooblayout,
				     tx58cxgxsxraix_ecc_get_status)),
	/* 3.3V 4Gb (2nd generation) */
	SPINAND_INFO("TC58CVG2S0HRAIJ",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xED),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_x4_variants,
					      &update_cache_x4_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&tx58cxgxsxraix_ooblayout,
				     tx58cxgxsxraix_ecc_get_status)),
	/* 3.3V 8Gb (2nd generation) */
	SPINAND_INFO("TH58CVG3S0HRAIJ",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xE4),
		     NAND_MEMORG(1, 4096, 256, 64, 4096, 80, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_x4_variants,
					      &update_cache_x4_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&tx58cxgxsxraix_ooblayout,
				     tx58cxgxsxraix_ecc_get_status)),
	/* 1.8V 1Gb (2nd generation) */
	SPINAND_INFO("TC58CYG0S3HRAIJ",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xD2),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_x4_variants,
					      &update_cache_x4_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&tx58cxgxsxraix_ooblayout,
				     tx58cxgxsxraix_ecc_get_status)),
	/* 1.8V 2Gb (2nd generation) */
	SPINAND_INFO("TC58CYG1S3HRAIJ",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xDB),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_x4_variants,
					      &update_cache_x4_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&tx58cxgxsxraix_ooblayout,
				     tx58cxgxsxraix_ecc_get_status)),
	/* 1.8V 4Gb (2nd generation) */
	SPINAND_INFO("TC58CYG2S0HRAIJ",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xDD),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_x4_variants,
					      &update_cache_x4_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&tx58cxgxsxraix_ooblayout,
				     tx58cxgxsxraix_ecc_get_status)),
	/* 1.8V 8Gb (2nd generation) */
	SPINAND_INFO("TH58CYG3S0HRAIJ",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xD4),
		     NAND_MEMORG(1, 4096, 256, 64, 4096, 80, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_x4_variants,
					      &update_cache_x4_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&tx58cxgxsxraix_ooblayout,
				     tx58cxgxsxraix_ecc_get_status)),
};

static const struct spinand_manufacturer_ops toshiba_spinand_manuf_ops = {
};

const struct spinand_manufacturer toshiba_spinand_manufacturer = {
	.id = SPINAND_MFR_TOSHIBA,
	.name = "Toshiba",
	.chips = toshiba_spinand_table,
	.nchips = ARRAY_SIZE(toshiba_spinand_table),
	.ops = &toshiba_spinand_manuf_ops,
};
