/*
 * Copyright (C) 2016 Free Electrons
 * Copyright (C) 2016 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <linux/clk-provider.h>

#include "sun4i_tcon.h"
#include "sun4i_hdmi.h"

struct sun4i_tmds {
	struct clk_hw		hw;
	struct sun4i_hdmi	*hdmi;
};

static inline struct sun4i_tmds *hw_to_tmds(struct clk_hw *hw)
{
	return container_of(hw, struct sun4i_tmds, hw);
}


static unsigned long sun4i_tmds_calc_divider(unsigned long rate,
					     unsigned long parent_rate,
					     u8 *div,
					     bool *half)
{
	unsigned long best_rate = 0;
	u8 best_m = 0, m;
	bool is_double;

	for (m = 1; m < 16; m++) {
		u8 d;

		for (d = 1; d < 3; d++) {
			unsigned long tmp_rate;

			tmp_rate = parent_rate / m / d;

			if (tmp_rate > rate)
				continue;

			if (!best_rate ||
			    (rate - tmp_rate) < (rate - best_rate)) {
				best_rate = tmp_rate;
				best_m = m;
				is_double = d;
			}
		}
	}

	if (div && half) {
		*div = best_m;
		*half = is_double;
	}

	return best_rate;
}


static int sun4i_tmds_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	struct clk_hw *parent = NULL;
	unsigned long best_parent = 0;
	unsigned long rate = req->rate;
	int best_div = 1, best_half = 1;
	int i, j, p;

	/*
	 * We only consider PLL3, since the TCON is very likely to be
	 * clocked from it, and to have the same rate than our HDMI
	 * clock, so we should not need to do anything.
	 */

	for (p = 0; p < clk_hw_get_num_parents(hw); p++) {
		parent = clk_hw_get_parent_by_index(hw, p);
		if (!parent)
			continue;

		for (i = 1; i < 3; i++) {
			for (j = 1; j < 16; j++) {
				unsigned long ideal = rate * i * j;
				unsigned long rounded;

				rounded = clk_hw_round_rate(parent, ideal);

				if (rounded == ideal) {
					best_parent = rounded;
					best_half = i;
					best_div = j;
					goto out;
				}

				if (abs(rate - rounded / i) <
				    abs(rate - best_parent / best_div)) {
					best_parent = rounded;
					best_div = i;
				}
			}
		}
	}

	if (!parent)
		return -EINVAL;

out:
	req->rate = best_parent / best_half / best_div;
	req->best_parent_rate = best_parent;
	req->best_parent_hw = parent;

	return 0;
}

static unsigned long sun4i_tmds_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct sun4i_tmds *tmds = hw_to_tmds(hw);
	u32 reg;

	reg = readl(tmds->hdmi->base + SUN4I_HDMI_PAD_CTRL1_REG);
	if (reg & SUN4I_HDMI_PAD_CTRL1_HALVE_CLK)
		parent_rate /= 2;

	reg = readl(tmds->hdmi->base + SUN4I_HDMI_PLL_CTRL_REG);
	reg = (reg >> 4) & 0xf;
	if (!reg)
		reg = 1;

	return parent_rate / reg;
}

static int sun4i_tmds_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct sun4i_tmds *tmds = hw_to_tmds(hw);
	bool half;
	u32 reg;
	u8 div;

	sun4i_tmds_calc_divider(rate, parent_rate, &div, &half);

	reg = readl(tmds->hdmi->base + SUN4I_HDMI_PAD_CTRL1_REG);
	reg &= ~SUN4I_HDMI_PAD_CTRL1_HALVE_CLK;
	if (half)
		reg |= SUN4I_HDMI_PAD_CTRL1_HALVE_CLK;
	writel(reg, tmds->hdmi->base + SUN4I_HDMI_PAD_CTRL1_REG);

	reg = readl(tmds->hdmi->base + SUN4I_HDMI_PLL_CTRL_REG);
	reg &= ~SUN4I_HDMI_PLL_CTRL_DIV_MASK;
	writel(reg | SUN4I_HDMI_PLL_CTRL_DIV(div),
	       tmds->hdmi->base + SUN4I_HDMI_PLL_CTRL_REG);

	return 0;
}

static u8 sun4i_tmds_get_parent(struct clk_hw *hw)
{
	struct sun4i_tmds *tmds = hw_to_tmds(hw);
	u32 reg;

	reg = readl(tmds->hdmi->base + SUN4I_HDMI_PLL_DBG0_REG);
	return ((reg & SUN4I_HDMI_PLL_DBG0_TMDS_PARENT_MASK) >>
		SUN4I_HDMI_PLL_DBG0_TMDS_PARENT_SHIFT);
}

static int sun4i_tmds_set_parent(struct clk_hw *hw, u8 index)
{
	struct sun4i_tmds *tmds = hw_to_tmds(hw);
	u32 reg;

	if (index > 1)
		return -EINVAL;

	reg = readl(tmds->hdmi->base + SUN4I_HDMI_PLL_DBG0_REG);
	reg &= ~SUN4I_HDMI_PLL_DBG0_TMDS_PARENT_MASK;
	writel(reg | SUN4I_HDMI_PLL_DBG0_TMDS_PARENT(index),
	       tmds->hdmi->base + SUN4I_HDMI_PLL_DBG0_REG);

	return 0;
}

static const struct clk_ops sun4i_tmds_ops = {
	.determine_rate	= sun4i_tmds_determine_rate,
	.recalc_rate	= sun4i_tmds_recalc_rate,
	.set_rate	= sun4i_tmds_set_rate,

	.get_parent	= sun4i_tmds_get_parent,
	.set_parent	= sun4i_tmds_set_parent,
};

int sun4i_tmds_create(struct sun4i_hdmi *hdmi)
{
	struct clk_init_data init;
	struct sun4i_tmds *tmds;
	const char *parents[2];

	parents[0] = __clk_get_name(hdmi->pll0_clk);
	if (!parents[0])
		return -ENODEV;

	parents[1] = __clk_get_name(hdmi->pll1_clk);
	if (!parents[1])
		return -ENODEV;

	tmds = devm_kzalloc(hdmi->dev, sizeof(*tmds), GFP_KERNEL);
	if (!tmds)
		return -ENOMEM;

	init.name = "hdmi-tmds";
	init.ops = &sun4i_tmds_ops;
	init.parent_names = parents;
	init.num_parents = 2;
	init.flags = CLK_SET_RATE_PARENT;

	tmds->hdmi = hdmi;
	tmds->hw.init = &init;

	hdmi->tmds_clk = devm_clk_register(hdmi->dev, &tmds->hw);
	if (IS_ERR(hdmi->tmds_clk))
		return PTR_ERR(hdmi->tmds_clk);

	return 0;
}
