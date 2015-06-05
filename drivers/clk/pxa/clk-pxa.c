/*
 * Marvell PXA family clocks
 *
 * Copyright (C) 2014 Robert Jarzmik
 *
 * Common clock code for PXA clocks ("CKEN" type clocks + DT)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>

#include <dt-bindings/clock/pxa-clock.h>
#include "clk-pxa.h"

DEFINE_SPINLOCK(lock);

static struct clk *pxa_clocks[CLK_MAX];
static struct clk_onecell_data onecell_data = {
	.clks = pxa_clocks,
	.clk_num = CLK_MAX,
};

struct pxa_clk {
	struct clk_hw hw;
	struct clk_fixed_factor lp;
	struct clk_fixed_factor hp;
	struct clk_gate gate;
	bool (*is_in_low_power)(void);
};

#define to_pxa_clk(_hw) container_of(_hw, struct pxa_clk, hw)

static unsigned long cken_recalc_rate(struct clk_hw *hw,
				      unsigned long parent_rate)
{
	struct pxa_clk *pclk = to_pxa_clk(hw);
	struct clk_fixed_factor *fix;

	if (!pclk->is_in_low_power || pclk->is_in_low_power())
		fix = &pclk->lp;
	else
		fix = &pclk->hp;
	__clk_hw_set_clk(&fix->hw, hw);
	return clk_fixed_factor_ops.recalc_rate(&fix->hw, parent_rate);
}

static struct clk_ops cken_rate_ops = {
	.recalc_rate = cken_recalc_rate,
};

static u8 cken_get_parent(struct clk_hw *hw)
{
	struct pxa_clk *pclk = to_pxa_clk(hw);

	if (!pclk->is_in_low_power)
		return 0;
	return pclk->is_in_low_power() ? 0 : 1;
}

static struct clk_ops cken_mux_ops = {
	.get_parent = cken_get_parent,
	.set_parent = dummy_clk_set_parent,
};

void __init clkdev_pxa_register(int ckid, const char *con_id,
				const char *dev_id, struct clk *clk)
{
	if (!IS_ERR(clk) && (ckid != CLK_NONE))
		pxa_clocks[ckid] = clk;
	if (!IS_ERR(clk))
		clk_register_clkdev(clk, con_id, dev_id);
}

int __init clk_pxa_cken_init(const struct desc_clk_cken *clks, int nb_clks)
{
	int i;
	struct pxa_clk *pxa_clk;
	struct clk *clk;

	for (i = 0; i < nb_clks; i++) {
		pxa_clk = kzalloc(sizeof(*pxa_clk), GFP_KERNEL);
		pxa_clk->is_in_low_power = clks[i].is_in_low_power;
		pxa_clk->lp = clks[i].lp;
		pxa_clk->hp = clks[i].hp;
		pxa_clk->gate = clks[i].gate;
		pxa_clk->gate.lock = &lock;
		clk = clk_register_composite(NULL, clks[i].name,
					     clks[i].parent_names, 2,
					     &pxa_clk->hw, &cken_mux_ops,
					     &pxa_clk->hw, &cken_rate_ops,
					     &pxa_clk->gate.hw, &clk_gate_ops,
					     clks[i].flags);
		clkdev_pxa_register(clks[i].ckid, clks[i].con_id,
				    clks[i].dev_id, clk);
	}
	return 0;
}

void __init clk_pxa_dt_common_init(struct device_node *np)
{
	of_clk_add_provider(np, of_clk_src_onecell_get, &onecell_data);
}
