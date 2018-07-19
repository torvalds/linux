// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017 AmLogic, Inc.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

/*
 * i2s master clock divider: The algorithm of the generic clk-divider used with
 * a very precise clock parent such as the mpll tends to select a low divider
 * factor. This gives poor results with this particular divider, especially with
 * high frequencies (> 100 MHz)
 *
 * This driver try to select the maximum possible divider with the rate the
 * upstream clock can provide.
 */

#include <linux/clk-provider.h>
#include "clkc.h"

static inline struct meson_clk_audio_div_data *
meson_clk_audio_div_data(struct clk_regmap *clk)
{
	return (struct meson_clk_audio_div_data *)clk->data;
}

static int _div_round(unsigned long parent_rate, unsigned long rate,
		      unsigned long flags)
{
	if (flags & CLK_DIVIDER_ROUND_CLOSEST)
		return DIV_ROUND_CLOSEST_ULL((u64)parent_rate, rate);

	return DIV_ROUND_UP_ULL((u64)parent_rate, rate);
}

static int _get_val(unsigned long parent_rate, unsigned long rate)
{
	return DIV_ROUND_UP_ULL((u64)parent_rate, rate) - 1;
}

static int _valid_divider(unsigned int width, int divider)
{
	int max_divider = 1 << width;

	return clamp(divider, 1, max_divider);
}

static unsigned long audio_divider_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_audio_div_data *adiv = meson_clk_audio_div_data(clk);
	unsigned long divider;

	divider = meson_parm_read(clk->map, &adiv->div);

	return DIV_ROUND_UP_ULL((u64)parent_rate, divider);
}

static long audio_divider_round_rate(struct clk_hw *hw,
				     unsigned long rate,
				     unsigned long *parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_audio_div_data *adiv = meson_clk_audio_div_data(clk);
	unsigned long max_prate;
	int divider;

	if (!(clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT)) {
		divider = _div_round(*parent_rate, rate, adiv->flags);
		divider = _valid_divider(adiv->div.width, divider);
		return DIV_ROUND_UP_ULL((u64)*parent_rate, divider);
	}

	/* Get the maximum parent rate */
	max_prate = clk_hw_round_rate(clk_hw_get_parent(hw), ULONG_MAX);

	/* Get the corresponding rounded down divider */
	divider = max_prate / rate;
	divider = _valid_divider(adiv->div.width, divider);

	/* Get actual rate of the parent */
	*parent_rate = clk_hw_round_rate(clk_hw_get_parent(hw),
					 divider * rate);

	return DIV_ROUND_UP_ULL((u64)*parent_rate, divider);
}

static int audio_divider_set_rate(struct clk_hw *hw,
				  unsigned long rate,
				  unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_audio_div_data *adiv = meson_clk_audio_div_data(clk);
	int val = _get_val(parent_rate, rate);

	meson_parm_write(clk->map, &adiv->div, val);

	return 0;
}

const struct clk_ops meson_clk_audio_divider_ro_ops = {
	.recalc_rate	= audio_divider_recalc_rate,
	.round_rate	= audio_divider_round_rate,
};

const struct clk_ops meson_clk_audio_divider_ops = {
	.recalc_rate	= audio_divider_recalc_rate,
	.round_rate	= audio_divider_round_rate,
	.set_rate	= audio_divider_set_rate,
};
