/*
 * Copyright (C) 2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
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

static int mpd4_lvds_pll_enable(struct clk_hw *hw)
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

static void mpd4_lvds_pll_disable(struct clk_hw *hw)
{
	struct mdp4_lvds_pll *lvds_pll = to_mdp4_lvds_pll(hw);
	struct mdp4_kms *mdp4_kms = get_kms(lvds_pll);

	DBG("");

	mdp4_write(mdp4_kms, REG_MDP4_LVDS_PHY_CFG0, 0x0);
	mdp4_write(mdp4_kms, REG_MDP4_LVDS_PHY_PLL_CTRL_0, 0x0);
}

static unsigned long mpd4_lvds_pll_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct mdp4_lvds_pll *lvds_pll = to_mdp4_lvds_pll(hw);
	return lvds_pll->pixclk;
}

static long mpd4_lvds_pll_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *parent_rate)
{
	const struct pll_rate *pll_rate = find_rate(rate);
	return pll_rate->rate;
}

static int mpd4_lvds_pll_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct mdp4_lvds_pll *lvds_pll = to_mdp4_lvds_pll(hw);
	lvds_pll->pixclk = rate;
	return 0;
}


static const struct clk_ops mpd4_lvds_pll_ops = {
	.enable = mpd4_lvds_pll_enable,
	.disable = mpd4_lvds_pll_disable,
	.recalc_rate = mpd4_lvds_pll_recalc_rate,
	.round_rate = mpd4_lvds_pll_round_rate,
	.set_rate = mpd4_lvds_pll_set_rate,
};

static const char *mpd4_lvds_pll_parents[] = {
	"pxo",
};

static struct clk_init_data pll_init = {
	.name = "mpd4_lvds_pll",
	.ops = &mpd4_lvds_pll_ops,
	.parent_names = mpd4_lvds_pll_parents,
	.num_parents = ARRAY_SIZE(mpd4_lvds_pll_parents),
};

struct clk *mpd4_lvds_pll_init(struct drm_device *dev)
{
	struct mdp4_lvds_pll *lvds_pll;
	struct clk *clk;
	int ret;

	lvds_pll = devm_kzalloc(dev->dev, sizeof(*lvds_pll), GFP_KERNEL);
	if (!lvds_pll) {
		ret = -ENOMEM;
		goto fail;
	}

	lvds_pll->dev = dev;

	lvds_pll->pll_hw.init = &pll_init;
	clk = devm_clk_register(dev->dev, &lvds_pll->pll_hw);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto fail;
	}

	return clk;

fail:
	return ERR_PTR(ret);
}
