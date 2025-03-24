/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021-2024 Raspberry Pi Ltd.
 * Copyright (c) 2023-2024 Ideas on Board Oy
 */

#ifndef _RP1_DPHY_
#define _RP1_DPHY_

#include <linux/io.h>
#include <linux/types.h>

struct dphy_data {
	struct device *dev;

	void __iomem *base;

	u32 dphy_rate;
	u32 max_lanes;
	u32 active_lanes;
};

void dphy_probe(struct dphy_data *dphy);
void dphy_start(struct dphy_data *dphy);
void dphy_stop(struct dphy_data *dphy);

#endif
