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

static int macronix_nand_init(struct nand_chip *chip)
{
	if (nand_is_slc(chip))
		chip->bbt_options |= NAND_BBT_SCAN2NDPAGE;

	return 0;
}

const struct nand_manufacturer_ops macronix_nand_manuf_ops = {
	.init = macronix_nand_init,
};
