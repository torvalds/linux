// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive JH7110 Clock Generator Driver
 *
 * Copyright (C) 2022 Xingyu Wu <xingyu.wu@starfivetech.com>
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>

#include <dt-bindings/clock/starfive-jh7110-clkgen.h>
#include "clk-starfive-jh7110.h"

static struct jh7110_clk * __init jh7110_clk_from(struct clk_hw *hw)
{
	return container_of(hw, struct jh7110_clk, hw);
}

static struct jh7110_clk_priv *jh7110_priv_from(struct jh7110_clk *clk)
{
	return container_of(clk, struct jh7110_clk_priv, reg[clk->idx]);
}

void __iomem *jh7110_clk_reg_addr_get(struct jh7110_clk *clk)
{
	void __iomem *reg;
	struct jh7110_clk_priv *priv = jh7110_priv_from(clk);

	if (clk->reg_flags == JH7110_CLK_SYS_FLAG)
		reg = priv->sys_base + 4 * clk->idx;
	else if (clk->reg_flags == JH7110_CLK_STG_FLAG)
		reg = priv->stg_base + 4 * (clk->idx - JH7110_CLK_SYS_REG_END);
	else if (clk->reg_flags == JH7110_CLK_AON_FLAG)
		reg = priv->aon_base + 4 * (clk->idx - JH7110_CLK_STG_REG_END);
	else if (clk->reg_flags == JH7110_CLK_VOUT_FLAG)
		reg = priv->vout_base + 4 * clk->idx;
	else if (clk->reg_flags == JH7110_CLK_ISP_FLAG)
		reg = priv->isp_base + 4 * clk->idx;

	return reg;
}

static u32 jh7110_clk_reg_get(struct jh7110_clk *clk)
{
	void __iomem *reg = jh7110_clk_reg_addr_get(clk);

	return readl_relaxed(reg);
}

static void jh7110_clk_reg_rmw(struct jh7110_clk *clk, u32 mask, u32 value)
{
	struct jh7110_clk_priv *priv = jh7110_priv_from(clk);
	void __iomem *reg = jh7110_clk_reg_addr_get(clk);
	unsigned long flags;

	spin_lock_irqsave(&priv->rmw_lock, flags);
	value |= readl_relaxed(reg) & ~mask;
	writel_relaxed(value, reg);
	spin_unlock_irqrestore(&priv->rmw_lock, flags);
}

static int jh7110_clk_enable(struct clk_hw *hw)
{
	struct jh7110_clk *clk = jh7110_clk_from(hw);

	jh7110_clk_reg_rmw(clk, JH7110_CLK_ENABLE, JH7110_CLK_ENABLE);
	return 0;
}

static void jh7110_clk_disable(struct clk_hw *hw)
{
	struct jh7110_clk *clk = jh7110_clk_from(hw);

	jh7110_clk_reg_rmw(clk, JH7110_CLK_ENABLE, 0);
}

static int jh7110_clk_is_enabled(struct clk_hw *hw)
{
	struct jh7110_clk *clk = jh7110_clk_from(hw);

	return !!(jh7110_clk_reg_get(clk) & JH7110_CLK_ENABLE);
}

static unsigned long jh7110_clk_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct jh7110_clk *clk = jh7110_clk_from(hw);
	u32 div = jh7110_clk_reg_get(clk) & JH7110_CLK_DIV_MASK;

	return div ? parent_rate / div : 0;
}

static int jh7110_clk_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req)
{
	struct jh7110_clk *clk = jh7110_clk_from(hw);
	unsigned long parent = req->best_parent_rate;
	unsigned long rate = clamp(req->rate, req->min_rate, req->max_rate);
	unsigned long div = min_t(unsigned long, 
				DIV_ROUND_UP(parent, rate), clk->max_div);
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
	 *	div - 1 = ceil(parent / rate) - 1 < parent / rate
	 * and thus
	 *	min_rate <= rate < parent / (div - 1)
	 */
	if (result < req->min_rate && div > 1)
		result = parent / (div - 1);

	req->rate = result;
	return 0;
}

