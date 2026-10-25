// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Toradex AG
 *
 * Author: Marcel Ziswiler <marcel.ziswiler@toradex.com>
 *
 * ESMT and GigaDevice share the same JEDEC manufacturer ID (0xc8), so this
 * file handles chips from both vendors.
 */

#include <linux/mtd/rawnand.h>
#include "internals.h"

static void esmt_nand_decode_id(struct nand_chip *chip)
{
	struct nand_device *base = &chip->base;
	struct nand_ecc_props requirements = {};

	nand_decode_ext_id(chip);

	/* Extract ECC requirements from 5th id byte. */
	if (chip->id.len >= 5 && nand_is_slc(chip)) {
		requirements.step_size = 512;
		switch (chip->id.data[4] & 0x3) {
		case 0x0:
			requirements.strength = 4;
			break;
		case 0x1:
			requirements.strength = 2;
			break;
		case 0x2:
			requirements.strength = 1;
			break;
		default:
			WARN(1, "Could not get ECC info");
			requirements.step_size = 0;
			break;
		}
	}

	nanddev_set_ecc_requirements(base, &requirements);
}

/*
 * GigaDevice GD9FU2G8F3A does not implement the timing mode feature:
 * GET_FEATURES does not report the mode that was written, and
 * SET_FEATURES is ignored, even though the parameter page advertises
 * support. The vendor confirmed the chip works at every advertised
 * timing mode without being told, so unflag the feature and configure
 * the host side only.
 */
static void esmt_nand_fix_broken_get_timings(struct nand_chip *chip)
{
	int i;
	static const char * const broken_get_timings[] = {
		"GD9FU2G8F3A",
	};

	if (!chip->parameters.supports_set_get_features)
		return;

	i = match_string(broken_get_timings, ARRAY_SIZE(broken_get_timings),
			 chip->parameters.model);
	if (i < 0)
		return;

	bitmap_clear(chip->parameters.get_feature_list,
		     ONFI_FEATURE_ADDR_TIMING_MODE, 1);
	bitmap_clear(chip->parameters.set_feature_list,
		     ONFI_FEATURE_ADDR_TIMING_MODE, 1);
}

static int esmt_nand_init(struct nand_chip *chip)
{
	if (nand_is_slc(chip))
		/*
		 * It is known that some ESMT SLC NANDs have been shipped
		 * with the factory bad block markers in the first or last page
		 * of the block, instead of the first or second page. To be on
		 * the safe side, let's check all three locations.
		 */
		chip->options |= NAND_BBM_FIRSTPAGE | NAND_BBM_SECONDPAGE |
				 NAND_BBM_LASTPAGE;

	esmt_nand_fix_broken_get_timings(chip);

	return 0;
}

const struct nand_manufacturer_ops esmt_nand_manuf_ops = {
	.detect = esmt_nand_decode_id,
	.init = esmt_nand_init,
};
