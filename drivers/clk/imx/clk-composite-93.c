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
#include <linux/slab.h>

#include "clk.h"

#define CCM_DIV_SHIFT	0
#define CCM_DIV_WIDTH	8
#define CCM_MUX_SHIFT	8
#define CCM_MUX_MASK	3
#define CCM_OFF_SHIFT	24

#define AUTHEN_OFFSET	0x30
#define TZ_NS_SHIFT	9
#define TZ_NS_MASK	BIT(9)

struct clk_hw *imx93_clk_composite_flags(const char *name, const char * const *parent_names,
					 int num_parents, void __iomem *reg,
					 unsigned long flags)
{
	struct clk_hw *hw = ERR_PTR(-ENOMEM), *mux_hw;
	struct clk_hw *div_hw, *gate_hw;
	struct clk_divider *div = NULL;
	struct clk_gate *gate = NULL;
	struct clk_mux *mux = NULL;
	bool clk_ro = false;

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

	if (!(readl(reg + AUTHEN_OFFSET) & TZ_NS_MASK))
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
					       mux_hw, &clk_mux_ops, div_hw,
					       &clk_divider_ops, gate_hw,
					       &clk_gate_ops, flags | CLK_SET_RATE_NO_REPARENT);
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
