// SPDX-License-Identifier: GPL-2.0+
/*
 * Based on drivers/clk/tegra/clk-emc.c
 * Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author: Dmitry Osipenko <digetx@gmail.com>
 * Copyright (C) 2019 GRATE-DRIVER project
 */

#define pr_fmt(fmt)	"tegra-emc-clk: " fmt

#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/clk/tegra.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "clk.h"

#define CLK_SOURCE_EMC_2X_CLK_DIVISOR_MASK	GENMASK(7, 0)
#define CLK_SOURCE_EMC_2X_CLK_SRC_MASK		GENMASK(31, 30)
#define CLK_SOURCE_EMC_2X_CLK_SRC_SHIFT		30

#define MC_EMC_SAME_FREQ	BIT(16)
#define USE_PLLM_UD		BIT(29)

#define EMC_SRC_PLL_M		0
#define EMC_SRC_PLL_C		1
#define EMC_SRC_PLL_P		2
#define EMC_SRC_CLK_M		3

static const char * const emc_parent_clk_names[] = {
	"pll_m", "pll_c", "pll_p", "clk_m",
};

struct tegra_clk_emc {
	struct clk_hw hw;
	void __iomem *reg;
	bool mc_same_freq;
	bool want_low_jitter;

	tegra20_clk_emc_round_cb *round_cb;
	void *cb_arg;
};

static inline struct tegra_clk_emc *to_tegra_clk_emc(struct clk_hw *hw)
{
	return container_of(hw, struct tegra_clk_emc, hw);
}

static unsigned long emc_recalc_rate(struct clk_hw *hw,
				     unsigned long parent_rate)
{
	struct tegra_clk_emc *emc = to_tegra_clk_emc(hw);
	u32 val, div;

	val = readl_relaxed(emc->reg);
	div = val & CLK_SOURCE_EMC_2X_CLK_DIVISOR_MASK;

	return DIV_ROUND_UP(parent_rate * 2, div + 2);
}

static u8 emc_get_parent(struct clk_hw *hw)
{
	struct tegra_clk_emc *emc = to_tegra_clk_emc(hw);

	return readl_relaxed(emc->reg) >> CLK_SOURCE_EMC_2X_CLK_SRC_SHIFT;
}

static int emc_set_parent(struct clk_hw *hw, u8 index)
{
	struct tegra_clk_emc *emc = to_tegra_clk_emc(hw);
	u32 val, div;

	val = readl_relaxed(emc->reg);
	val &= ~CLK_SOURCE_EMC_2X_CLK_SRC_MASK;
	val |= index << CLK_SOURCE_EMC_2X_CLK_SRC_SHIFT;

	div = val & CLK_SOURCE_EMC_2X_CLK_DIVISOR_MASK;

	if (index == EMC_SRC_PLL_M && div == 0 && emc->want_low_jitter)
		val |= USE_PLLM_UD;
	else
		val &= ~USE_PLLM_UD;

	if (emc->mc_same_freq)
		val |= MC_EMC_SAME_FREQ;
	else
		val &= ~MC_EMC_SAME_FREQ;

	writel_relaxed(val, emc->reg);

	fence_udelay(1, emc->reg);

	return 0;
}

static int emc_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct tegra_clk_emc *emc = to_tegra_clk_emc(hw);
	unsigned int index;
	u32 val, div;

	div = div_frac_get(rate, parent_rate, 8, 1, 0);

	val = readl_relaxed(emc->reg);
	val &= ~CLK_SOURCE_EMC_2X_CLK_DIVISOR_MASK;
	val |= div;

	index = val >> CLK_SOURCE_EMC_2X_CLK_SRC_SHIFT;

	if (index == EMC_SRC_PLL_M && div == 0 && emc->want_low_jitter)
		val |= USE_PLLM_UD;
	else
		val &= ~USE_PLLM_UD;

	if (emc->mc_same_freq)
		val |= MC_EMC_SAME_FREQ;
	else
		val &= ~MC_EMC_SAME_FREQ;

	writel_relaxed(val, emc->reg);

	fence_udelay(1, emc->reg);

	return 0;
}

