// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2013 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/slab.h>

static u8 clk_composite_get_parent(struct clk_hw *hw)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *mux_ops = composite->mux_ops;
	struct clk_hw *mux_hw = composite->mux_hw;

	__clk_hw_set_clk(mux_hw, hw);

	return mux_ops->get_parent(mux_hw);
}

static int clk_composite_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *mux_ops = composite->mux_ops;
	struct clk_hw *mux_hw = composite->mux_hw;

	__clk_hw_set_clk(mux_hw, hw);

	return mux_ops->set_parent(mux_hw, index);
}

static unsigned long clk_composite_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *rate_ops = composite->rate_ops;
	struct clk_hw *rate_hw = composite->rate_hw;

	__clk_hw_set_clk(rate_hw, hw);

	return rate_ops->recalc_rate(rate_hw, parent_rate);
}

static int clk_composite_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req)
{
	struct clk_composite *composite = to_clk_composite(hw);
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
	int i;

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

			if (!rate_diff || !req->best_parent_hw
				       || best_rate_diff > rate_diff) {
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
		pr_err("clk: clk_composite_determine_rate function called, but no mux or rate callback set!\n");
		return -EINVAL;
	}
}

static long clk_composite_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *prate)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *rate_ops = composite->rate_ops;
	struct clk_hw *rate_hw = composite->rate_hw;

	__clk_hw_set_clk(rate_hw, hw);

	return rate_ops->round_rate(rate_hw, rate, prate);
}

static int clk_composite_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *rate_ops = composite->rate_ops;
	struct clk_hw *rate_hw = composite->rate_hw;

	__clk_hw_set_clk(rate_hw, hw);

	return rate_ops->set_rate(rate_hw, rate, parent_rate);
}

static int clk_composite_set_rate_and_parent(struct clk_hw *hw,
					     unsigned long rate,
					     unsigned long parent_rate,
					     u8 index)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *rate_ops = composite->rate_ops;
	const struct clk_ops *mux_ops = composite->mux_ops;
	struct clk_hw *rate_hw = composite->rate_hw;
	struct clk_hw *mux_hw = composite->mux_hw;
	unsigned long temp_rate;

	__clk_hw_set_clk(rate_hw, hw);
	__clk_hw_set_clk(mux_hw, hw);

	temp_rate = rate_ops->recalc_rate(rate_hw, parent_rate);
	if (temp_rate > rate) {
		rate_ops->set_rate(rate_hw, rate, parent_rate);
		mux_ops->set_parent(mux_hw, index);
	} else {
		mux_ops->set_parent(mux_hw, index);
		rate_ops->set_rate(rate_hw, rate, parent_rate);
	}

	return 0;
}

static int clk_composite_is_enabled(struct clk_hw *hw)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *gate_ops = composite->gate_ops;
	struct clk_hw *gate_hw = composite->gate_hw;

	__clk_hw_set_clk(gate_hw, hw);

	return gate_ops->is_enabled(gate_hw);
}

static int clk_composite_enable(struct clk_hw *hw)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *gate_ops = composite->gate_ops;
	struct clk_hw *gate_hw = composite->gate_hw;

	__clk_hw_set_clk(gate_hw, hw);

	return gate_ops->enable(gate_hw);
}

static void clk_composite_disable(struct clk_hw *hw)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *gate_ops = composite->gate_ops;
	struct clk_hw *gate_hw = composite->gate_hw;

	__clk_hw_set_clk(gate_hw, hw);

	gate_ops->disable(gate_hw);
}

