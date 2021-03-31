// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 */

#include "dsi_phy.h"
#include "dsi_pll.h"

/*
 * DSI PLL Helper functions
 */
long msm_dsi_pll_helper_clk_round_rate(struct clk_hw *hw,
		unsigned long rate, unsigned long *parent_rate)
{
	struct msm_dsi_pll *pll = hw_clk_to_pll(hw);

	if      (rate < pll->cfg->min_pll_rate)
		return  pll->cfg->min_pll_rate;
	else if (rate > pll->cfg->max_pll_rate)
		return  pll->cfg->max_pll_rate;
	else
		return rate;
}
