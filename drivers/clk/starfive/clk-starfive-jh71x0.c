// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive JH71X0 Clock Generator Driver
 *
 * Copyright (C) 2021-2022 Emil Renner Berthing <kernel@esmil.dk>
 */

#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/io.h>

#include "clk-starfive-jh71x0.h"

static struct jh71x0_clk *jh71x0_clk_from(struct clk_hw *hw)
{
	return container_of(hw, struct jh71x0_clk, hw);
}

static struct jh71x0_clk_priv *jh71x0_priv_from(struct jh71x0_clk *clk)
{
	return container_of(clk, struct jh71x0_clk_priv, reg[clk->idx]);
}

static u32 jh71x0_clk_reg_get(struct jh71x0_clk *clk)
{
	struct jh71x0_clk_priv *priv = jh71x0_priv_from(clk);
	void __iomem *reg = priv->base + 4 * clk->idx;

	return readl_relaxed(reg);
}

static void jh71x0_clk_reg_rmw(struct jh71x0_clk *clk, u32 mask, u32 value)
{
	struct jh71x0_clk_priv *priv = jh71x0_priv_from(clk);
	void __iomem *reg = priv->base + 4 * clk->idx;
	unsigned long flags;

	spin_lock_irqsave(&priv->rmw_lock, flags);
	value |= readl_relaxed(reg) & ~mask;
	writel_relaxed(value, reg);
	spin_unlock_irqrestore(&priv->rmw_lock, flags);
}

static int jh71x0_clk_enable(struct clk_hw *hw)
{
	struct jh71x0_clk *clk = jh71x0_clk_from(hw);

	jh71x0_clk_reg_rmw(clk, JH71X0_CLK_ENABLE, JH71X0_CLK_ENABLE);
	return 0;
}

static void jh71x0_clk_disable(struct clk_hw *hw)
{
	struct jh71x0_clk *clk = jh71x0_clk_from(hw);

	jh71x0_clk_reg_rmw(clk, JH71X0_CLK_ENABLE, 0);
}

static int jh71x0_clk_is_enabled(struct clk_hw *hw)
{
	struct jh71x0_clk *clk = jh71x0_clk_from(hw);

	return !!(jh71x0_clk_reg_get(clk) & JH71X0_CLK_ENABLE);
}

static unsigned long jh71x0_clk_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct jh71x0_clk *clk = jh71x0_clk_from(hw);
	u32 div = jh71x0_clk_reg_get(clk) & JH71X0_CLK_DIV_MASK;

	return div ? parent_rate / div : 0;
}

static int jh71x0_clk_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	struct jh71x0_clk *clk = jh71x0_clk_from(hw);
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

static int jh71x0_clk_set_rate(struct clk_hw *hw,
			       unsigned long rate,
			       unsigned long parent_rate)
{
	struct jh71x0_clk *clk = jh71x0_clk_from(hw);
	unsigned long div = clamp(DIV_ROUND_CLOSEST(parent_rate, rate),
				  1UL, (unsigned long)clk->max_div);

	jh71x0_clk_reg_rmw(clk, JH71X0_CLK_DIV_MASK, div);
	return 0;
}

static unsigned long jh71x0_clk_frac_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct jh71x0_clk *clk = jh71x0_clk_from(hw);
	u32 reg = jh71x0_clk_reg_get(clk);
	unsigned long div100 = 100 * (reg & JH71X0_CLK_INT_MASK) +
			       ((reg & JH71X0_CLK_FRAC_MASK) >> JH71X0_CLK_FRAC_SHIFT);

	return (div100 >= JH71X0_CLK_FRAC_MIN) ? 100 * parent_rate / div100 : 0;
}

static int jh71x0_clk_frac_determine_rate(struct clk_hw *hw,
					  struct clk_rate_request *req)
{
	unsigned long parent100 = 100 * req->best_parent_rate;
	unsigned long rate = clamp(req->rate, req->min_rate, req->max_rate);
	unsigned long div100 = clamp(DIV_ROUND_CLOSEST(parent100, rate),
				     JH71X0_CLK_FRAC_MIN, JH71X0_CLK_FRAC_MAX);
	unsigned long result = parent100 / div100;

