// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Jeff Kletsky
 *
 * Author: Jeff Kletsky <git-commits@allycomm.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>


#define SPINAND_MFR_PARAGON	0xa1


#define PN26G0XA_STATUS_ECC_BITMASK		(3 << 4)

#define PN26G0XA_STATUS_ECC_NONE_DETECTED	(0 << 4)
#define PN26G0XA_STATUS_ECC_1_7_CORRECTED	(1 << 4)
#define PN26G0XA_STATUS_ECC_ERRORED		(2 << 4)
#define PN26G0XA_STATUS_ECC_8_CORRECTED		(3 << 4)


static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_1S_4S_4S_OP(0, 2, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_4S_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_2S_2S_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_2S_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_FAST_1S_1S_1S_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_1S_OP(0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
		SPINAND_PROG_LOAD(false, 0, NULL, 0));


static int pn26g0xa_ooblayout_ecc(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = 6 + (15 * section); /* 4 BBM + 2 user bytes */
	region->length = 13;

	return 0;
}

static int pn26g0xa_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section > 4)
		return -ERANGE;

	if (section == 4) {
		region->offset = 64;
		region->length = 64;
	} else {
		region->offset = 4 + (15 * section);
		region->length = 2;
	}

	return 0;
}

static int pn26g0xa_ecc_get_status(struct spinand_device *spinand,
				   u8 status)
{
	switch (status & PN26G0XA_STATUS_ECC_BITMASK) {
	case PN26G0XA_STATUS_ECC_NONE_DETECTED:
		return 0;

	case PN26G0XA_STATUS_ECC_1_7_CORRECTED:
		return 7;	/* Return upper limit by convention */

	case PN26G0XA_STATUS_ECC_8_CORRECTED:
		return 8;

	case PN26G0XA_STATUS_ECC_ERRORED:
		return -EBADMSG;

	default:
		break;
	}

	return -EINVAL;
}

static const struct mtd_ooblayout_ops pn26g0xa_ooblayout = {
	.ecc = pn26g0xa_ooblayout_ecc,
	.free = pn26g0xa_ooblayout_free,
};


static const struct spinand_info paragon_spinand_table[] = {
	SPINAND_INFO("PN26G01A",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xe1),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 21, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&pn26g0xa_ooblayout,
				     pn26g0xa_ecc_get_status)),
	SPINAND_INFO("PN26G02A",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xe2),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 41, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     0,
		     SPINAND_ECCINFO(&pn26g0xa_ooblayout,
				     pn26g0xa_ecc_get_status)),
};

static const struct spinand_manufacturer_ops paragon_spinand_manuf_ops = {
};

const struct spinand_manufacturer paragon_spinand_manufacturer = {
	.id = SPINAND_MFR_PARAGON,
	.name = "Paragon",
	.chips = paragon_spinand_table,
	.nchips = ARRAY_SIZE(paragon_spinand_table),
	.ops = &paragon_spinand_manuf_ops,
};
