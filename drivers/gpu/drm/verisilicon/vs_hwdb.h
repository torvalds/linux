/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 */

#ifndef _VS_HWDB_H_
#define _VS_HWDB_H_

#include <linux/regmap.h>
#include <linux/types.h>

struct vs_formats {
	const u32 *array;
	unsigned int num;
};

struct vs_chip_identity {
	u32 model;
	u32 revision;
	u32 customer_id;

	u32 display_count;
	const struct vs_formats *formats;
};

int vs_fill_chip_identity(struct regmap *regs,
			  struct vs_chip_identity *ident);

#endif /* _VS_HWDB_H_ */
