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

/*
 * Macronix AC series does not support using SET/GET_FEATURES to change
 * the timings unlike what is declared in the parameter page. Unflag
 * this feature to avoid unnecessary downturns.
 */
static void macronix_nand_fix_broken_get_timings(struct nand_chip *chip)
{
	unsigned int i;
	static const char * const broken_get_timings[] = {
		"MX30LF1G18AC",
		"MX30LF1G28AC",
		"MX30LF2G18AC",
		"MX30LF2G28AC",
		"MX30LF4G18AC",
		"MX30LF4G28AC",
		"MX60LF8G18AC",
		"MX30UF1G18AC",
		"MX30UF1G16AC",
		"MX30UF2G18AC",
		"MX30UF2G16AC",
		"MX30UF4G18AC",
		"MX30UF4G16AC",
		"MX30UF4G28AC",
	};

	if (!chip->parameters.supports_set_get_features)
		return;

	for (i = 0; i < ARRAY_SIZE(broken_get_timings); i++) {
		if (!strcmp(broken_get_timings[i], chip->parameters.model))
			break;
	}

	if (i == ARRAY_SIZE(broken_get_timings))
		return;

	bitmap_clear(chip->parameters.get_feature_list,
		     ONFI_FEATURE_ADDR_TIMING_MODE, 1);
	bitmap_clear(chip->parameters.set_feature_list,
		     ONFI_FEATURE_ADDR_TIMING_MODE, 1);
}

static int macronix_nand_init(struct nand_chip *chip)
{
	if (nand_is_slc(chip))
		chip->options |= NAND_BBM_FIRSTPAGE | NAND_BBM_SECONDPAGE;

	macronix_nand_fix_broken_get_timings(chip);

	return 0;
}

const struct nand_manufacturer_ops macronix_nand_manuf_ops = {
	.init = macronix_nand_init,
};
