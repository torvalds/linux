/*
 * Copyright (c) 2013 NVIDIA CORPORATION.  All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/slab.h>

#define to_clk_composite(_hw) container_of(_hw, struct clk_composite, hw)

static u8 clk_composite_get_parent(struct clk_hw *hw)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *mux_ops = composite->mux_ops;
	struct clk_hw *mux_hw = composite->mux_hw;

	mux_hw->clk = hw->clk;

	return mux_ops->get_parent(mux_hw);
}

static int clk_composite_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *mux_ops = composite->mux_ops;
	struct clk_hw *mux_hw = composite->mux_hw;

	mux_hw->clk = hw->clk;

	return mux_ops->set_parent(mux_hw, index);
}

static unsigned long clk_composite_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *rate_ops = composite->rate_ops;
	struct clk_hw *rate_hw = composite->rate_hw;

	rate_hw->clk = hw->clk;

	return rate_ops->recalc_rate(rate_hw, parent_rate);
}

static long clk_composite_determine_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *best_parent_rate,
					struct clk **best_parent_p)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *rate_ops = composite->rate_ops;
	const struct clk_ops *mux_ops = composite->mux_ops;
	struct clk_hw *rate_hw = composite->rate_hw;
	struct clk_hw *mux_hw = composite->mux_hw;

	if (rate_hw && rate_ops && rate_ops->determine_rate) {
		rate_hw->clk = hw->clk;
		return rate_ops->determine_rate(rate_hw, rate, best_parent_rate,
						best_parent_p);
	} else if (mux_hw && mux_ops && mux_ops->determine_rate) {
		mux_hw->clk = hw->clk;
		return mux_ops->determine_rate(rate_hw, rate, best_parent_rate,
					       best_parent_p);
	} else {
		pr_err("clk: clk_composite_determine_rate function called, but no mux or rate callback set!\n");
		return 0;
	}
}

static long clk_composite_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *prate)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *rate_ops = composite->rate_ops;
	struct clk_hw *rate_hw = composite->rate_hw;

	rate_hw->clk = hw->clk;

	return rate_ops->round_rate(rate_hw, rate, prate);
}

static int clk_composite_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *rate_ops = composite->rate_ops;
	struct clk_hw *rate_hw = composite->rate_hw;

	rate_hw->clk = hw->clk;

	return rate_ops->set_rate(rate_hw, rate, parent_rate);
}

static int clk_composite_is_enabled(struct clk_hw *hw)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *gate_ops = composite->gate_ops;
	struct clk_hw *gate_hw = composite->gate_hw;

	gate_hw->clk = hw->clk;

	return gate_ops->is_enabled(gate_hw);
}

static int clk_composite_enable(struct clk_hw *hw)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *gate_ops = composite->gate_ops;
	struct clk_hw *gate_hw = composite->gate_hw;

	gate_hw->clk = hw->clk;

	return gate_ops->enable(gate_hw);
}

static void clk_composite_disable(struct clk_hw *hw)
{
	struct clk_composite *composite = to_clk_composite(hw);
	const struct clk_ops *gate_ops = composite->gate_ops;
	struct clk_hw *gate_hw = composite->gate_hw;

	gate_hw->clk = hw->clk;

	gate_ops->disable(gate_hw);
}

struct clk *clk_register_composite(struct device *dev, const char *name,
			const char **parent_names, int num_parents,
			struct clk_hw *mux_hw, const struct clk_ops *mux_ops,
			struct clk_hw *rate_hw, const struct clk_ops *rate_ops,
			struct clk_hw *gate_hw, const struct clk_ops *gate_ops,
			unsigned long flags)
{
	struct clk *clk;
	struct clk_init_data init;
	struct clk_composite *composite;
	struct clk_ops *clk_composite_ops;

	composite = kzalloc(sizeof(*composite), GFP_KERNEL);
	if (!composite) {
		pr_err("%s: could not allocate composite clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	clk_composite_ops = &composite->ops;

	if (mux_hw && mux_ops) {
		if (!mux_ops->get_parent || !mux_ops->set_parent) {
			clk = ERR_PTR(-EINVAL);
			goto err;
		}

		composite->mux_hw = mux_hw;
		composite->mux_ops = mux_ops;
		clk_composite_ops->get_parent = clk_composite_get_parent;
		clk_composite_ops->set_parent = clk_composite_set_parent;
		if (mux_ops->determine_rate)
			clk_composite_ops->determine_rate = clk_composite_determine_rate;
	}

	if (rate_hw && rate_ops) {
		if (!rate_ops->recalc_rate) {
			clk = ERR_PTR(-EINVAL);
			goto err;
		}

		/* .round_rate is a prerequisite for .set_rate */
		if (rate_ops->round_rate) {
			clk_composite_ops->round_rate = clk_composite_round_rate;
			if (rate_ops->set_rate) {
				clk_composite_ops->set_rate = clk_composite_set_rate;
			}
		} else {
			WARN(rate_ops->set_rate,
				"%s: missing round_rate op is required\n",
				__func__);
		}

		composite->rate_hw = rate_hw;
		composite->rate_ops = rate_ops;
		clk_composite_ops->recalc_rate = clk_composite_recalc_rate;
		if (rate_ops->determine_rate)
			clk_composite_ops->determine_rate = clk_composite_determine_rate;
	}

	if (gate_hw && gate_ops) {
		if (!gate_ops->is_enabled || !gate_ops->enable ||
		    !gate_ops->disable) {
			clk = ERR_PTR(-EINVAL);
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

	clk = clk_register(dev, &composite->hw);
	if (IS_ERR(clk))
		goto err;

	if (composite->mux_hw)
		composite->mux_hw->clk = clk;

	if (composite->rate_hw)
		composite->rate_hw->clk = clk;

	if (composite->gate_hw)
		composite->gate_hw->clk = clk;

	return clk;

err:
	kfree(composite);
	return clk;
}
