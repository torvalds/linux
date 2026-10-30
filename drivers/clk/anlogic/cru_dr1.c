// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2025 Anlogic, Inc.
 * Copyright (C) 2026 Junhui Liu <junhui.liu@pigmoral.tech>
 */

#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "cru_dr1.h"

static unsigned long cru_pll_nm_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct cru_pll *pll = hw_to_cru_pll(hw);
	u32 mult, div;

	div = FIELD_GET(GENMASK(6, 0), readl(pll->reg)) + 1;
	mult = FIELD_GET(GENMASK(6, 0), readl(pll->reg + 4)) + 1;

	return parent_rate * mult / div;
}

const struct clk_ops dr1_cru_pll_nm_ops = {
	.recalc_rate = cru_pll_nm_recalc_rate,
};
EXPORT_SYMBOL_NS_GPL(dr1_cru_pll_nm_ops, "CLK_ANLOGIC");

static unsigned long cru_pll_c_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct cru_pll *pll = hw_to_cru_pll(hw);
	u32 div;

	div = FIELD_GET(GENMASK(30, 24), readl(pll->reg)) + 1;

	return parent_rate / div;
}

const struct clk_ops dr1_cru_pll_c_ops = {
	.recalc_rate = cru_pll_c_recalc_rate,
};
EXPORT_SYMBOL_NS_GPL(dr1_cru_pll_c_ops, "CLK_ANLOGIC");

static void cru_div_gate_endisable(struct clk_hw *hw, int enable)
{
	struct cru_div_gate *div_gate = hw_to_cru_div_gate(hw);
	struct clk_divider *divider = &div_gate->divider;
	u32 reg;

	reg = readl(divider->reg);
	reg &= ~(clk_div_mask(divider->width) << divider->shift);

	if (enable)
		reg |= div_gate->val << divider->shift;

	writel(reg, divider->reg);
}

static int cru_div_gate_enable(struct clk_hw *hw)
{
	cru_div_gate_endisable(hw, 1);

	return 0;
}

static void cru_div_gate_disable(struct clk_hw *hw)
{
	cru_div_gate_endisable(hw, 0);
}

static int cru_div_gate_is_enabled(struct clk_hw *hw)
{
	struct cru_div_gate *div_gate = hw_to_cru_div_gate(hw);
	struct clk_divider *divider = &div_gate->divider;
	u32 val;

	val = readl(divider->reg) >> divider->shift;
	val &= clk_div_mask(divider->width);

	return !!val;
}

static unsigned long cru_div_gate_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct cru_div_gate *div_gate = hw_to_cru_div_gate(hw);
	struct clk_divider *divider = &div_gate->divider;
	unsigned int val;

	val = readl(divider->reg) >> divider->shift;
	val &= clk_div_mask(divider->width);

	if (val < div_gate->min)
		return 0;

	return divider_recalc_rate(hw, parent_rate, val, divider->table,
				   divider->flags, divider->width);
}

static int cru_div_gate_determine_rate(struct clk_hw *hw,
				       struct clk_rate_request *req)
{
	struct cru_div_gate *div_gate = hw_to_cru_div_gate(hw);
	struct clk_divider *divider = &div_gate->divider;
	unsigned long maxdiv, mindiv;
	int div = 0;

	maxdiv = clk_div_mask(divider->width) + 1;
	mindiv = div_gate->min + 1;

	div = DIV_ROUND_UP_ULL(req->best_parent_rate, req->rate);
	div = div > maxdiv ? maxdiv : div;
	div = div < mindiv ? mindiv : div;

	req->rate = DIV_ROUND_UP_ULL(req->best_parent_rate, div);

	return 0;
}

static int cru_div_gate_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct cru_div_gate *div_gate = hw_to_cru_div_gate(hw);
	struct clk_divider *divider = &div_gate->divider;
	int value;
	u32 reg;

	if (!__clk_is_enabled(hw->clk))
		return 0;

	value = divider_get_val(rate, parent_rate, divider->table,
				divider->width, divider->flags);
	if (value < 0)
		return value;

	if (value < div_gate->min)
		value = div_gate->min;

	reg = readl(divider->reg);
	reg &= ~(clk_div_mask(divider->width) << divider->shift);
	reg |= (u32)value << divider->shift;
	writel(reg, divider->reg);

	div_gate->val = value;

	return 0;
}

static int cru_div_gate_init(struct clk_hw *hw)
{
	struct cru_div_gate *div_gate = hw_to_cru_div_gate(hw);
	struct clk_divider *divider = &div_gate->divider;
	u32 val;

	val = readl(divider->reg) >> divider->shift;
	val &= clk_div_mask(divider->width);
	div_gate->val = val;

	return 0;
}

const struct clk_ops dr1_cru_div_gate_ops = {
	.enable = cru_div_gate_enable,
	.disable = cru_div_gate_disable,
	.is_enabled = cru_div_gate_is_enabled,
	.recalc_rate = cru_div_gate_recalc_rate,
	.determine_rate = cru_div_gate_determine_rate,
	.set_rate = cru_div_gate_set_rate,
	.init = cru_div_gate_init,
};
EXPORT_SYMBOL_NS_GPL(dr1_cru_div_gate_ops, "CLK_ANLOGIC");

int dr1_cru_clk_register(struct device *dev, void __iomem *base,
			 const struct cru_clk *clks, int nr_clks)
{
	struct clk_hw_onecell_data *priv;
	int i, ret;

	priv = devm_kzalloc(dev, struct_size(priv, hws, nr_clks), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	for (i = 0; i < nr_clks; i++) {
		const struct cru_clk *clk = &clks[i];

		if (clk->reg)
			*(clk->reg) += (uintptr_t)base;

		ret = devm_clk_hw_register(dev, clk->hw);
		if (ret)
			return ret;

		priv->hws[i] = clk->hw;
	}

	priv->num = nr_clks;

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, priv);
	if (ret)
		dev_err(dev, "failed to add clock hardware provider\n");

	return ret;
}
EXPORT_SYMBOL_NS_GPL(dr1_cru_clk_register, "CLK_ANLOGIC");

int dr1_cru_reset_register(struct device *dev, void __iomem *base)
{
	struct auxiliary_device *adev;

	adev = devm_auxiliary_device_create(dev, "reset", base);
	if (!adev)
		return -ENODEV;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(dr1_cru_reset_register, "CLK_ANLOGIC");

MODULE_DESCRIPTION("Anlogic DR1 CRU driver");
MODULE_LICENSE("GPL");
