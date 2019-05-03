// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Toradex AG
 *
 * Author: Marcel Ziswiler <marcel.ziswiler@toradex.com>
 */

#include <linux/mtd/rawnand.h>
#include "internals.h"

static void esmt_nand_decode_id(struct nand_chip *chip)
{
	nand_decode_ext_id(chip);

	/* Extract ECC requirements from 5th id byte. */
	if (chip->id.len >= 5 && nand_is_slc(chip)) {
		chip->ecc_step_ds = 512;
		switch (chip->id.data[4] & 0x3) {
		case 0x0:
			chip->ecc_strength_ds = 4;
			break;
		case 0x1:
			chip->ecc_strength_ds = 2;
			break;
		case 0x2:
			chip->ecc_strength_ds = 1;
			break;
		default:
			WARN(1, "Could not get ECC info");
			chip->ecc_step_ds = 0;
			break;
		}
	}
}

static int esmt_nand_init(struct nand_chip *chip)
{
	if (nand_is_slc(chip))
		chip->bbt_options |= NAND_BBT_SCAN2NDPAGE;

	return 0;
}

const struct nand_manufacturer_ops esmt_nand_manuf_ops = {
	.detect = esmt_nand_decode_id,
	.init = esmt_nand_init,
};