static int jh7110_clk_set_rate(struct clk_hw *hw,
				unsigned long rate,
				unsigned long parent_rate)
{
	struct jh7110_clk *clk = jh7110_clk_from(hw);
	unsigned long div = clamp(DIV_ROUND_CLOSEST(parent_rate, rate),
					1UL, (unsigned long)clk->max_div);

	jh7110_clk_reg_rmw(clk, JH7110_CLK_DIV_MASK, div);
	return 0;
}

static u8 jh7110_clk_get_parent(struct clk_hw *hw)
{
	struct jh7110_clk *clk = jh7110_clk_from(hw);
	u32 value = jh7110_clk_reg_get(clk);

	return (value & JH7110_CLK_MUX_MASK) >> JH7110_CLK_MUX_SHIFT;
}

static int jh7110_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct jh7110_clk *clk = jh7110_clk_from(hw);
	u32 value = (u32)index << JH7110_CLK_MUX_SHIFT;

	jh7110_clk_reg_rmw(clk, JH7110_CLK_MUX_MASK, value);
	return 0;
}

static int jh7110_clk_mux_determine_rate(struct clk_hw *hw,
					 struct clk_rate_request *req)
{
	return clk_mux_determine_rate_flags(hw, req, 0);
}

static int jh7110_clk_get_phase(struct clk_hw *hw)
{
	struct jh7110_clk *clk = jh7110_clk_from(hw);
	u32 value = jh7110_clk_reg_get(clk);

	return (value & JH7110_CLK_INVERT) ? 180 : 0;
}

