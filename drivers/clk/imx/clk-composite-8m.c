// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 NXP
 */

#include <linux/clk-provider.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "clk.h"

#define PCG_PREDIV_SHIFT	16
#define PCG_PREDIV_WIDTH	3
#define PCG_PREDIV_MAX		8

#define PCG_DIV_SHIFT		0
#define PCG_DIV_WIDTH		6
#define PCG_DIV_MAX		64

#define PCG_PCS_SHIFT		24
#define PCG_PCS_MASK		0x7

#define PCG_CGC_SHIFT		28

static unsigned long imx8m_clk_composite_divider_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	unsigned long prediv_rate;
	unsigned int prediv_value;
	unsigned int div_value;

	prediv_value = readl(divider->reg) >> divider->shift;
	prediv_value &= clk_div_mask(divider->width);

	prediv_rate = divider_recalc_rate(hw, parent_rate, prediv_value,
						NULL, divider->flags,
						divider->width);

	div_value = readl(divider->reg) >> PCG_DIV_SHIFT;
	div_value &= clk_div_mask(PCG_DIV_WIDTH);

	return divider_recalc_rate(hw, prediv_rate, div_value, NULL,
				   divider->flags, PCG_DIV_WIDTH);
}

static int imx8m_clk_composite_compute_dividers(unsigned long rate,
						unsigned long parent_rate,
						int *prediv, int *postdiv)
{
	int div1, div2;
	int error = INT_MAX;
	int ret = -EINVAL;

	*prediv = 1;
	*postdiv = 1;

	for (div1 = 1; div1 <= PCG_PREDIV_MAX; div1++) {
		for (div2 = 1; div2 <= PCG_DIV_MAX; div2++) {
			int new_error = ((parent_rate / div1) / div2) - rate;

			if (abs(new_error) < abs(error)) {
				*prediv = div1;
				*postdiv = div2;
				error = new_error;
				ret = 0;
			}
		}
	}
	return ret;
}

static long imx8m_clk_composite_divider_round_rate(struct clk_hw *hw,
						unsigned long rate,
						unsigned long *prate)
{
	int prediv_value;
	int div_value;

	imx8m_clk_composite_compute_dividers(rate, *prate,
						&prediv_value, &div_value);
	rate = DIV_ROUND_UP(*prate, prediv_value);

	return DIV_ROUND_UP(rate, div_value);

}

static int imx8m_clk_composite_divider_set_rate(struct clk_hw *hw,
					unsigned long rate,
					unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	unsigned long flags = 0;
	int prediv_value;
	int div_value;
	int ret;
	u32 val;

	ret = imx8m_clk_composite_compute_dividers(rate, parent_rate,
						&prediv_value, &div_value);
	if (ret)
		return -EINVAL;

	spin_lock_irqsave(divider->lock, flags);

	val = readl(divider->reg);
	val &= ~((clk_div_mask(divider->width) << divider->shift) |
			(clk_div_mask(PCG_DIV_WIDTH) << PCG_DIV_SHIFT));

	val |= (u32)(prediv_value  - 1) << divider->shift;
	val |= (u32)(div_value - 1) << PCG_DIV_SHIFT;
	writel(val, divider->reg);

	spin_unlock_irqrestore(divider->lock, flags);

	return ret;
}

static const struct clk_ops imx8m_clk_composite_divider_ops = {
	.recalc_rate = imx8m_clk_composite_divider_recalc_rate,
	.round_rate = imx8m_clk_composite_divider_round_rate,
	.set_rate = imx8m_clk_composite_divider_set_rate,
};

struct clk *imx8m_clk_composite_flags(const char *name,
					const char * const *parent_names,
					int num_parents, void __iomem *reg,
					unsigned long flags)
{
	struct clk_hw *hw = ERR_PTR(-ENOMEM), *mux_hw;
	struct clk_hw *div_hw, *gate_hw;
	struct clk_divider *div = NULL;
	struct clk_gate *gate = NULL;
	struct clk_mux *mux = NULL;

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		goto fail;

	mux_hw = &mux->hw;
	mux->reg = reg;
	mux->shift = PCG_PCS_SHIFT;
	mux->mask = PCG_PCS_MASK;

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		goto fail;

	div_hw = &div->hw;
	div->reg = reg;
	div->shift = PCG_PREDIV_SHIFT;
	div->width = PCG_PREDIV_WIDTH;
	div->lock = &imx_ccm_lock;
	div->flags = CLK_DIVIDER_ROUND_CLOSEST;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		goto fail;

	gate_hw = &gate->hw;
	gate->reg = reg;
	gate->bit_idx = PCG_CGC_SHIFT;

	hw = clk_hw_register_composite(NULL, name, parent_names, num_parents,
			mux_hw, &clk_mux_ops, div_hw,
			&imx8m_clk_composite_divider_ops,
			gate_hw, &clk_gate_ops, flags);
	if (IS_ERR(hw))
		goto fail;

	return hw->clk;

fail:
	kfree(gate);
	kfree(div);
	kfree(mux);
	return ERR_CAST(hw);
}
