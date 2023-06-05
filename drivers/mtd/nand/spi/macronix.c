// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Macronix
 *
 * Author: Boris Brezillon <boris.brezillon@bootlin.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_MACRONIX		0xC2
#define MACRONIX_ECCSR_MASK		0x0F

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(false, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
		SPINAND_PROG_LOAD(false, 0, NULL, 0));

static int mx35lfxge4ab_ooblayout_ecc(struct mtd_info *mtd, int section,
				      struct mtd_oob_region *region)
{
	return -ERANGE;
}

static int mx35lfxge4ab_ooblayout_free(struct mtd_info *mtd, int section,
				       struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = mtd->oobsize - 2;

	return 0;
}

static const struct mtd_ooblayout_ops mx35lfxge4ab_ooblayout = {
	.ecc = mx35lfxge4ab_ooblayout_ecc,
	.free = mx35lfxge4ab_ooblayout_free,
};

static int mx35lf1ge4ab_get_eccsr(struct spinand_device *spinand, u8 *eccsr)
{
	struct spi_mem_op op = SPI_MEM_OP(SPI_MEM_OP_CMD(0x7c, 1),
					  SPI_MEM_OP_NO_ADDR,
					  SPI_MEM_OP_DUMMY(1, 1),
					  SPI_MEM_OP_DATA_IN(1, eccsr, 1));

	int ret = spi_mem_exec_op(spinand->spimem, &op);
	if (ret)
		return ret;

	*eccsr &= MACRONIX_ECCSR_MASK;
	return 0;
}

static int mx35lf1ge4ab_ecc_get_status(struct spinand_device *spinand,
				       u8 status)
{
	struct nand_device *nand = spinand_to_nand(spinand);
	u8 eccsr;

	switch (status & STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	case STATUS_ECC_HAS_BITFLIPS:
		/*
		 * Let's try to retrieve the real maximum number of bitflips
		 * in order to avoid forcing the wear-leveling layer to move
		 * data around if it's not necessary.
		 */
		if (mx35lf1ge4ab_get_eccsr(spinand, spinand->scratchbuf))
			return nanddev_get_ecc_conf(nand)->strength;

		eccsr = *spinand->scratchbuf;
		if (WARN_ON(eccsr > nanddev_get_ecc_conf(nand)->strength ||
			    !eccsr))
			return nanddev_get_ecc_conf(nand)->strength;

		return eccsr;

	default:
		break;
	}

	return -EINVAL;
}

static const struct spinand_info macronix_spinand_table[] = {
	SPINAND_INFO("MX35LF1GE4AB",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x12),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),
	SPINAND_INFO("MX35LF2GE4AB",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x22),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 2, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout, NULL)),
	SPINAND_INFO("MX35LF2GE4AD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x26),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),
	SPINAND_INFO("MX35LF4GE4AD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x37),
		     NAND_MEMORG(1, 4096, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),
	SPINAND_INFO("MX35LF1G24AD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x14),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout, NULL)),
	SPINAND_INFO("MX35LF2G24AD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x24),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 2, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout, NULL)),
	SPINAND_INFO("MX35LF4G24AD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x35),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 2, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout, NULL)),
	SPINAND_INFO("MX31LF1GE4BC",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x1e),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),
	SPINAND_INFO("MX31UF1GE4BC",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x9e),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),

	SPINAND_INFO("MX35LF2G14AC",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x20),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 2, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),
	SPINAND_INFO("MX35UF4G24AD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xb5),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 2, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),
	SPINAND_INFO("MX35UF4GE4AD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xb7),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),
	SPINAND_INFO("MX35UF2G14AC",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xa0),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 2, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),
	SPINAND_INFO("MX35UF2G24AD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xa4),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 2, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),
	SPINAND_INFO("MX35UF2GE4AD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xa6),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),
	SPINAND_INFO("MX35UF2GE4AC",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xa2),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),
	SPINAND_INFO("MX35UF1G14AC",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x90),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),
	SPINAND_INFO("MX35UF1G24AD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x94),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),
	SPINAND_INFO("MX35UF1GE4AD",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x96),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),
	SPINAND_INFO("MX35UF1GE4AC",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x92),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&mx35lfxge4ab_ooblayout,
				     mx35lf1ge4ab_ecc_get_status)),

};

static const struct spinand_manufacturer_ops macronix_spinand_manuf_ops = {
};

const struct spinand_manufacturer macronix_spinand_manufacturer = {
	.id = SPINAND_MFR_MACRONIX,
	.name = "Macronix",
	.chips = macronix_spinand_table,
	.nchips = ARRAY_SIZE(macronix_spinand_table),
	.ops = &macronix_spinand_manuf_ops,
};
