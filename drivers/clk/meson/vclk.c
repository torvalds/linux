// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 Neil Armstrong <neil.armstrong@linaro.org>
 */

#include <linux/module.h>
#include "vclk.h"

/* The VCLK gate has a supplementary reset bit to pulse after ungating */

static inline struct meson_vclk_gate_data *
clk_get_meson_vclk_gate_data(struct clk_regmap *clk)
{
	return (struct meson_vclk_gate_data *)clk->data;
}

static int meson_vclk_gate_enable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_vclk_gate_data *vclk = clk_get_meson_vclk_gate_data(clk);

	meson_parm_write(clk->map, &vclk->enable, 1);

	/* Do a reset pulse */
	meson_parm_write(clk->map, &vclk->reset, 1);
	meson_parm_write(clk->map, &vclk->reset, 0);

	return 0;
}

static void meson_vclk_gate_disable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_vclk_gate_data *vclk = clk_get_meson_vclk_gate_data(clk);

	meson_parm_write(clk->map, &vclk->enable, 0);
}

static int meson_vclk_gate_is_enabled(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_vclk_gate_data *vclk = clk_get_meson_vclk_gate_data(clk);

	return meson_parm_read(clk->map, &vclk->enable);
}

const struct clk_ops meson_vclk_gate_ops = {
	.enable = meson_vclk_gate_enable,
	.disable = meson_vclk_gate_disable,
	.is_enabled = meson_vclk_gate_is_enabled,
};
EXPORT_SYMBOL_GPL(meson_vclk_gate_ops);

/* The VCLK Divider has supplementary reset & enable bits */

static inline struct meson_vclk_div_data *
clk_get_meson_vclk_div_data(struct clk_regmap *clk)
{
	return (struct meson_vclk_div_data *)clk->data;
}

static unsigned long meson_vclk_div_recalc_rate(struct clk_hw *hw,
						unsigned long prate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_vclk_div_data *vclk = clk_get_meson_vclk_div_data(clk);

	return divider_recalc_rate(hw, prate, meson_parm_read(clk->map, &vclk->div),
				   vclk->table, vclk->flags, vclk->div.width);
}

static int meson_vclk_div_determine_rate(struct clk_hw *hw,
					 struct clk_rate_request *req)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_vclk_div_data *vclk = clk_get_meson_vclk_div_data(clk);

	return divider_determine_rate(hw, req, vclk->table, vclk->div.width,
				      vclk->flags);
}

static int meson_vclk_div_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_vclk_div_data *vclk = clk_get_meson_vclk_div_data(clk);
	int ret;

	ret = divider_get_val(rate, parent_rate, vclk->table, vclk->div.width,
			      vclk->flags);
	if (ret < 0)
		return ret;

	meson_parm_write(clk->map, &vclk->div, ret);

	return 0;
};

static int meson_vclk_div_enable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_vclk_div_data *vclk = clk_get_meson_vclk_div_data(clk);

	/* Unreset the divider when ungating */
	meson_parm_write(clk->map, &vclk->reset, 0);
	meson_parm_write(clk->map, &vclk->enable, 1);

	return 0;
}

static void meson_vclk_div_disable(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_vclk_div_data *vclk = clk_get_meson_vclk_div_data(clk);

	/* Reset the divider when gating */
	meson_parm_write(clk->map, &vclk->enable, 0);
	meson_parm_write(clk->map, &vclk->reset, 1);
}

static int meson_vclk_div_is_enabled(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_vclk_div_data *vclk = clk_get_meson_vclk_div_data(clk);

	return meson_parm_read(clk->map, &vclk->enable);
}

const struct clk_ops meson_vclk_div_ops = {
	.recalc_rate = meson_vclk_div_recalc_rate,
	.determine_rate = meson_vclk_div_determine_rate,
	.set_rate = meson_vclk_div_set_rate,
	.enable = meson_vclk_div_enable,
	.disable = meson_vclk_div_disable,
	.is_enabled = meson_vclk_div_is_enabled,
};
EXPORT_SYMBOL_GPL(meson_vclk_div_ops);

MODULE_DESCRIPTION("Amlogic vclk clock driver");
MODULE_AUTHOR("Neil Armstrong <neil.armstrong@linaro.org>");
MODULE_LICENSE("GPL");
