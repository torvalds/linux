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

#include <linux/mtd/nand.h>

static void hynix_nand_decode_id(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	/* Hynix MLC   (6 byte ID): Hynix H27UBG8T2B (p.22) */
	if (chip->id.len == 6 && !nand_is_slc(chip)) {
		u8 tmp, extid = chip->id.data[3];

		/* Extract pagesize */
		mtd->writesize = 2048 << (extid & 0x03);
		extid >>= 2;

		/* Extract oobsize */
		switch (((extid >> 2) & 0x4) | (extid & 0x3)) {
		case 0:
			mtd->oobsize = 128;
			break;
		case 1:
			mtd->oobsize = 224;
			break;
		case 2:
			mtd->oobsize = 448;
			break;
		case 3:
			mtd->oobsize = 64;
			break;
		case 4:
			mtd->oobsize = 32;
			break;
		case 5:
			mtd->oobsize = 16;
			break;
		default:
			mtd->oobsize = 640;
			break;
		}

		/* Extract blocksize */
		extid >>= 2;
		tmp = ((extid >> 1) & 0x04) | (extid & 0x03);
		if (tmp < 0x03)
			mtd->erasesize = (128 * 1024) << tmp;
		else if (tmp == 0x03)
			mtd->erasesize = 768 * 1024;
		else
			mtd->erasesize = (64 * 1024) << tmp;
	} else {
		nand_decode_ext_id(chip);
	}
}

static int hynix_nand_init(struct nand_chip *chip)
{
	if (!nand_is_slc(chip))
		chip->bbt_options |= NAND_BBT_SCANLASTPAGE;
	else
		chip->bbt_options |= NAND_BBT_SCAN2NDPAGE;

	return 0;
}

const struct nand_manufacturer_ops hynix_nand_manuf_ops = {
	.detect = hynix_nand_decode_id,
	.init = hynix_nand_init,
};
