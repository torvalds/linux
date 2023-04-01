// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive JH7100 Clock Generator Driver
 *
 * Copyright (C) 2021-2022 Emil Renner Berthing <kernel@esmil.dk>
 */

#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/io.h>

#include "clk-starfive-jh71x0.h"

static struct jh7100_clk *jh7100_clk_from(struct clk_hw *hw)
{
	return container_of(hw, struct jh7100_clk, hw);
}

static struct jh7100_clk_priv *jh7100_priv_from(struct jh7100_clk *clk)
{
	return container_of(clk, struct jh7100_clk_priv, reg[clk->idx]);
}

static u32 jh7100_clk_reg_get(struct jh7100_clk *clk)
{
	struct jh7100_clk_priv *priv = jh7100_priv_from(clk);
	void __iomem *reg = priv->base + 4 * clk->idx;

	return readl_relaxed(reg);
}

static void jh7100_clk_reg_rmw(struct jh7100_clk *clk, u32 mask, u32 value)
{
	struct jh7100_clk_priv *priv = jh7100_priv_from(clk);
	void __iomem *reg = priv->base + 4 * clk->idx;
	unsigned long flags;

	spin_lock_irqsave(&priv->rmw_lock, flags);
	value |= readl_relaxed(reg) & ~mask;
	writel_relaxed(value, reg);
	spin_unlock_irqrestore(&priv->rmw_lock, flags);
}

static int jh7100_clk_enable(struct clk_hw *hw)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);

	jh7100_clk_reg_rmw(clk, JH7100_CLK_ENABLE, JH7100_CLK_ENABLE);
	return 0;
}

static void jh7100_clk_disable(struct clk_hw *hw)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);

	jh7100_clk_reg_rmw(clk, JH7100_CLK_ENABLE, 0);
}

static int jh7100_clk_is_enabled(struct clk_hw *hw)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);

	return !!(jh7100_clk_reg_get(clk) & JH7100_CLK_ENABLE);
}

static unsigned long jh7100_clk_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	u32 div = jh7100_clk_reg_get(clk) & JH7100_CLK_DIV_MASK;

	return div ? parent_rate / div : 0;
}

static int jh7100_clk_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	unsigned long parent = req->best_parent_rate;
	unsigned long rate = clamp(req->rate, req->min_rate, req->max_rate);
	unsigned long div = min_t(unsigned long, DIV_ROUND_UP(parent, rate), clk->max_div);
	unsigned long result = parent / div;

	/*
	 * we want the result clamped by min_rate and max_rate if possible:
	 * case 1: div hits the max divider value, which means it's less than
	 * parent / rate, so the result is greater than rate and min_rate in
	 * particular. we can't do anything about result > max_rate because the
	 * divider doesn't go any further.
	 * case 2: div = DIV_ROUND_UP(parent, rate) which means the result is
	 * always lower or equal to rate and max_rate. however the result may
	 * turn out lower than min_rate, but then the next higher rate is fine:
	 *   div - 1 = ceil(parent / rate) - 1 < parent / rate
	 * and thus
	 *   min_rate <= rate < parent / (div - 1)
	 */
	if (result < req->min_rate && div > 1)
		result = parent / (div - 1);

	req->rate = result;
	return 0;
}

static int jh7100_clk_set_rate(struct clk_hw *hw,
			       unsigned long rate,
			       unsigned long parent_rate)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	unsigned long div = clamp(DIV_ROUND_CLOSEST(parent_rate, rate),
				  1UL, (unsigned long)clk->max_div);

	jh7100_clk_reg_rmw(clk, JH7100_CLK_DIV_MASK, div);
	return 0;
}

static unsigned long jh7100_clk_frac_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	u32 reg = jh7100_clk_reg_get(clk);
	unsigned long div100 = 100 * (reg & JH7100_CLK_INT_MASK) +
			       ((reg & JH7100_CLK_FRAC_MASK) >> JH7100_CLK_FRAC_SHIFT);

	return (div100 >= JH7100_CLK_FRAC_MIN) ? 100 * parent_rate / div100 : 0;
}

