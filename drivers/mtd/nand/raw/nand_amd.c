// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Free Electrons
 * Copyright (C) 2017 NextThing Co
 *
 * Author: Boris Brezillon <boris.brezillon@free-electrons.com>
 */

#include "internals.h"

static void amd_nand_decode_id(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct nand_memory_organization *memorg;

	memorg = nanddev_get_memorg(&chip->base);

	nand_decode_ext_id(chip);

	/*
	 * Check for Spansion/AMD ID + repeating 5th, 6th byte since
	 * some Spansion chips have erasesize that conflicts with size
	 * listed in nand_ids table.
	 * Data sheet (5 byte ID): Spansion S30ML-P ORNAND (p.39)
	 */
	if (chip->id.data[4] != 0x00 && chip->id.data[5] == 0x00 &&
	    chip->id.data[6] == 0x00 && chip->id.data[7] == 0x00 &&
	    memorg->pagesize == 512) {
		memorg->pages_per_eraseblock = 256;
		memorg->pages_per_eraseblock <<= ((chip->id.data[3] & 0x03) << 1);
		mtd->erasesize = memorg->pages_per_eraseblock *
				 memorg->pagesize;
	}
}

static int amd_nand_init(struct nand_chip *chip)
{
	if (nand_is_slc(chip))
		/*
		 * According to the datasheet of some Cypress SLC NANDs,
		 * the bad block markers can be in the first, second or last
		 * page of a block. So let's check all three locations.
		 */
		chip->options |= NAND_BBM_FIRSTPAGE | NAND_BBM_SECONDPAGE |
				 NAND_BBM_LASTPAGE;

	return 0;
}

const struct nand_manufacturer_ops amd_nand_manuf_ops = {
	.detect = amd_nand_decode_id,
	.init = amd_nand_init,
};
