// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "rvu_struct.h"
#include "common.h"
#include "mbox.h"
#include "rvu.h"

struct reg_range {
	u64  start;
	u64  end;
};

struct hw_reg_map {
	u8	regblk;
	u8	num_ranges;
	u64	mask;
#define	 MAX_REG_RANGES	8
	struct reg_range range[MAX_REG_RANGES];
};

static struct hw_reg_map txsch_reg_map[NIX_TXSCH_LVL_CNT] = {
	{NIX_TXSCH_LVL_SMQ, 2, 0xFFFF, {{0x0700, 0x0708}, {0x1400, 0x14C8} } },
	{NIX_TXSCH_LVL_TL4, 3, 0xFFFF, {{0x0B00, 0x0B08}, {0x0B10, 0x0B18},
			      {0x1200, 0x12E0} } },
	{NIX_TXSCH_LVL_TL3, 4, 0xFFFF, {{0x1000, 0x10E0}, {0x1600, 0x1608},
			      {0x1610, 0x1618}, {0x1700, 0x17B0} } },
	{NIX_TXSCH_LVL_TL2, 2, 0xFFFF, {{0x0E00, 0x0EE0}, {0x1700, 0x17B0} } },
	{NIX_TXSCH_LVL_TL1, 1, 0xFFFF, {{0x0C00, 0x0D98} } },
};

bool rvu_check_valid_reg(int regmap, int regblk, u64 reg)
{
	int idx;
	struct hw_reg_map *map;

	/* Only 64bit offsets */
	if (reg & 0x07)
		return false;

	if (regmap == TXSCHQ_HWREGMAP) {
		if (regblk >= NIX_TXSCH_LVL_CNT)
			return false;
		map = &txsch_reg_map[regblk];
	} else {
		return false;
	}

	/* Should never happen */
	if (map->regblk != regblk)
		return false;

	reg &= map->mask;

	for (idx = 0; idx < map->num_ranges; idx++) {
		if (reg >= map->range[idx].start &&
		    reg < map->range[idx].end)
			return true;
	}
	return false;
}
