// SPDX-License-Identifier: GPL-2.0
//
// Spreadtrum composite clock driver
//
// Copyright (C) 2017 Spreadtrum, Inc.
// Author: Chunyan Zhang <chunyan.zhang@spreadtrum.com>

#include <linux/clk-provider.h>

#include "composite.h"

static int sprd_comp_determine_rate(struct clk_hw *hw,
				    struct clk_rate_request *req)
{
	struct sprd_comp *cc = hw_to_sprd_comp(hw);

	return divider_determine_rate(hw, req, NULL, cc->div.width, 0);
}

static unsigned long sprd_comp_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct sprd_comp *cc = hw_to_sprd_comp(hw);

	return sprd_div_helper_recalc_rate(&cc->common, &cc->div, parent_rate);
}

static int sprd_comp_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	struct sprd_comp *cc = hw_to_sprd_comp(hw);

	return sprd_div_helper_set_rate(&cc->common, &cc->div,
				       rate, parent_rate);
}

static u8 sprd_comp_get_parent(struct clk_hw *hw)
{
	struct sprd_comp *cc = hw_to_sprd_comp(hw);

	return sprd_mux_helper_get_parent(&cc->common, &cc->mux);
}

static int sprd_comp_set_parent(struct clk_hw *hw, u8 index)
{
	struct sprd_comp *cc = hw_to_sprd_comp(hw);

	return sprd_mux_helper_set_parent(&cc->common, &cc->mux, index);
}

const struct clk_ops sprd_comp_ops = {
	.get_parent	= sprd_comp_get_parent,
	.set_parent	= sprd_comp_set_parent,

	.determine_rate	= sprd_comp_determine_rate,
	.recalc_rate	= sprd_comp_recalc_rate,
	.set_rate	= sprd_comp_set_rate,
};
EXPORT_SYMBOL_GPL(sprd_comp_ops);
