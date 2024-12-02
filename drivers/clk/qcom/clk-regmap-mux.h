/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
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
