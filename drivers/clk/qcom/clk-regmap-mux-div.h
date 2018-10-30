// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017, Linaro Limited
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#ifndef __QCOM_CLK_REGMAP_MUX_DIV_H__
#define __QCOM_CLK_REGMAP_MUX_DIV_H__

#include <linux/clk-provider.h>
#include "clk-regmap.h"

/**
 * struct mux_div_clk - combined mux/divider clock
 * @reg_offset: offset of the mux/divider register
 * @hid_width:	number of bits in half integer divider
 * @hid_shift:	lowest bit of hid value field
 * @src_width:	number of bits in source select
 * @src_shift:	lowest bit of source select field
 * @div:	the divider raw configuration value
 * @src:	the mux index which will be used if the clock is enabled
 * @parent_map: map from parent_names index to src_sel field
 * @clkr:	handle between common and hardware-specific interfaces
 * @pclk:	the input PLL clock
 * @clk_nb:	clock notifier for rate changes of the input PLL
 */
struct clk_regmap_mux_div {
	u32				reg_offset;
	u32				hid_width;
	u32				hid_shift;
	u32				src_width;
	u32				src_shift;
	u32				div;
	u32				src;
	const u32			*parent_map;
	struct clk_regmap		clkr;
	struct clk			*pclk;
	struct notifier_block		clk_nb;
};

extern const struct clk_ops clk_regmap_mux_div_ops;
extern int mux_div_set_src_div(struct clk_regmap_mux_div *md, u32 src, u32 div);

#endif