struct clk_hw *clk_hw_register_composite(struct device *dev, const char *name,
			const char * const *parent_names, int num_parents,
			struct clk_hw *mux_hw, const struct clk_ops *mux_ops,
			struct clk_hw *rate_hw, const struct clk_ops *rate_ops,
			struct clk_hw *gate_hw, const struct clk_ops *gate_ops,
			unsigned long flags)
{
	struct clk_hw *hw;
	struct clk_init_data init = {};
	struct clk_composite *composite;
	struct clk_ops *clk_composite_ops;
	int ret;

	composite = kzalloc(sizeof(*composite), GFP_KERNEL);
	if (!composite)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.flags = flags;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	hw = &composite->hw;

	clk_composite_ops = &composite->ops;

	if (mux_hw && mux_ops) {
		if (!mux_ops->get_parent) {
			hw = ERR_PTR(-EINVAL);
			goto err;
		}

		composite->mux_hw = mux_hw;
		composite->mux_ops = mux_ops;
		clk_composite_ops->get_parent = clk_composite_get_parent;
		if (mux_ops->set_parent)
			clk_composite_ops->set_parent = clk_composite_set_parent;
		if (mux_ops->determine_rate)
			clk_composite_ops->determine_rate = clk_composite_determine_rate;
	}

	if (rate_hw && rate_ops) {
		if (!rate_ops->recalc_rate) {
			hw = ERR_PTR(-EINVAL);
			goto err;
		}
		clk_composite_ops->recalc_rate = clk_composite_recalc_rate;

		if (rate_ops->determine_rate)
			clk_composite_ops->determine_rate =
				clk_composite_determine_rate;
		else if (rate_ops->round_rate)
			clk_composite_ops->round_rate =
				clk_composite_round_rate;

		/* .set_rate requires either .round_rate or .determine_rate */
		if (rate_ops->set_rate) {
			if (rate_ops->determine_rate || rate_ops->round_rate)
				clk_composite_ops->set_rate =
						clk_composite_set_rate;
			else
				WARN(1, "%s: missing round_rate op is required\n",
						__func__);
		}

		composite->rate_hw = rate_hw;
		composite->rate_ops = rate_ops;
	}

	if (mux_hw && mux_ops && rate_hw && rate_ops) {
		if (mux_ops->set_parent && rate_ops->set_rate)
			clk_composite_ops->set_rate_and_parent =
			clk_composite_set_rate_and_parent;
	}

	if (gate_hw && gate_ops) {
		if (!gate_ops->is_enabled || !gate_ops->enable ||
		    !gate_ops->disable) {
			hw = ERR_PTR(-EINVAL);
			goto err;
		}

		composite->gate_hw = gate_hw;
		composite->gate_ops = gate_ops;
		clk_composite_ops->is_enabled = clk_composite_is_enabled;
		clk_composite_ops->enable = clk_composite_enable;
		clk_composite_ops->disable = clk_composite_disable;
	}

	init.ops = clk_composite_ops;
	composite->hw.init = &init;

	ret = clk_hw_register(dev, hw);
	if (ret) {
		hw = ERR_PTR(ret);
		goto err;
	}

	if (composite->mux_hw)
		composite->mux_hw->clk = hw->clk;

	if (composite->rate_hw)
		composite->rate_hw->clk = hw->clk;

	if (composite->gate_hw)
		composite->gate_hw->clk = hw->clk;

	return hw;

err:
	kfree(composite);
	return hw;
}

struct clk *clk_register_composite(struct device *dev, const char *name,
			const char * const *parent_names, int num_parents,
			struct clk_hw *mux_hw, const struct clk_ops *mux_ops,
			struct clk_hw *rate_hw, const struct clk_ops *rate_ops,
			struct clk_hw *gate_hw, const struct clk_ops *gate_ops,
			unsigned long flags)
{
	struct clk_hw *hw;

	hw = clk_hw_register_composite(dev, name, parent_names, num_parents,
			mux_hw, mux_ops, rate_hw, rate_ops, gate_hw, gate_ops,
			flags);
	if (IS_ERR(hw))
		return ERR_CAST(hw);
	return hw->clk;
}

void clk_unregister_composite(struct clk *clk)
{
	struct clk_composite *composite;
	struct clk_hw *hw;

	hw = __clk_get_hw(clk);
	if (!hw)
		return;

	composite = to_clk_composite(hw);

	clk_unregister(clk);
	kfree(composite);
}

void clk_hw_unregister_composite(struct clk_hw *hw)
{
	struct clk_composite *composite;

	composite = to_clk_composite(hw);

	clk_hw_unregister(hw);
	kfree(composite);
}
EXPORT_SYMBOL_GPL(clk_hw_unregister_composite);
