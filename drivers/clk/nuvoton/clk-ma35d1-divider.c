// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Nuvoton Technology Corp.
 * Author: Chi-Fang Li <cfli0@nuvoton.com>
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>

#include "clk-ma35d1.h"

struct ma35d1_adc_clk_div {
	struct clk_hw hw;
	void __iomem *reg;
	u8 shift;
	u8 width;
	u32 mask;
	const struct clk_div_table *table;
	/* protects concurrent access to clock divider registers */
	spinlock_t *lock;
};

static inline struct ma35d1_adc_clk_div *to_ma35d1_adc_clk_div(struct clk_hw *_hw)
{
	return container_of(_hw, struct ma35d1_adc_clk_div, hw);
}

static unsigned long ma35d1_clkdiv_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	unsigned int val;
	struct ma35d1_adc_clk_div *dclk = to_ma35d1_adc_clk_div(hw);

	val = readl_relaxed(dclk->reg) >> dclk->shift;
	val &= clk_div_mask(dclk->width);
	val += 1;
	return divider_recalc_rate(hw, parent_rate, val, dclk->table,
				   CLK_DIVIDER_ROUND_CLOSEST, dclk->width);
}

static int ma35d1_clkdiv_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req)
{
	struct ma35d1_adc_clk_div *dclk = to_ma35d1_adc_clk_div(hw);

	req->rate = divider_round_rate(hw, req->rate, &req->best_parent_rate,
				       dclk->table, dclk->width,
				       CLK_DIVIDER_ROUND_CLOSEST);

	return 0;
}

static int ma35d1_clkdiv_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate)
{
	int value;
	unsigned long flags = 0;
	u32 data;
	struct ma35d1_adc_clk_div *dclk = to_ma35d1_adc_clk_div(hw);

	value = divider_get_val(rate, parent_rate, dclk->table,
				dclk->width, CLK_DIVIDER_ROUND_CLOSEST);

	spin_lock_irqsave(dclk->lock, flags);

	data = readl_relaxed(dclk->reg);
	data &= ~(clk_div_mask(dclk->width) << dclk->shift);
	data |= (value - 1) << dclk->shift;
	data |= dclk->mask;
	writel_relaxed(data, dclk->reg);

	spin_unlock_irqrestore(dclk->lock, flags);
	return 0;
}

static const struct clk_ops ma35d1_adc_clkdiv_ops = {
	.recalc_rate = ma35d1_clkdiv_recalc_rate,
	.determine_rate = ma35d1_clkdiv_determine_rate,
	.set_rate = ma35d1_clkdiv_set_rate,
};

struct clk_hw *ma35d1_reg_adc_clkdiv(struct device *dev, const char *name,
				     struct clk_hw *parent_hw, spinlock_t *lock,
				     unsigned long flags, void __iomem *reg,
				     u8 shift, u8 width, u32 mask_bit)
{
	struct ma35d1_adc_clk_div *div;
	struct clk_init_data init;
	struct clk_div_table *table;
	struct clk_parent_data pdata = { .index = 0 };
	u32 max_div, min_div;
	struct clk_hw *hw;
	int ret;
	int i;

	div = devm_kzalloc(dev, sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	max_div = clk_div_mask(width) + 1;
	min_div = 1;

	table = devm_kcalloc(dev, max_div + 1, sizeof(*table), GFP_KERNEL);
	if (!table)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < max_div; i++) {
		table[i].val = min_div + i;
		table[i].div = 2 * table[i].val;
	}
	table[max_div].val = 0;
	table[max_div].div = 0;

	memset(&init, 0, sizeof(init));
	init.name = name;
	init.ops = &ma35d1_adc_clkdiv_ops;
	init.flags |= flags;
	pdata.hw = parent_hw;
	init.parent_data = &pdata;
	init.num_parents = 1;

	div->reg = reg;
	div->shift = shift;
	div->width = width;
	div->mask = mask_bit ? BIT(mask_bit) : 0;
	div->lock = lock;
	div->hw.init = &init;
	div->table = table;

	hw = &div->hw;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ERR_PTR(ret);
	return hw;
}
EXPORT_SYMBOL_GPL(ma35d1_reg_adc_clkdiv);
