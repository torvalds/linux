/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 */

#ifndef __QCOM_CLK_REGMAP_DIVIDER_H__
#define __QCOM_CLK_REGMAP_DIVIDER_H__

#include <linux/clk-provider.h>
#include "clk-regmap.h"

struct clk_regmap_div {
	u32			reg;
	u32			shift;
	u32			width;
	struct clk_regmap	clkr;
};

extern const struct clk_ops clk_regmap_div_ops;
extern const struct clk_ops clk_regmap_div_ro_ops;

#endif
