// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Aidan MacDonald
 *
 * Author: Aidan MacDonald <aidanmacdonald.0x0@gmail.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>


#define SPINAND_MFR_ATO		0x9b


static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
		SPINAND_PROG_LOAD(false, 0, NULL, 0));


static int ato25d1ga_ooblayout_ecc(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 8;
	region->length = 8;
	return 0;
}

static int ato25d1ga_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	if (section) {
		region->offset = (16 * section);
		region->length = 8;
	} else {
		/* first byte of section 0 is reserved for the BBM */
		region->offset = 1;
		region->length = 7;
	}

	return 0;
}

static const struct mtd_ooblayout_ops ato25d1ga_ooblayout = {
	.ecc = ato25d1ga_ooblayout_ecc,
	.free = ato25d1ga_ooblayout_free,
};


static const struct spinand_info ato_spinand_table[] = {
	SPINAND_INFO("ATO25D1GA",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x12),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&ato25d1ga_ooblayout, NULL)),
};

static const struct spinand_manufacturer_ops ato_spinand_manuf_ops = {
};

const struct spinand_manufacturer ato_spinand_manufacturer = {
	.id = SPINAND_MFR_ATO,
	.name = "ATO",
	.chips = ato_spinand_table,
	.nchips = ARRAY_SIZE(ato_spinand_table),
	.ops = &ato_spinand_manuf_ops,
};