	/* clamp the result as in jh71x0_clk_determine_rate() above */
	if (result > req->max_rate && div100 < JH71X0_CLK_FRAC_MAX)
		result = parent100 / (div100 + 1);
	if (result < req->min_rate && div100 > JH71X0_CLK_FRAC_MIN)
		result = parent100 / (div100 - 1);

	req->rate = result;
	return 0;
}

static int jh71x0_clk_frac_set_rate(struct clk_hw *hw,
				    unsigned long rate,
				    unsigned long parent_rate)
{
	struct jh71x0_clk *clk = jh71x0_clk_from(hw);
	unsigned long div100 = clamp(DIV_ROUND_CLOSEST(100 * parent_rate, rate),
				     JH71X0_CLK_FRAC_MIN, JH71X0_CLK_FRAC_MAX);
	u32 value = ((div100 % 100) << JH71X0_CLK_FRAC_SHIFT) | (div100 / 100);

	jh71x0_clk_reg_rmw(clk, JH71X0_CLK_DIV_MASK, value);
	return 0;
}

static u8 jh71x0_clk_get_parent(struct clk_hw *hw)
{
	struct jh71x0_clk *clk = jh71x0_clk_from(hw);
	u32 value = jh71x0_clk_reg_get(clk);

	return (value & JH71X0_CLK_MUX_MASK) >> JH71X0_CLK_MUX_SHIFT;
}

static int jh71x0_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct jh71x0_clk *clk = jh71x0_clk_from(hw);
	u32 value = (u32)index << JH71X0_CLK_MUX_SHIFT;

	jh71x0_clk_reg_rmw(clk, JH71X0_CLK_MUX_MASK, value);
	return 0;
}

static int jh71x0_clk_mux_determine_rate(struct clk_hw *hw,
					 struct clk_rate_request *req)
{
	return clk_mux_determine_rate_flags(hw, req, 0);
}

static int jh71x0_clk_get_phase(struct clk_hw *hw)
{
	struct jh71x0_clk *clk = jh71x0_clk_from(hw);
	u32 value = jh71x0_clk_reg_get(clk);

	return (value & JH71X0_CLK_INVERT) ? 180 : 0;
}

