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

#include <linux/mtd/rawnand.h>

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

	return 0;
}

const struct nand_manufacturer_ops toshiba_nand_manuf_ops = {
	.detect = toshiba_nand_decode_id,
	.init = toshiba_nand_init,
};
