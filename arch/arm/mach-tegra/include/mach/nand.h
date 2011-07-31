/*
 * arch/arm/mach-tegra/include/mach/nand.h
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Dima Zavin <dmitriyz@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MACH_TEGRA_NAND_H
#define __MACH_TEGRA_NAND_H

struct tegra_nand_chip_parms {
	uint8_t vendor_id;
	uint8_t device_id;
	uint32_t flags;

	uint32_t capacity;

	/* all timing info is in nanoseconds */
	struct {
		uint32_t trp;
		uint32_t trh;
		uint32_t twp;
		uint32_t twh;
		uint32_t tcs;
		uint32_t twhr;
		uint32_t tcr_tar_trr;
		uint32_t twb;
		uint32_t trp_resp;
		uint32_t tadl;
	} timing;
};

struct tegra_nand_platform {
	uint8_t				max_chips;
	struct tegra_nand_chip_parms	*chip_parms;
	unsigned int			nr_chip_parms;
	struct mtd_partition		*parts;
	unsigned int			nr_parts;
};

#endif
