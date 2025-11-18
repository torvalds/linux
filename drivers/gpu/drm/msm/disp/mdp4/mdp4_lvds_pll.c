// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>

#include "mdp4_kms.h"

struct mdp4_lvds_pll {
	struct clk_hw pll_hw;
	struct drm_device *dev;
	unsigned long pixclk;
};
#define to_mdp4_lvds_pll(x) container_of(x, struct mdp4_lvds_pll, pll_hw)

static struct mdp4_kms *get_kms(struct mdp4_lvds_pll *lvds_pll)
{
	struct msm_drm_private *priv = lvds_pll->dev->dev_private;
	return to_mdp4_kms(to_mdp_kms(priv->kms));
}

struct pll_rate {
	unsigned long rate;
	struct {
		uint32_t val;
		uint32_t reg;
	} conf[32];
};

/* NOTE: keep sorted highest freq to lowest: */
static const struct pll_rate freqtbl[] = {
	{ 72000000, {
		{ 0x8f, REG_MDP4_LVDS_PHY_PLL_CTRL_1 },
		{ 0x30, REG_MDP4_LVDS_PHY_PLL_CTRL_2 },
		{ 0xc6, REG_MDP4_LVDS_PHY_PLL_CTRL_3 },
		{ 0x10, REG_MDP4_LVDS_PHY_PLL_CTRL_5 },
		{ 0x07, REG_MDP4_LVDS_PHY_PLL_CTRL_6 },
		{ 0x62, REG_MDP4_LVDS_PHY_PLL_CTRL_7 },
		{ 0x41, REG_MDP4_LVDS_PHY_PLL_CTRL_8 },
		{ 0x0d, REG_MDP4_LVDS_PHY_PLL_CTRL_9 },
		{ 0, 0 } }
	},
};

static const struct pll_rate *find_rate(unsigned long rate)
{
	int i;
	for (i = 1; i < ARRAY_SIZE(freqtbl); i++)
		if (rate > freqtbl[i].rate)
			return &freqtbl[i-1];
	return &freqtbl[i-1];
}

static int mdp4_lvds_pll_enable(struct clk_hw *hw)
{
	struct mdp4_lvds_pll *lvds_pll = to_mdp4_lvds_pll(hw);
	struct mdp4_kms *mdp4_kms = get_kms(lvds_pll);
	const struct pll_rate *pll_rate = find_rate(lvds_pll->pixclk);
	int i;

	DBG("pixclk=%lu (%lu)", lvds_pll->pixclk, pll_rate->rate);

	if (WARN_ON(!pll_rate))
		return -EINVAL;

	mdp4_write(mdp4_kms, REG_MDP4_LCDC_LVDS_PHY_RESET, 0x33);

	for (i = 0; pll_rate->conf[i].reg; i++)
		mdp4_write(mdp4_kms, pll_rate->conf[i].reg, pll_rate->conf[i].val);

	mdp4_write(mdp4_kms, REG_MDP4_LVDS_PHY_PLL_CTRL_0, 0x01);

	/* Wait until LVDS PLL is locked and ready */
	while (!mdp4_read(mdp4_kms, REG_MDP4_LVDS_PHY_PLL_LOCKED))
		cpu_relax();

	return 0;
}

static void mdp4_lvds_pll_disable(struct clk_hw *hw)
{
	struct mdp4_lvds_pll *lvds_pll = to_mdp4_lvds_pll(hw);
	struct mdp4_kms *mdp4_kms = get_kms(lvds_pll);

	DBG("");

	mdp4_write(mdp4_kms, REG_MDP4_LVDS_PHY_CFG0, 0x0);
	mdp4_write(mdp4_kms, REG_MDP4_LVDS_PHY_PLL_CTRL_0, 0x0);
}

static unsigned long mdp4_lvds_pll_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct mdp4_lvds_pll *lvds_pll = to_mdp4_lvds_pll(hw);
	return lvds_pll->pixclk;
}

static int mdp4_lvds_pll_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req)
{
	const struct pll_rate *pll_rate = find_rate(req->rate);

	req->rate = pll_rate->rate;

	return 0;
}

static int mdp4_lvds_pll_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct mdp4_lvds_pll *lvds_pll = to_mdp4_lvds_pll(hw);
	lvds_pll->pixclk = rate;
	return 0;
}


static const struct clk_ops mdp4_lvds_pll_ops = {
	.enable = mdp4_lvds_pll_enable,
	.disable = mdp4_lvds_pll_disable,
	.recalc_rate = mdp4_lvds_pll_recalc_rate,
	.determine_rate = mdp4_lvds_pll_determine_rate,
	.set_rate = mdp4_lvds_pll_set_rate,
};

static const struct clk_parent_data mdp4_lvds_pll_parents[] = {
	{ .fw_name = "pxo", .name = "pxo", },
};

static struct clk_init_data pll_init = {
	.name = "mdp4_lvds_pll",
	.ops = &mdp4_lvds_pll_ops,
	.parent_data = mdp4_lvds_pll_parents,
	.num_parents = ARRAY_SIZE(mdp4_lvds_pll_parents),
};

static struct clk_hw *mdp4_lvds_pll_init(struct drm_device *dev)
{
	struct mdp4_lvds_pll *lvds_pll;
	int ret;

	lvds_pll = devm_kzalloc(dev->dev, sizeof(*lvds_pll), GFP_KERNEL);
	if (!lvds_pll)
		return ERR_PTR(-ENOMEM);

	lvds_pll->dev = dev;

	lvds_pll->pll_hw.init = &pll_init;
	ret = devm_clk_hw_register(dev->dev, &lvds_pll->pll_hw);
	if (ret)
		return ERR_PTR(ret);

	ret = devm_of_clk_add_hw_provider(dev->dev, of_clk_hw_simple_get, &lvds_pll->pll_hw);
	if (ret)
		return ERR_PTR(ret);

	return &lvds_pll->pll_hw;
}

struct clk *mdp4_get_lcdc_clock(struct drm_device *dev)
{
	struct clk_hw *hw;
	struct clk *clk;


	/* TODO: do we need different pll in other cases? */
	hw = mdp4_lvds_pll_init(dev);
	if (IS_ERR(hw)) {
		DRM_DEV_ERROR(dev->dev, "failed to register LVDS PLL\n");
		return ERR_CAST(hw);
	}

	clk = devm_clk_get(dev->dev, "lcdc_clk");
	if (clk == ERR_PTR(-ENOENT)) {
		drm_warn(dev, "can't get LCDC clock, using PLL directly\n");

		return devm_clk_hw_get_clk(dev->dev, hw, "lcdc_clk");
	}

	return clk;
}
