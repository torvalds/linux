/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __QCOM_CLK_REGMAP_MUX_H__
#define __QCOM_CLK_REGMAP_MUX_H__

#include <linux/clk-provider.h>
#include "clk-regmap.h"
#include "common.h"

struct clk_regmap_mux {
	u32			reg;
	u32			shift;
	u32			width;
	const struct parent_map	*parent_map;
	struct clk_regmap	clkr;
};

extern const struct clk_ops clk_regmap_mux_closest_ops;

#endif