static int jh7100_clk_frac_determine_rate(struct clk_hw *hw,
					  struct clk_rate_request *req)
{
	unsigned long parent100 = 100 * req->best_parent_rate;
	unsigned long rate = clamp(req->rate, req->min_rate, req->max_rate);
	unsigned long div100 = clamp(DIV_ROUND_CLOSEST(parent100, rate),
				     JH7100_CLK_FRAC_MIN, JH7100_CLK_FRAC_MAX);
	unsigned long result = parent100 / div100;

	/* clamp the result as in jh7100_clk_determine_rate() above */
	if (result > req->max_rate && div100 < JH7100_CLK_FRAC_MAX)
		result = parent100 / (div100 + 1);
	if (result < req->min_rate && div100 > JH7100_CLK_FRAC_MIN)
		result = parent100 / (div100 - 1);

	req->rate = result;
	return 0;
}

static int jh7100_clk_frac_set_rate(struct clk_hw *hw,
				    unsigned long rate,
				    unsigned long parent_rate)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	unsigned long div100 = clamp(DIV_ROUND_CLOSEST(100 * parent_rate, rate),
				     JH7100_CLK_FRAC_MIN, JH7100_CLK_FRAC_MAX);
	u32 value = ((div100 % 100) << JH7100_CLK_FRAC_SHIFT) | (div100 / 100);

	jh7100_clk_reg_rmw(clk, JH7100_CLK_DIV_MASK, value);
	return 0;
}

static u8 jh7100_clk_get_parent(struct clk_hw *hw)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	u32 value = jh7100_clk_reg_get(clk);

	return (value & JH7100_CLK_MUX_MASK) >> JH7100_CLK_MUX_SHIFT;
}

static int jh7100_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	u32 value = (u32)index << JH7100_CLK_MUX_SHIFT;

	jh7100_clk_reg_rmw(clk, JH7100_CLK_MUX_MASK, value);
	return 0;
}

static int jh7100_clk_mux_determine_rate(struct clk_hw *hw,
					 struct clk_rate_request *req)
{
	return clk_mux_determine_rate_flags(hw, req, 0);
}

static int jh7100_clk_get_phase(struct clk_hw *hw)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	u32 value = jh7100_clk_reg_get(clk);

	return (value & JH7100_CLK_INVERT) ? 180 : 0;
}

