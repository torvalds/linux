/*
 * Copyright (c) 2012 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>

#include <loongson1.h>

#define OSC	33

static DEFINE_SPINLOCK(_lock);

static int ls1x_pll_clk_enable(struct clk_hw *hw)
{
	return 0;
}

static void ls1x_pll_clk_disable(struct clk_hw *hw)
{
}

static unsigned long ls1x_pll_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	u32 pll, rate;

	pll = __raw_readl(LS1X_CLK_PLL_FREQ);
	rate = ((12 + (pll & 0x3f)) * 1000000) +
		((((pll >> 8) & 0x3ff) * 1000000) >> 10);
	rate *= OSC;
	rate >>= 1;

	return rate;
}

static const struct clk_ops ls1x_pll_clk_ops = {
	.enable = ls1x_pll_clk_enable,
	.disable = ls1x_pll_clk_disable,
	.recalc_rate = ls1x_pll_recalc_rate,
};

static struct clk * __init clk_register_pll(struct device *dev,
	 const char *name, const char *parent_name, unsigned long flags)
{
	struct clk_hw *hw;
	struct clk *clk;
	struct clk_init_data init;

	/* allocate the divider */
	hw = kzalloc(sizeof(struct clk_hw), GFP_KERNEL);
	if (!hw) {
		pr_err("%s: could not allocate clk_hw\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &ls1x_pll_clk_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);
	hw->init = &init;

	/* register the clock */
	clk = clk_register(dev, hw);

	if (IS_ERR(clk))
		kfree(hw);

	return clk;
}

void __init ls1x_clk_init(void)
{
	struct clk *clk;

	clk = clk_register_pll(NULL, "pll_clk", NULL, CLK_IS_ROOT);
	clk_prepare_enable(clk);

	clk = clk_register_divider(NULL, "cpu_clk", "pll_clk",
			CLK_SET_RATE_PARENT, LS1X_CLK_PLL_DIV, DIV_CPU_SHIFT,
			DIV_CPU_WIDTH, CLK_DIVIDER_ONE_BASED, &_lock);
	clk_prepare_enable(clk);
	clk_register_clkdev(clk, "cpu", NULL);

	clk = clk_register_divider(NULL, "dc_clk", "pll_clk",
			CLK_SET_RATE_PARENT, LS1X_CLK_PLL_DIV, DIV_DC_SHIFT,
			DIV_DC_WIDTH, CLK_DIVIDER_ONE_BASED, &_lock);
	clk_prepare_enable(clk);
	clk_register_clkdev(clk, "dc", NULL);

	clk = clk_register_divider(NULL, "ahb_clk", "pll_clk",
			CLK_SET_RATE_PARENT, LS1X_CLK_PLL_DIV, DIV_DDR_SHIFT,
			DIV_DDR_WIDTH, CLK_DIVIDER_ONE_BASED, &_lock);
	clk_prepare_enable(clk);
	clk_register_clkdev(clk, "ahb", NULL);
	clk_register_clkdev(clk, "stmmaceth", NULL);

	clk = clk_register_fixed_factor(NULL, "apb_clk", "ahb_clk", 0, 1, 2);
	clk_prepare_enable(clk);
	clk_register_clkdev(clk, "apb", NULL);
	clk_register_clkdev(clk, "serial8250", NULL);
}
