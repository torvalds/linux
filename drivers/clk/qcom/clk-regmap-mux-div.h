/* SPDX-License-Identifier: GPL-2.0 */
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
 * @safe_src:	the safe source mux value we switch to, while the main PLL is
 *		reconfigured
 * @safe_div:	the safe divider value that we set, while the main PLL is
 *		reconfigured
 * @safe_freq:	When switching rates from A to B, the mux div clock will
 *		instead switch from A -> safe_freq -> B. This allows the
 *		mux_div clock to change rates while enabled, even if this
 *		behavior is not supported by the parent clocks.
 *		If changing the rate of parent A also causes the rate of
 *		parent B to change, then safe_freq must be defined.
 *		safe_freq is expected to have a source clock which is always
 *		on and runs at only one rate.
 * @parent_map: pointer to parent_map struct
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
	u32				safe_src;
	u32				safe_div;
	unsigned long			safe_freq;
	const struct parent_map		*parent_map;
	struct clk_regmap		clkr;
	struct clk			*pclk;
	struct notifier_block		clk_nb;
};

extern const struct clk_ops clk_regmap_mux_div_ops;
extern int mux_div_set_src_div(struct clk_regmap_mux_div *md, u32 src, u32 div);
int mux_div_get_src_div(struct clk_regmap_mux_div *md, u32 *src, u32 *div);

#endif