static int jh7100_clk_set_phase(struct clk_hw *hw, int degrees)
{
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	u32 value;

	if (degrees == 0)
		value = 0;
	else if (degrees == 180)
		value = JH7100_CLK_INVERT;
	else
		return -EINVAL;

	jh7100_clk_reg_rmw(clk, JH7100_CLK_INVERT, value);
	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void jh7100_clk_debug_init(struct clk_hw *hw, struct dentry *dentry)
{
	static const struct debugfs_reg32 jh7100_clk_reg = {
		.name = "CTRL",
		.offset = 0,
	};
	struct jh7100_clk *clk = jh7100_clk_from(hw);
	struct jh7100_clk_priv *priv = jh7100_priv_from(clk);
	struct debugfs_regset32 *regset;

	regset = devm_kzalloc(priv->dev, sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return;

	regset->regs = &jh7100_clk_reg;
	regset->nregs = 1;
	regset->base = priv->base + 4 * clk->idx;

	debugfs_create_regset32("registers", 0400, dentry, regset);
}
#else
#define jh7100_clk_debug_init NULL
#endif

static const struct clk_ops jh7100_clk_gate_ops = {
	.enable = jh7100_clk_enable,
	.disable = jh7100_clk_disable,
	.is_enabled = jh7100_clk_is_enabled,
	.debug_init = jh7100_clk_debug_init,
};

static const struct clk_ops jh7100_clk_div_ops = {
	.recalc_rate = jh7100_clk_recalc_rate,
	.determine_rate = jh7100_clk_determine_rate,
	.set_rate = jh7100_clk_set_rate,
	.debug_init = jh7100_clk_debug_init,
};

static const struct clk_ops jh7100_clk_fdiv_ops = {
	.recalc_rate = jh7100_clk_frac_recalc_rate,
	.determine_rate = jh7100_clk_frac_determine_rate,
	.set_rate = jh7100_clk_frac_set_rate,
	.debug_init = jh7100_clk_debug_init,
};

static const struct clk_ops jh7100_clk_gdiv_ops = {
	.enable = jh7100_clk_enable,
	.disable = jh7100_clk_disable,
	.is_enabled = jh7100_clk_is_enabled,
	.recalc_rate = jh7100_clk_recalc_rate,
	.determine_rate = jh7100_clk_determine_rate,
	.set_rate = jh7100_clk_set_rate,
	.debug_init = jh7100_clk_debug_init,
};

static const struct clk_ops jh7100_clk_mux_ops = {
	.determine_rate = jh7100_clk_mux_determine_rate,
	.set_parent = jh7100_clk_set_parent,
	.get_parent = jh7100_clk_get_parent,
	.debug_init = jh7100_clk_debug_init,
};

static const struct clk_ops jh7100_clk_gmux_ops = {
	.enable = jh7100_clk_enable,
	.disable = jh7100_clk_disable,
	.is_enabled = jh7100_clk_is_enabled,
	.determine_rate = jh7100_clk_mux_determine_rate,
	.set_parent = jh7100_clk_set_parent,
	.get_parent = jh7100_clk_get_parent,
	.debug_init = jh7100_clk_debug_init,
};

static const struct clk_ops jh7100_clk_mdiv_ops = {
	.recalc_rate = jh7100_clk_recalc_rate,
	.determine_rate = jh7100_clk_determine_rate,
	.get_parent = jh7100_clk_get_parent,
	.set_parent = jh7100_clk_set_parent,
	.set_rate = jh7100_clk_set_rate,
	.debug_init = jh7100_clk_debug_init,
};

static const struct clk_ops jh7100_clk_gmd_ops = {
	.enable = jh7100_clk_enable,
	.disable = jh7100_clk_disable,
	.is_enabled = jh7100_clk_is_enabled,
	.recalc_rate = jh7100_clk_recalc_rate,
	.determine_rate = jh7100_clk_determine_rate,
	.get_parent = jh7100_clk_get_parent,
	.set_parent = jh7100_clk_set_parent,
	.set_rate = jh7100_clk_set_rate,
	.debug_init = jh7100_clk_debug_init,
};

static const struct clk_ops jh7100_clk_inv_ops = {
	.get_phase = jh7100_clk_get_phase,
	.set_phase = jh7100_clk_set_phase,
	.debug_init = jh7100_clk_debug_init,
};

const struct clk_ops *starfive_jh7100_clk_ops(u32 max)
{
	if (max & JH7100_CLK_DIV_MASK) {
		if (max & JH7100_CLK_MUX_MASK) {
			if (max & JH7100_CLK_ENABLE)
				return &jh7100_clk_gmd_ops;
			return &jh7100_clk_mdiv_ops;
		}
		if (max & JH7100_CLK_ENABLE)
			return &jh7100_clk_gdiv_ops;
		if (max == JH7100_CLK_FRAC_MAX)
			return &jh7100_clk_fdiv_ops;
		return &jh7100_clk_div_ops;
	}

	if (max & JH7100_CLK_MUX_MASK) {
		if (max & JH7100_CLK_ENABLE)
			return &jh7100_clk_gmux_ops;
		return &jh7100_clk_mux_ops;
	}

	if (max & JH7100_CLK_ENABLE)
		return &jh7100_clk_gate_ops;

	return &jh7100_clk_inv_ops;
}
EXPORT_SYMBOL_GPL(starfive_jh7100_clk_ops);
