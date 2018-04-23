/*
 * Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "dsi_pll.h"

static int dsi_pll_enable(struct msm_dsi_pll *pll)
{
	int i, ret = 0;

	/*
	 * Certain PLLs do not allow VCO rate update when it is on.
	 * Keep track of their status to turn on/off after set rate success.
	 */
	if (unlikely(pll->pll_on))
		return 0;

	/* Try all enable sequences until one succeeds */
	for (i = 0; i < pll->en_seq_cnt; i++) {
		ret = pll->enable_seqs[i](pll);
		DBG("DSI PLL %s after sequence #%d",
			ret ? "unlocked" : "locked", i + 1);
		if (!ret)
			break;
	}

	if (ret) {
		DRM_ERROR("DSI PLL failed to lock\n");
		return ret;
	}

	pll->pll_on = true;

	return 0;
}

static void dsi_pll_disable(struct msm_dsi_pll *pll)
{
	if (unlikely(!pll->pll_on))
		return;

	pll->disable_seq(pll);

	pll->pll_on = false;
}

/*
 * DSI PLL Helper functions
 */
long msm_dsi_pll_helper_clk_round_rate(struct clk_hw *hw,
		unsigned long rate, unsigned long *parent_rate)
{
	struct msm_dsi_pll *pll = hw_clk_to_pll(hw);

	if      (rate < pll->min_rate)
		return  pll->min_rate;
	else if (rate > pll->max_rate)
		return  pll->max_rate;
	else
		return rate;
}

int msm_dsi_pll_helper_clk_prepare(struct clk_hw *hw)
{
	struct msm_dsi_pll *pll = hw_clk_to_pll(hw);

	return dsi_pll_enable(pll);
}

void msm_dsi_pll_helper_clk_unprepare(struct clk_hw *hw)
{
	struct msm_dsi_pll *pll = hw_clk_to_pll(hw);

	dsi_pll_disable(pll);
}

void msm_dsi_pll_helper_unregister_clks(struct platform_device *pdev,
					struct clk **clks, u32 num_clks)
{
	of_clk_del_provider(pdev->dev.of_node);

	if (!num_clks || !clks)
		return;

	do {
		clk_unregister(clks[--num_clks]);
		clks[num_clks] = NULL;
	} while (num_clks);
}

/*
 * DSI PLL API
 */
int msm_dsi_pll_get_clk_provider(struct msm_dsi_pll *pll,
	struct clk **byte_clk_provider, struct clk **pixel_clk_provider)
{
	if (pll->get_provider)
		return pll->get_provider(pll,
					byte_clk_provider,
					pixel_clk_provider);

	return -EINVAL;
}

void msm_dsi_pll_destroy(struct msm_dsi_pll *pll)
{
	if (pll->destroy)
		pll->destroy(pll);
}

void msm_dsi_pll_save_state(struct msm_dsi_pll *pll)
{
	if (pll->save_state) {
		pll->save_state(pll);
		pll->state_saved = true;
	}
}

int msm_dsi_pll_restore_state(struct msm_dsi_pll *pll)
{
	int ret;

	if (pll->restore_state && pll->state_saved) {
		ret = pll->restore_state(pll);
		if (ret)
			return ret;

		pll->state_saved = false;
	}

	return 0;
}

int msm_dsi_pll_set_usecase(struct msm_dsi_pll *pll,
			    enum msm_dsi_phy_usecase uc)
{
	if (pll->set_usecase)
		return pll->set_usecase(pll, uc);

	return 0;
}

struct msm_dsi_pll *msm_dsi_pll_init(struct platform_device *pdev,
			enum msm_dsi_phy_type type, int id)
{
	struct device *dev = &pdev->dev;
	struct msm_dsi_pll *pll;

	switch (type) {
	case MSM_DSI_PHY_28NM_HPM:
	case MSM_DSI_PHY_28NM_LP:
		pll = msm_dsi_pll_28nm_init(pdev, type, id);
		break;
	case MSM_DSI_PHY_28NM_8960:
		pll = msm_dsi_pll_28nm_8960_init(pdev, id);
		break;
	case MSM_DSI_PHY_14NM:
		pll = msm_dsi_pll_14nm_init(pdev, id);
		break;
	case MSM_DSI_PHY_10NM:
		pll = msm_dsi_pll_10nm_init(pdev, id);
		break;
	default:
		pll = ERR_PTR(-ENXIO);
		break;
	}

	if (IS_ERR(pll)) {
		dev_err(dev, "%s: failed to init DSI PLL\n", __func__);
		return pll;
	}

	pll->type = type;

	DBG("DSI:%d PLL registered", id);

	return pll;
}

