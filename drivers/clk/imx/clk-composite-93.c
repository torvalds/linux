// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021 NXP
 *
 * Peng Fan <peng.fan@nxp.com>
 */

#include <linux/clk-provider.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/slab.h>

#include "clk.h"

#define TIMEOUT_US	500U

#define CCM_DIV_SHIFT	0
#define CCM_DIV_WIDTH	8
#define CCM_MUX_SHIFT	8
#define CCM_MUX_MASK	3
#define CCM_OFF_SHIFT	24
#define CCM_BUSY_SHIFT	28

#define STAT_OFFSET	0x4
#define AUTHEN_OFFSET	0x30
#define TZ_NS_SHIFT	9
#define TZ_NS_MASK	BIT(9)

#define WHITE_LIST_SHIFT	16

static int imx93_clk_composite_wait_ready(struct clk_hw *hw, void __iomem *reg)
{
	int ret;
	u32 val;

	ret = readl_poll_timeout_atomic(reg + STAT_OFFSET, val, !(val & BIT(CCM_BUSY_SHIFT)),
					0, TIMEOUT_US);
	if (ret)
		pr_err("Slice[%s] busy timeout\n", clk_hw_get_name(hw));

	return ret;
}

static void imx93_clk_composite_gate_endisable(struct clk_hw *hw, int enable)
{
	struct clk_gate *gate = to_clk_gate(hw);
	unsigned long flags;
	u32 reg;

	if (gate->lock)
		spin_lock_irqsave(gate->lock, flags);

	reg = readl(gate->reg);

	if (enable)
		reg &= ~BIT(gate->bit_idx);
	else
		reg |= BIT(gate->bit_idx);

	writel(reg, gate->reg);

	imx93_clk_composite_wait_ready(hw, gate->reg);

	if (gate->lock)
		spin_unlock_irqrestore(gate->lock, flags);
}

static int imx93_clk_composite_gate_enable(struct clk_hw *hw)
{
	imx93_clk_composite_gate_endisable(hw, 1);

	return 0;
}

static void imx93_clk_composite_gate_disable(struct clk_hw *hw)
{
	imx93_clk_composite_gate_endisable(hw, 0);
}

static const struct clk_ops imx93_clk_composite_gate_ops = {
	.enable = imx93_clk_composite_gate_enable,
	.disable = imx93_clk_composite_gate_disable,
	.is_enabled = clk_gate_is_enabled,
};

static unsigned long
imx93_clk_composite_divider_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	return clk_divider_ops.recalc_rate(hw, parent_rate);
}

static long
imx93_clk_composite_divider_round_rate(struct clk_hw *hw, unsigned long rate, unsigned long *prate)
{
	return clk_divider_ops.round_rate(hw, rate, prate);
}

static int
imx93_clk_composite_divider_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	return clk_divider_ops.determine_rate(hw, req);
}

static int imx93_clk_composite_divider_set_rate(struct clk_hw *hw, unsigned long rate,
						unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	int value;
	unsigned long flags = 0;
	u32 val;
	int ret;

	value = divider_get_val(rate, parent_rate, divider->table, divider->width, divider->flags);
	if (value < 0)
		return value;

	if (divider->lock)
		spin_lock_irqsave(divider->lock, flags);

	val = readl(divider->reg);
	val &= ~(clk_div_mask(divider->width) << divider->shift);
	val |= (u32)value << divider->shift;
	writel(val, divider->reg);

	ret = imx93_clk_composite_wait_ready(hw, divider->reg);

	if (divider->lock)
		spin_unlock_irqrestore(divider->lock, flags);

	return ret;
}

static const struct clk_ops imx93_clk_composite_divider_ops = {
	.recalc_rate = imx93_clk_composite_divider_recalc_rate,
	.round_rate = imx93_clk_composite_divider_round_rate,
	.determine_rate = imx93_clk_composite_divider_determine_rate,
	.set_rate = imx93_clk_composite_divider_set_rate,
};

static u8 imx93_clk_composite_mux_get_parent(struct clk_hw *hw)
{
	return clk_mux_ops.get_parent(hw);
}

static int imx93_clk_composite_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_mux *mux = to_clk_mux(hw);
	u32 val = clk_mux_index_to_val(mux->table, mux->flags, index);
	unsigned long flags = 0;
	u32 reg;
	int ret;

	if (mux->lock)
		spin_lock_irqsave(mux->lock, flags);

	reg = readl(mux->reg);
	reg &= ~(mux->mask << mux->shift);
	val = val << mux->shift;
	reg |= val;
	writel(reg, mux->reg);

	ret = imx93_clk_composite_wait_ready(hw, mux->reg);

	if (mux->lock)
		spin_unlock_irqrestore(mux->lock, flags);

	return ret;
}

static int
imx93_clk_composite_mux_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	return clk_mux_ops.determine_rate(hw, req);
}

static const struct clk_ops imx93_clk_composite_mux_ops = {
	.get_parent = imx93_clk_composite_mux_get_parent,
	.set_parent = imx93_clk_composite_mux_set_parent,
	.determine_rate = imx93_clk_composite_mux_determine_rate,
};

struct clk_hw *imx93_clk_composite_flags(const char *name, const char * const *parent_names,
					 int num_parents, void __iomem *reg, u32 domain_id,
					 unsigned long flags)
{
	struct clk_hw *hw = ERR_PTR(-ENOMEM), *mux_hw;
	struct clk_hw *div_hw, *gate_hw;
	struct clk_divider *div = NULL;
	struct clk_gate *gate = NULL;
	struct clk_mux *mux = NULL;
	bool clk_ro = false;
	u32 authen;

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		goto fail;

	mux_hw = &mux->hw;
	mux->reg = reg;
	mux->shift = CCM_MUX_SHIFT;
	mux->mask = CCM_MUX_MASK;
	mux->lock = &imx_ccm_lock;

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		goto fail;

	div_hw = &div->hw;
	div->reg = reg;
	div->shift = CCM_DIV_SHIFT;
	div->width = CCM_DIV_WIDTH;
	div->lock = &imx_ccm_lock;
	div->flags = CLK_DIVIDER_ROUND_CLOSEST;

	authen = readl(reg + AUTHEN_OFFSET);
	if (!(authen & TZ_NS_MASK) || !(authen & BIT(WHITE_LIST_SHIFT + domain_id)))
		clk_ro = true;

	if (clk_ro) {
		hw = clk_hw_register_composite(NULL, name, parent_names, num_parents,
					       mux_hw, &clk_mux_ro_ops, div_hw,
					       &clk_divider_ro_ops, NULL, NULL, flags);
	} else {
		gate = kzalloc(sizeof(*gate), GFP_KERNEL);
		if (!gate)
			goto fail;

		gate_hw = &gate->hw;
		gate->reg = reg;
		gate->bit_idx = CCM_OFF_SHIFT;
		gate->lock = &imx_ccm_lock;
		gate->flags = CLK_GATE_SET_TO_DISABLE;

		hw = clk_hw_register_composite(NULL, name, parent_names, num_parents,
					       mux_hw, &imx93_clk_composite_mux_ops, div_hw,
					       &imx93_clk_composite_divider_ops, gate_hw,
					       &imx93_clk_composite_gate_ops,
					       flags | CLK_SET_RATE_NO_REPARENT);
	}

	if (IS_ERR(hw))
		goto fail;

	return hw;

fail:
	kfree(gate);
	kfree(div);
	kfree(mux);
	return ERR_CAST(hw);
}
EXPORT_SYMBOL_GPL(imx93_clk_composite_flags);
