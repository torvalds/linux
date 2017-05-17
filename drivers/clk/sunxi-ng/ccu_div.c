/*
 * Copyright (C) 2016 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <linux/clk-provider.h>

#include "ccu_gate.h"
#include "ccu_div.h"

static unsigned long ccu_div_round_rate(struct ccu_mux_internal *mux,
					struct clk_hw *parent,
					unsigned long *parent_rate,
					unsigned long rate,
					void *data)
{
	struct ccu_div *cd = data;
	unsigned long val;

	/*
	 * We can't use divider_round_rate that assumes that there's
	 * several parents, while we might be called to evaluate
	 * several different parents.
	 */
	val = divider_get_val(rate, *parent_rate, cd->div.table, cd->div.width,
			      cd->div.flags);

	return divider_recalc_rate(&cd->common.hw, *parent_rate, val,
				   cd->div.table, cd->div.flags);
}

static void ccu_div_disable(struct clk_hw *hw)
{
	struct ccu_div *cd = hw_to_ccu_div(hw);

	return ccu_gate_helper_disable(&cd->common, cd->enable);
}

static int ccu_div_enable(struct clk_hw *hw)
{
	struct ccu_div *cd = hw_to_ccu_div(hw);

	return ccu_gate_helper_enable(&cd->common, cd->enable);
}

static int ccu_div_is_enabled(struct clk_hw *hw)
{
	struct ccu_div *cd = hw_to_ccu_div(hw);

	return ccu_gate_helper_is_enabled(&cd->common, cd->enable);
}

static unsigned long ccu_div_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct ccu_div *cd = hw_to_ccu_div(hw);
	unsigned long val;
	u32 reg;

	reg = readl(cd->common.base + cd->common.reg);
	val = reg >> cd->div.shift;
	val &= (1 << cd->div.width) - 1;

	ccu_mux_helper_adjust_parent_for_prediv(&cd->common, &cd->mux, -1,
						&parent_rate);

	return divider_recalc_rate(hw, parent_rate, val, cd->div.table,
				   cd->div.flags);
}

static int ccu_div_determine_rate(struct clk_hw *hw,
				struct clk_rate_request *req)
{
	struct ccu_div *cd = hw_to_ccu_div(hw);

	if (clk_hw_get_num_parents(hw) == 1) {
		req->rate = divider_round_rate(hw, req->rate,
					       &req->best_parent_rate,
					       cd->div.table,
					       cd->div.width,
					       cd->div.flags);

		req->best_parent_hw = clk_hw_get_parent(hw);

		return 0;
	}

	return ccu_mux_helper_determine_rate(&cd->common, &cd->mux,
					     req, ccu_div_round_rate, cd);
}

static int ccu_div_set_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long parent_rate)
{
	struct ccu_div *cd = hw_to_ccu_div(hw);
	unsigned long flags;
	unsigned long val;
	u32 reg;

	ccu_mux_helper_adjust_parent_for_prediv(&cd->common, &cd->mux, -1,
						&parent_rate);

	val = divider_get_val(rate, parent_rate, cd->div.table, cd->div.width,
			      cd->div.flags);

	spin_lock_irqsave(cd->common.lock, flags);

	reg = readl(cd->common.base + cd->common.reg);
	reg &= ~GENMASK(cd->div.width + cd->div.shift - 1, cd->div.shift);

	writel(reg | (val << cd->div.shift),
	       cd->common.base + cd->common.reg);

	spin_unlock_irqrestore(cd->common.lock, flags);

	return 0;
}

static u8 ccu_div_get_parent(struct clk_hw *hw)
{
	struct ccu_div *cd = hw_to_ccu_div(hw);

	return ccu_mux_helper_get_parent(&cd->common, &cd->mux);
}

static int ccu_div_set_parent(struct clk_hw *hw, u8 index)
{
	struct ccu_div *cd = hw_to_ccu_div(hw);

	return ccu_mux_helper_set_parent(&cd->common, &cd->mux, index);
}

const struct clk_ops ccu_div_ops = {
	.disable	= ccu_div_disable,
	.enable		= ccu_div_enable,
	.is_enabled	= ccu_div_is_enabled,

	.get_parent	= ccu_div_get_parent,
	.set_parent	= ccu_div_set_parent,

	.determine_rate	= ccu_div_determine_rate,
	.recalc_rate	= ccu_div_recalc_rate,
	.set_rate	= ccu_div_set_rate,
};