static int emc_set_rate_and_parent(struct clk_hw *hw,
				   unsigned long rate,
				   unsigned long parent_rate,
				   u8 index)
{
	struct tegra_clk_emc *emc = to_tegra_clk_emc(hw);
	u32 val, div;

	div = div_frac_get(rate, parent_rate, 8, 1, 0);

	val = readl_relaxed(emc->reg);

	val &= ~CLK_SOURCE_EMC_2X_CLK_SRC_MASK;
	val |= index << CLK_SOURCE_EMC_2X_CLK_SRC_SHIFT;

	val &= ~CLK_SOURCE_EMC_2X_CLK_DIVISOR_MASK;
	val |= div;

	if (index == EMC_SRC_PLL_M && div == 0 && emc->want_low_jitter)
		val |= USE_PLLM_UD;
	else
		val &= ~USE_PLLM_UD;

	if (emc->mc_same_freq)
		val |= MC_EMC_SAME_FREQ;
	else
		val &= ~MC_EMC_SAME_FREQ;

	writel_relaxed(val, emc->reg);

	fence_udelay(1, emc->reg);

	return 0;
}

static int emc_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	struct tegra_clk_emc *emc = to_tegra_clk_emc(hw);
	struct clk_hw *parent_hw;
	unsigned long divided_rate;
	unsigned long parent_rate;
	unsigned int i;
	long emc_rate;
	int div;

	emc_rate = emc->round_cb(req->rate, req->min_rate, req->max_rate,
				 emc->cb_arg);
	if (emc_rate < 0)
		return emc_rate;

	for (i = 0; i < ARRAY_SIZE(emc_parent_clk_names); i++) {
		parent_hw = clk_hw_get_parent_by_index(hw, i);

		if (req->best_parent_hw == parent_hw)
			parent_rate = req->best_parent_rate;
		else
			parent_rate = clk_hw_get_rate(parent_hw);

		if (emc_rate > parent_rate)
			continue;

		div = div_frac_get(emc_rate, parent_rate, 8, 1, 0);
		divided_rate = DIV_ROUND_UP(parent_rate * 2, div + 2);

		if (divided_rate != emc_rate)
			continue;

		req->best_parent_rate = parent_rate;
		req->best_parent_hw = parent_hw;
		req->rate = emc_rate;
		break;
	}

	if (i == ARRAY_SIZE(emc_parent_clk_names)) {
		pr_err_once("can't find parent for rate %lu emc_rate %lu\n",
			    req->rate, emc_rate);
		return -EINVAL;
	}

	return 0;
}

static const struct clk_ops tegra_clk_emc_ops = {
	.recalc_rate = emc_recalc_rate,
	.get_parent = emc_get_parent,
	.set_parent = emc_set_parent,
	.set_rate = emc_set_rate,
	.set_rate_and_parent = emc_set_rate_and_parent,
	.determine_rate = emc_determine_rate,
};

void tegra20_clk_set_emc_round_callback(tegra20_clk_emc_round_cb *round_cb,
					void *cb_arg)
{
	struct clk *clk = __clk_lookup("emc");
	struct tegra_clk_emc *emc;
	struct clk_hw *hw;

	if (clk) {
		hw = __clk_get_hw(clk);
		emc = to_tegra_clk_emc(hw);

		emc->round_cb = round_cb;
		emc->cb_arg = cb_arg;
	}
}
EXPORT_SYMBOL_GPL(tegra20_clk_set_emc_round_callback);

bool tegra20_clk_emc_driver_available(struct clk_hw *emc_hw)
{
	return to_tegra_clk_emc(emc_hw)->round_cb != NULL;
}

struct clk *tegra20_clk_register_emc(void __iomem *ioaddr, bool low_jitter)
{
	struct tegra_clk_emc *emc;
	struct clk_init_data init;
	struct clk *clk;

	emc = kzalloc(sizeof(*emc), GFP_KERNEL);
	if (!emc)
		return NULL;

	/*
	 * EMC stands for External Memory Controller.
	 *
	 * We don't want EMC clock to be disabled ever by gating its
	 * parent and whatnot because system is busted immediately in that
	 * case, hence the clock is marked as critical.
	 */
	init.name = "emc";
	init.ops = &tegra_clk_emc_ops;
	init.flags = CLK_IS_CRITICAL;
	init.parent_names = emc_parent_clk_names;
	init.num_parents = ARRAY_SIZE(emc_parent_clk_names);

	emc->reg = ioaddr;
	emc->hw.init = &init;
	emc->want_low_jitter = low_jitter;

	clk = clk_register(NULL, &emc->hw);
	if (IS_ERR(clk)) {
		kfree(emc);
		return NULL;
	}

	return clk;
}

int tegra20_clk_prepare_emc_mc_same_freq(struct clk *emc_clk, bool same)
{
	struct tegra_clk_emc *emc;
	struct clk_hw *hw;

	if (!emc_clk)
		return -EINVAL;

	hw = __clk_get_hw(emc_clk);
	emc = to_tegra_clk_emc(hw);
	emc->mc_same_freq = same;

	return 0;
}
EXPORT_SYMBOL_GPL(tegra20_clk_prepare_emc_mc_same_freq);