static int jh7110_clk_set_phase(struct clk_hw *hw, int degrees)
{
	struct jh7110_clk *clk = jh7110_clk_from(hw);
	u32 value;

	if (degrees == 0)
		value = 0;
	else if (degrees == 180)
		value = JH7110_CLK_INVERT;
	else
		return -EINVAL;

	jh7110_clk_reg_rmw(clk, JH7110_CLK_INVERT, value);
	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void jh7110_clk_debug_init(struct clk_hw *hw, struct dentry *dentry)
{
	static const struct debugfs_reg32 jh7110_clk_reg = {
		.name = "CTRL",
		.offset = 0,
	};
	struct jh7110_clk *clk = jh7110_clk_from(hw);
	struct jh7110_clk_priv *priv = jh7110_priv_from(clk);
	struct debugfs_regset32 *regset;

	regset = devm_kzalloc(priv->dev, sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return;

	regset->regs = &jh7110_clk_reg;
	regset->nregs = 1;
	regset->base = jh7110_clk_reg_addr_get(clk);

	debugfs_create_regset32("registers", 0400, dentry, regset);
}
#else
#define jh7110_clk_debug_init NULL
#endif

static const struct clk_ops jh7110_clk_gate_ops = {
	.enable = jh7110_clk_enable,
	.disable = jh7110_clk_disable,
	.is_enabled = jh7110_clk_is_enabled,
	.debug_init = jh7110_clk_debug_init,
};

static const struct clk_ops jh7110_clk_div_ops = {
	.recalc_rate = jh7110_clk_recalc_rate,
	.determine_rate = jh7110_clk_determine_rate,
	.set_rate = jh7110_clk_set_rate,
	.debug_init = jh7110_clk_debug_init,
};

static const struct clk_ops jh7110_clk_gdiv_ops = {
	.enable = jh7110_clk_enable,
	.disable = jh7110_clk_disable,
	.is_enabled = jh7110_clk_is_enabled,
	.recalc_rate = jh7110_clk_recalc_rate,
	.determine_rate = jh7110_clk_determine_rate,
	.set_rate = jh7110_clk_set_rate,
	.debug_init = jh7110_clk_debug_init,
};

static const struct clk_ops jh7110_clk_mux_ops = {
	.determine_rate = jh7110_clk_mux_determine_rate,
	.set_parent = jh7110_clk_set_parent,
	.get_parent = jh7110_clk_get_parent,
	.debug_init = jh7110_clk_debug_init,
};

static const struct clk_ops jh7110_clk_gmux_ops = {
	.enable = jh7110_clk_enable,
	.disable = jh7110_clk_disable,
	.is_enabled = jh7110_clk_is_enabled,
	.determine_rate = jh7110_clk_mux_determine_rate,
	.set_parent = jh7110_clk_set_parent,
	.get_parent = jh7110_clk_get_parent,
	.debug_init = jh7110_clk_debug_init,
};

static const struct clk_ops jh7110_clk_mdiv_ops = {
	.recalc_rate = jh7110_clk_recalc_rate,
	.determine_rate = jh7110_clk_determine_rate,
	.get_parent = jh7110_clk_get_parent,
	.set_parent = jh7110_clk_set_parent,
	.set_rate = jh7110_clk_set_rate,
	.debug_init = jh7110_clk_debug_init,
};

static const struct clk_ops jh7110_clk_gmd_ops = {
	.enable = jh7110_clk_enable,
	.disable = jh7110_clk_disable,
	.is_enabled = jh7110_clk_is_enabled,
	.recalc_rate = jh7110_clk_recalc_rate,
	.determine_rate = jh7110_clk_determine_rate,
	.get_parent = jh7110_clk_get_parent,
	.set_parent = jh7110_clk_set_parent,
	.set_rate = jh7110_clk_set_rate,
	.debug_init = jh7110_clk_debug_init,
};

static const struct clk_ops jh7110_clk_inv_ops = {
	.get_phase = jh7110_clk_get_phase,
	.set_phase = jh7110_clk_set_phase,
	.debug_init = jh7110_clk_debug_init,
};

const struct clk_ops *starfive_jh7110_clk_ops(u32 max)
{
	const struct clk_ops *ops;

	if (max & JH7110_CLK_DIV_MASK) {
		if (max & JH7110_CLK_MUX_MASK) {
			if (max & JH7110_CLK_ENABLE)
				ops = &jh7110_clk_gmd_ops;
			else
				ops = &jh7110_clk_mdiv_ops;
		} else if (max & JH7110_CLK_ENABLE)
			ops = &jh7110_clk_gdiv_ops;
		else
			ops = &jh7110_clk_div_ops;
	} else if (max & JH7110_CLK_MUX_MASK) {
		if (max & JH7110_CLK_ENABLE)
			ops = &jh7110_clk_gmux_ops;
		else
			ops = &jh7110_clk_mux_ops;
	} else if (max & JH7110_CLK_ENABLE)
		ops = &jh7110_clk_gate_ops;
	else
		ops = &jh7110_clk_inv_ops;

	return ops;
}
EXPORT_SYMBOL_GPL(starfive_jh7110_clk_ops);

static struct clk_hw *jh7110_clk_get(struct of_phandle_args *clkspec, void *data)
{
	struct jh7110_clk_priv *priv = data;
	unsigned int idx = clkspec->args[0];

	if (idx < JH7110_PLL0_OUT)
		return &priv->reg[idx].hw;

	if (idx < JH7110_CLK_END)
		return priv->pll[PLL_OF(idx)];

	return ERR_PTR(-EINVAL);
}


static int __init clk_starfive_jh7110_probe(struct platform_device *pdev)
{
	struct jh7110_clk_priv *priv;
	int ret = 0;

	priv = devm_kzalloc(&pdev->dev, struct_size(priv, reg, JH7110_PLL0_OUT),
					GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->rmw_lock);
	priv->dev = &pdev->dev;

	ret = clk_starfive_jh7110_sys_init(pdev, priv);
	if (ret)
		return ret;

	ret = clk_starfive_jh7110_stg_init(pdev, priv);
	if (ret)
		return ret;

	ret = clk_starfive_jh7110_aon_init(pdev, priv);
	if (ret)
		return ret;

	ret = devm_of_clk_add_hw_provider(priv->dev, jh7110_clk_get, priv);
	if (ret)
		return ret;

	dev_info(&pdev->dev,"starfive JH7110 clkgen init successfully.");
	return 0;
}

static const struct of_device_id clk_starfive_jh7110_match[] = {	
	{.compatible = "starfive,jh7110-clkgen"},
	{ /* sentinel */ }
};

static struct platform_driver clk_starfive_jh7110_driver = {
	.driver = {
		.name = "clk-starfive-jh7110",
		.of_match_table = clk_starfive_jh7110_match,
	},
};
builtin_platform_driver_probe(clk_starfive_jh7110_driver,
			clk_starfive_jh7110_probe);

MODULE_AUTHOR("Xingyu Wu <xingyu.wu@starfivetech.com>");
MODULE_DESCRIPTION("StarFive JH7110 sysgen clock driver");
MODULE_LICENSE("GPL");
