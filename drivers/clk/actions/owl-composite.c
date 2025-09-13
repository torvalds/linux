// SPDX-License-Identifier: GPL-2.0+
//
// OWL composite clock driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#include <linux/clk-provider.h>
#include <linux/regmap.h>

#include "owl-composite.h"

static u8 owl_comp_get_parent(struct clk_hw *hw)
{
	struct owl_composite *comp = hw_to_owl_comp(hw);

	return owl_mux_helper_get_parent(&comp->common, &comp->mux_hw);
}

static int owl_comp_set_parent(struct clk_hw *hw, u8 index)
{
	struct owl_composite *comp = hw_to_owl_comp(hw);

	return owl_mux_helper_set_parent(&comp->common, &comp->mux_hw, index);
}

static void owl_comp_disable(struct clk_hw *hw)
{
	struct owl_composite *comp = hw_to_owl_comp(hw);
	struct owl_clk_common *common = &comp->common;

	owl_gate_set(common, &comp->gate_hw, false);
}

static int owl_comp_enable(struct clk_hw *hw)
{
	struct owl_composite *comp = hw_to_owl_comp(hw);
	struct owl_clk_common *common = &comp->common;

	owl_gate_set(common, &comp->gate_hw, true);

	return 0;
}

static int owl_comp_is_enabled(struct clk_hw *hw)
{
	struct owl_composite *comp = hw_to_owl_comp(hw);
	struct owl_clk_common *common = &comp->common;

	return owl_gate_clk_is_enabled(common, &comp->gate_hw);
}

static int owl_comp_div_determine_rate(struct clk_hw *hw,
				       struct clk_rate_request *req)
{
	struct owl_composite *comp = hw_to_owl_comp(hw);
	long rate;

	rate = owl_divider_helper_round_rate(&comp->common, &comp->rate.div_hw,
					     req->rate, &req->best_parent_rate);
	if (rate < 0)
		return rate;

	req->rate = rate;
	return 0;
}

static unsigned long owl_comp_div_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct owl_composite *comp = hw_to_owl_comp(hw);

	return owl_divider_helper_recalc_rate(&comp->common, &comp->rate.div_hw,
					parent_rate);
}

static int owl_comp_div_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct owl_composite *comp = hw_to_owl_comp(hw);

	return owl_divider_helper_set_rate(&comp->common, &comp->rate.div_hw,
					rate, parent_rate);
}

static int owl_comp_fact_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req)
{
	struct owl_composite *comp = hw_to_owl_comp(hw);
	long rate;

	rate = owl_factor_helper_round_rate(&comp->common,
					    &comp->rate.factor_hw,
					    req->rate, &req->best_parent_rate);
	if (rate < 0)
		return rate;

	req->rate = rate;
	return 0;
}

static unsigned long owl_comp_fact_recalc_rate(struct clk_hw *hw,
			unsigned long parent_rate)
{
	struct owl_composite *comp = hw_to_owl_comp(hw);

	return owl_factor_helper_recalc_rate(&comp->common,
					&comp->rate.factor_hw,
					parent_rate);
}

static int owl_comp_fact_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct owl_composite *comp = hw_to_owl_comp(hw);

	return owl_factor_helper_set_rate(&comp->common,
					&comp->rate.factor_hw,
					rate, parent_rate);
}

static int owl_comp_fix_fact_determine_rate(struct clk_hw *hw,
					    struct clk_rate_request *req)
{
	struct owl_composite *comp = hw_to_owl_comp(hw);
	struct clk_fixed_factor *fix_fact_hw = &comp->rate.fix_fact_hw;

	return comp->fix_fact_ops->determine_rate(&fix_fact_hw->hw, req);
}

static unsigned long owl_comp_fix_fact_recalc_rate(struct clk_hw *hw,
			unsigned long parent_rate)
{
	struct owl_composite *comp = hw_to_owl_comp(hw);
	struct clk_fixed_factor *fix_fact_hw = &comp->rate.fix_fact_hw;

	return comp->fix_fact_ops->recalc_rate(&fix_fact_hw->hw, parent_rate);

}

static int owl_comp_fix_fact_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	/*
	 * We must report success but we can do so unconditionally because
	 * owl_comp_fix_fact_round_rate returns values that ensure this call is
	 * a nop.
	 */

	return 0;
}

const struct clk_ops owl_comp_div_ops = {
	/* mux_ops */
	.get_parent	= owl_comp_get_parent,
	.set_parent	= owl_comp_set_parent,

	/* gate_ops */
	.disable	= owl_comp_disable,
	.enable		= owl_comp_enable,
	.is_enabled	= owl_comp_is_enabled,

	/* div_ops */
	.determine_rate	= owl_comp_div_determine_rate,
	.recalc_rate	= owl_comp_div_recalc_rate,
	.set_rate	= owl_comp_div_set_rate,
};


const struct clk_ops owl_comp_fact_ops = {
	/* mux_ops */
	.get_parent	= owl_comp_get_parent,
	.set_parent	= owl_comp_set_parent,

	/* gate_ops */
	.disable	= owl_comp_disable,
	.enable		= owl_comp_enable,
	.is_enabled	= owl_comp_is_enabled,

	/* fact_ops */
	.determine_rate	= owl_comp_fact_determine_rate,
	.recalc_rate	= owl_comp_fact_recalc_rate,
	.set_rate	= owl_comp_fact_set_rate,
};

const struct clk_ops owl_comp_fix_fact_ops = {
	/* gate_ops */
	.disable	= owl_comp_disable,
	.enable		= owl_comp_enable,
	.is_enabled	= owl_comp_is_enabled,

	/* fix_fact_ops */
	.determine_rate = owl_comp_fix_fact_determine_rate,
	.recalc_rate	= owl_comp_fix_fact_recalc_rate,
	.set_rate	= owl_comp_fix_fact_set_rate,
};


const struct clk_ops owl_comp_pass_ops = {
	/* mux_ops */
	.determine_rate	= clk_hw_determine_rate_no_reparent,
	.get_parent	= owl_comp_get_parent,
	.set_parent	= owl_comp_set_parent,

	/* gate_ops */
	.disable	= owl_comp_disable,
	.enable		= owl_comp_enable,
	.is_enabled	= owl_comp_is_enabled,
};
