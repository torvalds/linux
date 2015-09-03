/*
 * Copyright 2014 Linaro Ltd.
 * Copyright (C) 2014 ZTE Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "clk.h"

#define to_clk_zx_pll(_hw) container_of(_hw, struct clk_zx_pll, hw)

#define CFG0_CFG1_OFFSET 4
#define LOCK_FLAG BIT(30)
#define POWER_DOWN BIT(31)

static int rate_to_idx(struct clk_zx_pll *zx_pll, unsigned long rate)
{
	const struct zx_pll_config *config = zx_pll->lookup_table;
	int i;

	for (i = 0; i < zx_pll->count; i++) {
		if (config[i].rate > rate)
			return i > 0 ? i - 1 : 0;

		if (config[i].rate == rate)
			return i;
	}

	return i - 1;
}

static int hw_to_idx(struct clk_zx_pll *zx_pll)
{
	const struct zx_pll_config *config = zx_pll->lookup_table;
	u32 hw_cfg0, hw_cfg1;
	int i;

	hw_cfg0 = readl_relaxed(zx_pll->reg_base);
	hw_cfg1 = readl_relaxed(zx_pll->reg_base + CFG0_CFG1_OFFSET);

	/* For matching the value in lookup table */
	hw_cfg0 &= ~LOCK_FLAG;
	hw_cfg0 |= POWER_DOWN;

	for (i = 0; i < zx_pll->count; i++) {
		if (hw_cfg0 == config[i].cfg0 && hw_cfg1 == config[i].cfg1)
			return i;
	}

	return -EINVAL;
}

static unsigned long zx_pll_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct clk_zx_pll *zx_pll = to_clk_zx_pll(hw);
	int idx;

	idx = hw_to_idx(zx_pll);
	if (unlikely(idx == -EINVAL))
		return 0;

	return zx_pll->lookup_table[idx].rate;
}

static long zx_pll_round_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long *prate)
{
	struct clk_zx_pll *zx_pll = to_clk_zx_pll(hw);
	int idx;

	idx = rate_to_idx(zx_pll, rate);

	return zx_pll->lookup_table[idx].rate;
}

static int zx_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long parent_rate)
{
	/* Assume current cpu is not running on current PLL */
	struct clk_zx_pll *zx_pll = to_clk_zx_pll(hw);
	const struct zx_pll_config *config;
	int idx;

	idx = rate_to_idx(zx_pll, rate);
	config = &zx_pll->lookup_table[idx];

	writel_relaxed(config->cfg0, zx_pll->reg_base);
	writel_relaxed(config->cfg1, zx_pll->reg_base + CFG0_CFG1_OFFSET);

	return 0;
}

static int zx_pll_enable(struct clk_hw *hw)
{
	struct clk_zx_pll *zx_pll = to_clk_zx_pll(hw);
	u32 reg;

	reg = readl_relaxed(zx_pll->reg_base);
	writel_relaxed(reg & ~POWER_DOWN, zx_pll->reg_base);

	return readl_relaxed_poll_timeout(zx_pll->reg_base, reg,
					  reg & LOCK_FLAG, 0, 100);
}

static void zx_pll_disable(struct clk_hw *hw)
{
	struct clk_zx_pll *zx_pll = to_clk_zx_pll(hw);
	u32 reg;

	reg = readl_relaxed(zx_pll->reg_base);
	writel_relaxed(reg | POWER_DOWN, zx_pll->reg_base);
}

static int zx_pll_is_enabled(struct clk_hw *hw)
{
	struct clk_zx_pll *zx_pll = to_clk_zx_pll(hw);
	u32 reg;

	reg = readl_relaxed(zx_pll->reg_base);

	return !(reg & POWER_DOWN);
}

static const struct clk_ops zx_pll_ops = {
	.recalc_rate = zx_pll_recalc_rate,
	.round_rate = zx_pll_round_rate,
	.set_rate = zx_pll_set_rate,
	.enable = zx_pll_enable,
	.disable = zx_pll_disable,
	.is_enabled = zx_pll_is_enabled,
};

struct clk *clk_register_zx_pll(const char *name, const char *parent_name,
	unsigned long flags, void __iomem *reg_base,
	const struct zx_pll_config *lookup_table, int count, spinlock_t *lock)
{
	struct clk_zx_pll *zx_pll;
	struct clk *clk;
	struct clk_init_data init;

	zx_pll = kzalloc(sizeof(*zx_pll), GFP_KERNEL);
	if (!zx_pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &zx_pll_ops;
	init.flags = flags;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	zx_pll->reg_base = reg_base;
	zx_pll->lookup_table = lookup_table;
	zx_pll->count = count;
	zx_pll->lock = lock;
	zx_pll->hw.init = &init;

	clk = clk_register(NULL, &zx_pll->hw);
	if (IS_ERR(clk))
		kfree(zx_pll);

	return clk;
}
