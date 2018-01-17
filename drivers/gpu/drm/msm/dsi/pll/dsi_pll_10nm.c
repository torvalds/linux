/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018, The Linux Foundation
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/iopoll.h>

#include "dsi_pll.h"
#include "dsi.xml.h"

struct dsi_pll_10nm {
	struct msm_dsi_pll base;

	int id;
	struct platform_device *pdev;

	void __iomem *phy_cmn_mmio;
	void __iomem *mmio;

	int vco_delay;

	enum msm_dsi_phy_usecase uc;
	struct dsi_pll_10nm *slave;
};

#define to_pll_10nm(x)	container_of(x, struct dsi_pll_10nm, base)

/*
 * Global list of private DSI PLL struct pointers. We need this for Dual DSI
 * mode, where the master PLL's clk_ops needs access the slave's private data
 */
static struct dsi_pll_10nm *pll_10nm_list[DSI_MAX];

static int dsi_pll_10nm_vco_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct msm_dsi_pll *pll = hw_clk_to_pll(hw);
	struct dsi_pll_10nm *pll_10nm = to_pll_10nm(pll);

	DBG("DSI PLL%d rate=%lu, parent's=%lu", pll_10nm->id, rate,
	    parent_rate);

	return 0;
}

static unsigned long dsi_pll_10nm_vco_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct msm_dsi_pll *pll = hw_clk_to_pll(hw);
	struct dsi_pll_10nm *pll_10nm = to_pll_10nm(pll);
	u64 vco_rate = 0x0;

	DBG("DSI PLL%d returning vco rate = %lu", pll_10nm->id,
	    (unsigned long)vco_rate);

	return (unsigned long)vco_rate;
}

static const struct clk_ops clk_ops_dsi_pll_10nm_vco = {
	.round_rate = msm_dsi_pll_helper_clk_round_rate,
	.set_rate = dsi_pll_10nm_vco_set_rate,
	.recalc_rate = dsi_pll_10nm_vco_recalc_rate,
	.prepare = msm_dsi_pll_helper_clk_prepare,
	.unprepare = msm_dsi_pll_helper_clk_unprepare,
};

/*
 * PLL Callbacks
 */

static void dsi_pll_10nm_save_state(struct msm_dsi_pll *pll)
{
	struct dsi_pll_10nm *pll_10nm = to_pll_10nm(pll);

	DBG("DSI PLL%d", pll_10nm->id);
}

static int dsi_pll_10nm_restore_state(struct msm_dsi_pll *pll)
{
	struct dsi_pll_10nm *pll_10nm = to_pll_10nm(pll);

	DBG("DSI PLL%d", pll_10nm->id);

	return 0;
}

static int dsi_pll_10nm_set_usecase(struct msm_dsi_pll *pll,
				    enum msm_dsi_phy_usecase uc)
{
	struct dsi_pll_10nm *pll_10nm = to_pll_10nm(pll);

	DBG("DSI PLL%d", pll_10nm->id);

	return 0;
}

static int dsi_pll_10nm_get_provider(struct msm_dsi_pll *pll,
				     struct clk **byte_clk_provider,
				     struct clk **pixel_clk_provider)
{
	struct dsi_pll_10nm *pll_10nm = to_pll_10nm(pll);

	DBG("DSI PLL%d", pll_10nm->id);

	if (byte_clk_provider)
		*byte_clk_provider = NULL;
	if (pixel_clk_provider)
		*pixel_clk_provider = NULL;

	return 0;
}

static void dsi_pll_10nm_destroy(struct msm_dsi_pll *pll)
{
	struct dsi_pll_10nm *pll_10nm = to_pll_10nm(pll);

	DBG("DSI PLL%d", pll_10nm->id);
}

static int pll_10nm_register(struct dsi_pll_10nm *pll_10nm)
{
	return 0;
}

struct msm_dsi_pll *msm_dsi_pll_10nm_init(struct platform_device *pdev, int id)
{
	struct dsi_pll_10nm *pll_10nm;
	struct msm_dsi_pll *pll;
	int ret;

	if (!pdev)
		return ERR_PTR(-ENODEV);

	pll_10nm = devm_kzalloc(&pdev->dev, sizeof(*pll_10nm), GFP_KERNEL);
	if (!pll_10nm)
		return ERR_PTR(-ENOMEM);

	DBG("DSI PLL%d", id);

	pll_10nm->pdev = pdev;
	pll_10nm->id = id;
	pll_10nm_list[id] = pll_10nm;

	pll_10nm->phy_cmn_mmio = msm_ioremap(pdev, "dsi_phy", "DSI_PHY");
	if (IS_ERR_OR_NULL(pll_10nm->phy_cmn_mmio)) {
		dev_err(&pdev->dev, "failed to map CMN PHY base\n");
		return ERR_PTR(-ENOMEM);
	}

	pll_10nm->mmio = msm_ioremap(pdev, "dsi_pll", "DSI_PLL");
	if (IS_ERR_OR_NULL(pll_10nm->mmio)) {
		dev_err(&pdev->dev, "failed to map PLL base\n");
		return ERR_PTR(-ENOMEM);
	}

	pll = &pll_10nm->base;
	pll->min_rate = 1000000000UL;
	pll->max_rate = 3500000000UL;
	pll->get_provider = dsi_pll_10nm_get_provider;
	pll->destroy = dsi_pll_10nm_destroy;
	pll->save_state = dsi_pll_10nm_save_state;
	pll->restore_state = dsi_pll_10nm_restore_state;
	pll->set_usecase = dsi_pll_10nm_set_usecase;

	pll_10nm->vco_delay = 1;

	ret = pll_10nm_register(pll_10nm);
	if (ret) {
		dev_err(&pdev->dev, "failed to register PLL: %d\n", ret);
		return ERR_PTR(ret);
	}

	return pll;
}
