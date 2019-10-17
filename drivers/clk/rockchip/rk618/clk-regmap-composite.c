/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * Base on code in drivers/clk/clk-composite.c.
 * See clk-composite.c for further copyright information.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "clk-regmap.h"

struct clk_regmap_composite {
	struct device *dev;
	struct clk_hw hw;
	struct clk_ops ops;

	struct clk_hw *mux_hw;
	struct clk_hw *rate_hw;
	struct clk_hw *gate_hw;

	const struct clk_ops *mux_ops;
	const struct clk_ops *rate_ops;
	const struct clk_ops *gate_ops;
};

#define to_clk_regmap_composite(_hw)	\
		container_of(_hw, struct clk_regmap_composite, hw)

static u8 clk_regmap_composite_get_parent(struct clk_hw *hw)
{
	struct clk_regmap_composite *composite = to_clk_regmap_composite(hw);
	const struct clk_ops *mux_ops = composite->mux_ops;
	struct clk_hw *mux_hw = composite->mux_hw;

	__clk_hw_set_clk(mux_hw, hw);

	return mux_ops->get_parent(mux_hw);
}

static int clk_regmap_composite_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_regmap_composite *composite = to_clk_regmap_composite(hw);
	const struct clk_ops *mux_ops = composite->mux_ops;
	struct clk_hw *mux_hw = composite->mux_hw;

	__clk_hw_set_clk(mux_hw, hw);

	return mux_ops->set_parent(mux_hw, index);
}

static unsigned long clk_regmap_composite_recalc_rate(struct clk_hw *hw,
						      unsigned long parent_rate)
{
	struct clk_regmap_composite *composite = to_clk_regmap_composite(hw);
	const struct clk_ops *rate_ops = composite->rate_ops;
	struct clk_hw *rate_hw = composite->rate_hw;

	__clk_hw_set_clk(rate_hw, hw);

	return rate_ops->recalc_rate(rate_hw, parent_rate);
}

static int clk_regmap_composite_determine_rate(struct clk_hw *hw,
					       struct clk_rate_request *req)
{
	struct clk_regmap_composite *composite = to_clk_regmap_composite(hw);
	const struct clk_ops *rate_ops = composite->rate_ops;
	const struct clk_ops *mux_ops = composite->mux_ops;
	struct clk_hw *rate_hw = composite->rate_hw;
	struct clk_hw *mux_hw = composite->mux_hw;
	struct clk_hw *parent;
	unsigned long parent_rate;
	long tmp_rate, best_rate = 0;
	unsigned long rate_diff;
	unsigned long best_rate_diff = ULONG_MAX;
	long rate;
	unsigned int i;

	if (rate_hw && rate_ops && rate_ops->determine_rate) {
		__clk_hw_set_clk(rate_hw, hw);
		return rate_ops->determine_rate(rate_hw, req);
	} else if (rate_hw && rate_ops && rate_ops->round_rate &&
		   mux_hw && mux_ops && mux_ops->set_parent) {
		req->best_parent_hw = NULL;

		if (clk_hw_get_flags(hw) & CLK_SET_RATE_NO_REPARENT) {
			parent = clk_hw_get_parent(mux_hw);
			req->best_parent_hw = parent;
			req->best_parent_rate = clk_hw_get_rate(parent);

			rate = rate_ops->round_rate(rate_hw, req->rate,
						    &req->best_parent_rate);
			if (rate < 0)
				return rate;

			req->rate = rate;
			return 0;
		}

		for (i = 0; i < clk_hw_get_num_parents(mux_hw); i++) {
			parent = clk_hw_get_parent_by_index(mux_hw, i);
			if (!parent)
				continue;

			parent_rate = clk_hw_get_rate(parent);

			tmp_rate = rate_ops->round_rate(rate_hw, req->rate,
							&parent_rate);
			if (tmp_rate < 0)
				continue;

			rate_diff = abs(req->rate - tmp_rate);

			if (!rate_diff || !req->best_parent_hw ||
			    best_rate_diff > rate_diff) {
				req->best_parent_hw = parent;
				req->best_parent_rate = parent_rate;
				best_rate_diff = rate_diff;
				best_rate = tmp_rate;
			}

			if (!rate_diff)
				return 0;
		}

		req->rate = best_rate;
		return 0;
	} else if (mux_hw && mux_ops && mux_ops->determine_rate) {
		__clk_hw_set_clk(mux_hw, hw);
		return mux_ops->determine_rate(mux_hw, req);
	} else {
		return -EINVAL;
	}

