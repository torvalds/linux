// SPDX-License-Identifier: GPL-2.0
/*
 * Authors:
 *	Andrey Zolotarev <andrey.zolotarev@keenetic.com> - the main driver logic
 *	Aleksei Sviridkin <f@lex.la> - adaptation to the mainline Linux kernel
 *
 * Based on:
 *	https://github.com/keenetic/kernel-49/commit/bacade569fb12bc0ad31ba09bca9b890118fbca7
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_HEYANGTEK			0xc9

#define HYF1GQ4_STATUS_ECC_LIMIT_BITFLIPS	(3 << 4)

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_1S_4S_4S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_4S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_2S_2S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_2S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_FAST_1S_1S_1S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_1S_OP(0, 1, NULL, 0, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_1S_1S_4S_OP(true, 0, NULL, 0),
		SPINAND_PROG_LOAD_1S_1S_1S_OP(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_1S_1S_4S_OP(false, 0, NULL, 0),
		SPINAND_PROG_LOAD_1S_1S_1S_OP(false, 0, NULL, 0));

/*
 * HYF1GQ4UDACAE is a GD5F1GQ4-compatible die, so the OOB layout is taken
 * from gd5fxgq4xa: the on-die ECC parity occupies bytes 8..15 of each
 * 16-byte section, the bad block marker sits in byte 0 and the remaining
 * bytes are exposed as free.
 */
static int hyf1gq4_ooblayout_ecc(struct mtd_info *mtd, int section,
				 struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	region->offset = (16 * section) + 8;
	region->length = 8;

	return 0;
}

static int hyf1gq4_ooblayout_free(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	if (section) {
		region->offset = 16 * section;
		region->length = 8;
	} else {
		/* section 0 has one byte reserved for the bad block marker */
		region->offset = 1;
		region->length = 7;
	}

	return 0;
}

static const struct mtd_ooblayout_ops hyf1gq4_ooblayout = {
	.ecc = hyf1gq4_ooblayout_ecc,
	.free = hyf1gq4_ooblayout_free,
};

static int hyf1gq4_ecc_get_status(struct spinand_device *spinand, u8 status)
{
	struct nand_device *nand = spinand_to_nand(spinand);

	switch (status & STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	case STATUS_ECC_HAS_BITFLIPS:
		/*
		 * The die exposes only a coarse 2-bit ECC status and has no
		 * register for the exact bitflip count. This code means
		 * "corrected, below the refresh threshold", so report half of
		 * the ECC strength as a representative value.
		 */
		return nanddev_get_ecc_conf(nand)->strength / 2;

	case HYF1GQ4_STATUS_ECC_LIMIT_BITFLIPS:
		/*
		 * "Corrected, refresh recommended": report the full ECC
		 * strength so the upper layers relocate the data.
		 */
		return nanddev_get_ecc_conf(nand)->strength;

	default:
		break;
	}

	return -EINVAL;
}

static const struct spinand_info heyangtek_spinand_table[] = {
	SPINAND_INFO("HYF1GQ4UDACAE",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0x21),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&hyf1gq4_ooblayout,
				     hyf1gq4_ecc_get_status)),
};

static const struct spinand_manufacturer_ops heyangtek_spinand_manuf_ops = {
};

const struct spinand_manufacturer heyangtek_spinand_manufacturer = {
	.id = SPINAND_MFR_HEYANGTEK,
	.name = "HeYangTek",
	.chips = heyangtek_spinand_table,
	.nchips = ARRAY_SIZE(heyangtek_spinand_table),
	.ops = &heyangtek_spinand_manuf_ops,
};
