/*
 * Copyright (c) 2017 AmLogic, Inc.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
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

#define to_meson_clk_audio_divider(_hw) container_of(_hw, \
				struct meson_clk_audio_divider, hw)

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

static int _valid_divider(struct clk_hw *hw, int divider)
{
	struct meson_clk_audio_divider *adiv =
		to_meson_clk_audio_divider(hw);
	int max_divider;
	u8 width;

	width = adiv->div.width;
	max_divider = 1 << width;

	return clamp(divider, 1, max_divider);
}

static unsigned long audio_divider_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct meson_clk_audio_divider *adiv =
		to_meson_clk_audio_divider(hw);
	struct parm *p;
	unsigned long reg, divider;

	p = &adiv->div;
	reg = readl(adiv->base + p->reg_off);
	divider = PARM_GET(p->width, p->shift, reg) + 1;

	return DIV_ROUND_UP_ULL((u64)parent_rate, divider);
}

static long audio_divider_round_rate(struct clk_hw *hw,
				     unsigned long rate,
				     unsigned long *parent_rate)
{
	struct meson_clk_audio_divider *adiv =
		to_meson_clk_audio_divider(hw);
	unsigned long max_prate;
	int divider;

	if (!(clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT)) {
		divider = _div_round(*parent_rate, rate, adiv->flags);
		divider = _valid_divider(hw, divider);
		return DIV_ROUND_UP_ULL((u64)*parent_rate, divider);
	}

	/* Get the maximum parent rate */
	max_prate = clk_hw_round_rate(clk_hw_get_parent(hw), ULONG_MAX);

	/* Get the corresponding rounded down divider */
	divider = max_prate / rate;
	divider = _valid_divider(hw, divider);

	/* Get actual rate of the parent */
	*parent_rate = clk_hw_round_rate(clk_hw_get_parent(hw),
					 divider * rate);

	return DIV_ROUND_UP_ULL((u64)*parent_rate, divider);
}

static int audio_divider_set_rate(struct clk_hw *hw,
				  unsigned long rate,
				  unsigned long parent_rate)
{
	struct meson_clk_audio_divider *adiv =
		to_meson_clk_audio_divider(hw);
	struct parm *p;
	unsigned long reg, flags = 0;
	int val;

	val = _get_val(parent_rate, rate);

	if (adiv->lock)
		spin_lock_irqsave(adiv->lock, flags);
	else
		__acquire(adiv->lock);

	p = &adiv->div;
	reg = readl(adiv->base + p->reg_off);
	reg = PARM_SET(p->width, p->shift, reg, val);
	writel(reg, adiv->base + p->reg_off);

	if (adiv->lock)
		spin_unlock_irqrestore(adiv->lock, flags);
	else
		__release(adiv->lock);

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