	return 0;
}

static long clk_regmap_composite_round_rate(struct clk_hw *hw,
					    unsigned long rate,
					    unsigned long *prate)
{
	struct clk_regmap_composite *composite = to_clk_regmap_composite(hw);
	const struct clk_ops *rate_ops = composite->rate_ops;
	struct clk_hw *rate_hw = composite->rate_hw;

	__clk_hw_set_clk(rate_hw, hw);

	return rate_ops->round_rate(rate_hw, rate, prate);
}

static int clk_regmap_composite_set_rate(struct clk_hw *hw,
					 unsigned long rate,
					 unsigned long parent_rate)
{
	struct clk_regmap_composite *composite = to_clk_regmap_composite(hw);
	const struct clk_ops *rate_ops = composite->rate_ops;
	struct clk_hw *rate_hw = composite->rate_hw;

	__clk_hw_set_clk(rate_hw, hw);

	return rate_ops->set_rate(rate_hw, rate, parent_rate);
}

static int clk_regmap_composite_is_prepared(struct clk_hw *hw)
{
	struct clk_regmap_composite *composite = to_clk_regmap_composite(hw);
	const struct clk_ops *gate_ops = composite->gate_ops;
	struct clk_hw *gate_hw = composite->gate_hw;

	__clk_hw_set_clk(gate_hw, hw);

	return gate_ops->is_prepared(gate_hw);
}

static int clk_regmap_composite_prepare(struct clk_hw *hw)
{
	struct clk_regmap_composite *composite = to_clk_regmap_composite(hw);
	const struct clk_ops *gate_ops = composite->gate_ops;
	struct clk_hw *gate_hw = composite->gate_hw;

	__clk_hw_set_clk(gate_hw, hw);

	return gate_ops->prepare(gate_hw);
}

static void clk_regmap_composite_unprepare(struct clk_hw *hw)
{
	struct clk_regmap_composite *composite = to_clk_regmap_composite(hw);
	const struct clk_ops *gate_ops = composite->gate_ops;
	struct clk_hw *gate_hw = composite->gate_hw;

	__clk_hw_set_clk(gate_hw, hw);

	gate_ops->unprepare(gate_hw);
}