static int jh71x0_clk_set_phase(struct clk_hw *hw, int degrees)
{
	struct jh71x0_clk *clk = jh71x0_clk_from(hw);
	u32 value;

	if (degrees == 0)
		value = 0;
	else if (degrees == 180)
		value = JH71X0_CLK_INVERT;
	else
		return -EINVAL;

	jh71x0_clk_reg_rmw(clk, JH71X0_CLK_INVERT, value);
	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void jh71x0_clk_debug_init(struct clk_hw *hw, struct dentry *dentry)
{
	static const struct debugfs_reg32 jh71x0_clk_reg = {
		.name = "CTRL",
		.offset = 0,
	};
	struct jh71x0_clk *clk = jh71x0_clk_from(hw);
	struct jh71x0_clk_priv *priv = jh71x0_priv_from(clk);
	struct debugfs_regset32 *regset;

	regset = devm_kzalloc(priv->dev, sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return;

	regset->regs = &jh71x0_clk_reg;
	regset->nregs = 1;
	regset->base = priv->base + 4 * clk->idx;

	debugfs_create_regset32("registers", 0400, dentry, regset);
}
#else
#define jh71x0_clk_debug_init NULL
#endif

static const struct clk_ops jh71x0_clk_gate_ops = {
	.enable = jh71x0_clk_enable,
	.disable = jh71x0_clk_disable,
	.is_enabled = jh71x0_clk_is_enabled,
	.debug_init = jh71x0_clk_debug_init,
};

static const struct clk_ops jh71x0_clk_div_ops = {
	.recalc_rate = jh71x0_clk_recalc_rate,
	.determine_rate = jh71x0_clk_determine_rate,
	.set_rate = jh71x0_clk_set_rate,
	.debug_init = jh71x0_clk_debug_init,
};

static const struct clk_ops jh71x0_clk_fdiv_ops = {
	.recalc_rate = jh71x0_clk_frac_recalc_rate,
	.determine_rate = jh71x0_clk_frac_determine_rate,
	.set_rate = jh71x0_clk_frac_set_rate,
	.debug_init = jh71x0_clk_debug_init,
};

static const struct clk_ops jh71x0_clk_gdiv_ops = {
	.enable = jh71x0_clk_enable,
	.disable = jh71x0_clk_disable,
	.is_enabled = jh71x0_clk_is_enabled,
	.recalc_rate = jh71x0_clk_recalc_rate,
	.determine_rate = jh71x0_clk_determine_rate,
	.set_rate = jh71x0_clk_set_rate,
	.debug_init = jh71x0_clk_debug_init,
};

static const struct clk_ops jh71x0_clk_mux_ops = {
	.determine_rate = jh71x0_clk_mux_determine_rate,
	.set_parent = jh71x0_clk_set_parent,
	.get_parent = jh71x0_clk_get_parent,
	.debug_init = jh71x0_clk_debug_init,
};

static const struct clk_ops jh71x0_clk_gmux_ops = {
	.enable = jh71x0_clk_enable,
	.disable = jh71x0_clk_disable,
	.is_enabled = jh71x0_clk_is_enabled,
	.determine_rate = jh71x0_clk_mux_determine_rate,
	.set_parent = jh71x0_clk_set_parent,
	.get_parent = jh71x0_clk_get_parent,
	.debug_init = jh71x0_clk_debug_init,
};

static const struct clk_ops jh71x0_clk_mdiv_ops = {
	.recalc_rate = jh71x0_clk_recalc_rate,
	.determine_rate = jh71x0_clk_determine_rate,
	.get_parent = jh71x0_clk_get_parent,
	.set_parent = jh71x0_clk_set_parent,
	.set_rate = jh71x0_clk_set_rate,
	.debug_init = jh71x0_clk_debug_init,
};

static const struct clk_ops jh71x0_clk_gmd_ops = {
	.enable = jh71x0_clk_enable,
	.disable = jh71x0_clk_disable,
	.is_enabled = jh71x0_clk_is_enabled,
	.recalc_rate = jh71x0_clk_recalc_rate,
	.determine_rate = jh71x0_clk_determine_rate,
	.get_parent = jh71x0_clk_get_parent,
	.set_parent = jh71x0_clk_set_parent,
	.set_rate = jh71x0_clk_set_rate,
	.debug_init = jh71x0_clk_debug_init,
};

static const struct clk_ops jh71x0_clk_inv_ops = {
	.get_phase = jh71x0_clk_get_phase,
	.set_phase = jh71x0_clk_set_phase,
	.debug_init = jh71x0_clk_debug_init,
};

const struct clk_ops *starfive_jh71x0_clk_ops(u32 max)
{
	if (max & JH71X0_CLK_DIV_MASK) {
		if (max & JH71X0_CLK_MUX_MASK) {
			if (max & JH71X0_CLK_ENABLE)
				return &jh71x0_clk_gmd_ops;
			return &jh71x0_clk_mdiv_ops;
		}
		if (max & JH71X0_CLK_ENABLE)
			return &jh71x0_clk_gdiv_ops;
		if (max == JH71X0_CLK_FRAC_MAX)
			return &jh71x0_clk_fdiv_ops;
		return &jh71x0_clk_div_ops;
	}

	if (max & JH71X0_CLK_MUX_MASK) {
		if (max & JH71X0_CLK_ENABLE)
			return &jh71x0_clk_gmux_ops;
		return &jh71x0_clk_mux_ops;
	}

	if (max & JH71X0_CLK_ENABLE)
		return &jh71x0_clk_gate_ops;

	return &jh71x0_clk_inv_ops;
}
EXPORT_SYMBOL_GPL(starfive_jh71x0_clk_ops);
