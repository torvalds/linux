/*
 * Copyright (C) 2017 Free Electrons
 * Copyright (C) 2017 NextThing Co
 *
 * Author: Boris Brezillon <boris.brezillon@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "internals.h"

/* Bit for detecting BENAND */
#define TOSHIBA_NAND_ID4_IS_BENAND		BIT(7)

/* Recommended to rewrite for BENAND */
#define TOSHIBA_NAND_STATUS_REWRITE_RECOMMENDED	BIT(3)

static int toshiba_nand_benand_eccstatus(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	int ret;
	unsigned int max_bitflips = 0;
	u8 status;

	/* Check Status */
	ret = nand_status_op(chip, &status);
	if (ret)
		return ret;

	if (status & NAND_STATUS_FAIL) {
		/* uncorrected */
		mtd->ecc_stats.failed++;
	} else if (status & TOSHIBA_NAND_STATUS_REWRITE_RECOMMENDED) {
		/* corrected */
		max_bitflips = mtd->bitflip_threshold;
		mtd->ecc_stats.corrected += max_bitflips;
	}

	return max_bitflips;
}

static int
toshiba_nand_read_page_benand(struct nand_chip *chip, uint8_t *buf,
			      int oob_required, int page)
{
	int ret;

	ret = nand_read_page_raw(chip, buf, oob_required, page);
	if (ret)
		return ret;

	return toshiba_nand_benand_eccstatus(chip);
}

static int
toshiba_nand_read_subpage_benand(struct nand_chip *chip, uint32_t data_offs,
				 uint32_t readlen, uint8_t *bufpoi, int page)
{
	int ret;

	ret = nand_read_page_op(chip, page, data_offs,
				bufpoi + data_offs, readlen);
	if (ret)
		return ret;

	return toshiba_nand_benand_eccstatus(chip);
}

static void toshiba_nand_benand_init(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	/*
	 * On BENAND, the entire OOB region can be used by the MTD user.
	 * The calculated ECC bytes are stored into other isolated
	 * area which is not accessible to users.
	 * This is why chip->ecc.bytes = 0.
	 */
	chip->ecc.bytes = 0;
	chip->ecc.size = 512;
	chip->ecc.strength = 8;
	chip->ecc.read_page = toshiba_nand_read_page_benand;
	chip->ecc.read_subpage = toshiba_nand_read_subpage_benand;
	chip->ecc.write_page = nand_write_page_raw;
	chip->ecc.read_page_raw = nand_read_page_raw_notsupp;
	chip->ecc.write_page_raw = nand_write_page_raw_notsupp;

	chip->options |= NAND_SUBPAGE_READ;

	mtd_set_ooblayout(mtd, &nand_ooblayout_lp_ops);
}

static void toshiba_nand_decode_id(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	nand_decode_ext_id(chip);

	/*
	 * Toshiba 24nm raw SLC (i.e., not BENAND) have 32B OOB per
	 * 512B page. For Toshiba SLC, we decode the 5th/6th byte as
	 * follows:
	 * - ID byte 6, bits[2:0]: 100b -> 43nm, 101b -> 32nm,
	 *                         110b -> 24nm
	 * - ID byte 5, bit[7]:    1 -> BENAND, 0 -> raw SLC
	 */
	if (chip->id.len >= 6 && nand_is_slc(chip) &&
	    (chip->id.data[5] & 0x7) == 0x6 /* 24nm */ &&
	    !(chip->id.data[4] & 0x80) /* !BENAND */)
		mtd->oobsize = 32 * mtd->writesize >> 9;

	/*
	 * Extract ECC requirements from 6th id byte.
	 * For Toshiba SLC, ecc requrements are as follows:
	 *  - 43nm: 1 bit ECC for each 512Byte is required.
	 *  - 32nm: 4 bit ECC for each 512Byte is required.
	 *  - 24nm: 8 bit ECC for each 512Byte is required.
	 */
	if (chip->id.len >= 6 && nand_is_slc(chip)) {
		chip->ecc_step_ds = 512;
		switch (chip->id.data[5] & 0x7) {
		case 0x4:
			chip->ecc_strength_ds = 1;
			break;
		case 0x5:
			chip->ecc_strength_ds = 4;
			break;
		case 0x6:
			chip->ecc_strength_ds = 8;
			break;
		default:
			WARN(1, "Could not get ECC info");
			chip->ecc_step_ds = 0;
			break;
		}
	}
}

static int toshiba_nand_init(struct nand_chip *chip)
{
	if (nand_is_slc(chip))
		chip->bbt_options |= NAND_BBT_SCAN2NDPAGE;

	/* Check that chip is BENAND and ECC mode is on-die */
	if (nand_is_slc(chip) && chip->ecc.mode == NAND_ECC_ON_DIE &&
	    chip->id.data[4] & TOSHIBA_NAND_ID4_IS_BENAND)
		toshiba_nand_benand_init(chip);

	return 0;
}

const struct nand_manufacturer_ops toshiba_nand_manuf_ops = {
	.detect = toshiba_nand_decode_id,
	.init = toshiba_nand_init,
};