struct clk *
devm_clk_regmap_register_composite(struct device *dev, const char *name,
				   const char *const *parent_names,
				   u8 num_parents, struct regmap *regmap,
				   u32 mux_reg, u8 mux_shift, u8 mux_width,
				   u32 div_reg, u8 div_shift, u8 div_width,
				   u8 div_flags,
				   u32 gate_reg, u8 gate_shift,
				   unsigned long flags)
{
	struct clk_regmap_gate *gate = NULL;
	struct clk_regmap_mux *mux = NULL;
	struct clk_regmap_divider *div = NULL;
	struct clk_regmap_fractional_divider *fd = NULL;
	const struct clk_ops *mux_ops = NULL, *div_ops = NULL, *gate_ops = NULL;
	const struct clk_ops *fd_ops = NULL;
	struct clk_hw *mux_hw = NULL, *div_hw = NULL, *gate_hw = NULL;
	struct clk_hw *fd_hw = NULL;
	struct clk *clk;
	struct clk_init_data init = {};
	struct clk_regmap_composite *composite;
	struct clk_ops *clk_composite_ops;

	if (num_parents > 1) {
		mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
		if (!mux)
			return ERR_PTR(-ENOMEM);

		mux->dev = dev;
		mux->regmap = regmap;
		mux->reg = mux_reg;
		mux->shift = mux_shift;
		mux->mask = BIT(mux_width) - 1;
		mux_ops = &clk_regmap_mux_ops;
		mux_hw = &mux->hw;
	}

	if (gate_reg > 0) {
		gate = devm_kzalloc(dev, sizeof(*gate), GFP_KERNEL);
		if (!gate)
			return ERR_PTR(-ENOMEM);

		gate->dev = dev;
		gate->regmap = regmap;
		gate->reg = gate_reg;
		gate->shift = gate_shift;
		gate_ops = &clk_regmap_gate_ops;
		gate_hw = &gate->hw;
	}

	if (div_reg > 0) {
		if (div_flags & CLK_DIVIDER_HIWORD_MASK) {
			div = devm_kzalloc(dev, sizeof(*div), GFP_KERNEL);
			if (!div)
				return ERR_PTR(-ENOMEM);

			div->dev = dev;
			div->regmap = regmap;
			div->reg = div_reg;
			div->shift = div_shift;
			div->width = div_width;
			div_ops = &clk_regmap_divider_ops;
			div_hw = &div->hw;
		} else {
			fd = devm_kzalloc(dev, sizeof(*fd), GFP_KERNEL);
			if (!fd)
				return ERR_PTR(-ENOMEM);

			fd->dev = dev;
			fd->regmap = regmap;
			fd->reg = div_reg;
			fd->mshift = 16;
			fd->mwidth = 16;
			fd->mmask = GENMASK(fd->mwidth - 1, 0) << fd->mshift;
			fd->nshift = 0;
			fd->nwidth = 16;
			fd->nmask = GENMASK(fd->nwidth - 1, 0) << fd->nshift;
			fd_ops = &clk_regmap_fractional_divider_ops;
			fd_hw = &fd->hw;
		}
	}

	composite = devm_kzalloc(dev, sizeof(*composite), GFP_KERNEL);
	if (!composite)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.flags = flags;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	clk_composite_ops = &composite->ops;

	if (mux_hw && mux_ops) {
		if (!mux_ops->get_parent)
			return ERR_PTR(-EINVAL);

		composite->mux_hw = mux_hw;
		composite->mux_ops = mux_ops;
		clk_composite_ops->get_parent =
			clk_regmap_composite_get_parent;
		if (mux_ops->set_parent)
			clk_composite_ops->set_parent =
				clk_regmap_composite_set_parent;
		if (mux_ops->determine_rate)
			clk_composite_ops->determine_rate =
				clk_regmap_composite_determine_rate;
	}

	if (div_hw && div_ops) {
		if (!div_ops->recalc_rate)
			return ERR_PTR(-EINVAL);

		clk_composite_ops->recalc_rate =
			clk_regmap_composite_recalc_rate;

		if (div_ops->determine_rate)
			clk_composite_ops->determine_rate =
				clk_regmap_composite_determine_rate;
		else if (div_ops->round_rate)
			clk_composite_ops->round_rate =
				clk_regmap_composite_round_rate;

		/* .set_rate requires either .round_rate or .determine_rate */
		if (div_ops->set_rate) {
			if (div_ops->determine_rate || div_ops->round_rate)
				clk_composite_ops->set_rate =
					clk_regmap_composite_set_rate;
			else
				WARN(1, "missing round_rate op\n");
		}

		composite->rate_hw = div_hw;
		composite->rate_ops = div_ops;
	}

	if (fd_hw && fd_ops) {
		if (!fd_ops->recalc_rate)
			return ERR_PTR(-EINVAL);

		clk_composite_ops->recalc_rate =
			clk_regmap_composite_recalc_rate;

		if (fd_ops->determine_rate)
			clk_composite_ops->determine_rate =
				clk_regmap_composite_determine_rate;
		else if (fd_ops->round_rate)
			clk_composite_ops->round_rate =
				clk_regmap_composite_round_rate;

		/* .set_rate requires either .round_rate or .determine_rate */
		if (fd_ops->set_rate) {
			if (fd_ops->determine_rate || fd_ops->round_rate)
				clk_composite_ops->set_rate =
					clk_regmap_composite_set_rate;
			else
				WARN(1, "missing round_rate op\n");
		}

		composite->rate_hw = fd_hw;
		composite->rate_ops = fd_ops;
	}

	if (gate_hw && gate_ops) {
		if (!gate_ops->is_prepared || !gate_ops->prepare ||
		    !gate_ops->unprepare)
			return ERR_PTR(-EINVAL);

		composite->gate_hw = gate_hw;
		composite->gate_ops = gate_ops;
		clk_composite_ops->is_prepared =
			clk_regmap_composite_is_prepared;
		clk_composite_ops->prepare = clk_regmap_composite_prepare;
		clk_composite_ops->unprepare = clk_regmap_composite_unprepare;
	}

	init.ops = clk_composite_ops;
	composite->dev = dev;
	composite->hw.init = &init;

	clk = devm_clk_register(dev, &composite->hw);
	if (IS_ERR(clk))
		return clk;

	if (composite->mux_hw)
		composite->mux_hw->clk = clk;

	if (composite->rate_hw)
		composite->rate_hw->clk = clk;

	if (composite->gate_hw)
		composite->gate_hw->clk = clk;

	return clk;
}
EXPORT_SYMBOL_GPL(devm_clk_regmap_register_composite);
