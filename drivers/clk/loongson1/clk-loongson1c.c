// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Yang Ling <gnaygnil@gmail.com>
 */

#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/io.h>

#include <loongson1.h>
#include "clk.h"

#define OSC		(24 * 1000000)
#define DIV_APB		1

static DEFINE_SPINLOCK(_lock);

static unsigned long ls1x_pll_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	u32 pll, rate;

	pll = __raw_readl(LS1X_CLK_PLL_FREQ);
	rate = ((pll >> 8) & 0xff) + ((pll >> 16) & 0xff);
	rate *= OSC;
	rate >>= 2;

	return rate;
}

static const struct clk_ops ls1x_pll_clk_ops = {
	.recalc_rate = ls1x_pll_recalc_rate,
};

static const struct clk_div_table ahb_div_table[] = {
	[0] = { .val = 0, .div = 2 },
	[1] = { .val = 1, .div = 4 },
	[2] = { .val = 2, .div = 3 },
	[3] = { .val = 3, .div = 3 },
};

void __init ls1x_clk_init(void)
{
	struct clk_hw *hw;

	hw = clk_hw_register_fixed_rate(NULL, "osc_clk", NULL, 0, OSC);
	clk_hw_register_clkdev(hw, "osc_clk", NULL);

	/* clock derived from 24 MHz OSC clk */
	hw = clk_hw_register_pll(NULL, "pll_clk", "osc_clk",
				&ls1x_pll_clk_ops, 0);
	clk_hw_register_clkdev(hw, "pll_clk", NULL);

	hw = clk_hw_register_divider(NULL, "cpu_clk_div", "pll_clk",
				   CLK_GET_RATE_NOCACHE, LS1X_CLK_PLL_DIV,
				   DIV_CPU_SHIFT, DIV_CPU_WIDTH,
				   CLK_DIVIDER_ONE_BASED |
				   CLK_DIVIDER_ROUND_CLOSEST, &_lock);
	clk_hw_register_clkdev(hw, "cpu_clk_div", NULL);
	hw = clk_hw_register_fixed_factor(NULL, "cpu_clk", "cpu_clk_div",
					0, 1, 1);
	clk_hw_register_clkdev(hw, "cpu_clk", NULL);

	hw = clk_hw_register_divider(NULL, "dc_clk_div", "pll_clk",
				   0, LS1X_CLK_PLL_DIV, DIV_DC_SHIFT,
				   DIV_DC_WIDTH, CLK_DIVIDER_ONE_BASED, &_lock);
	clk_hw_register_clkdev(hw, "dc_clk_div", NULL);
	hw = clk_hw_register_fixed_factor(NULL, "dc_clk", "dc_clk_div",
					0, 1, 1);
	clk_hw_register_clkdev(hw, "dc_clk", NULL);

	hw = clk_hw_register_divider_table(NULL, "ahb_clk_div", "cpu_clk_div",
				0, LS1X_CLK_PLL_FREQ, DIV_DDR_SHIFT,
				DIV_DDR_WIDTH, CLK_DIVIDER_ALLOW_ZERO,
				ahb_div_table, &_lock);
	clk_hw_register_clkdev(hw, "ahb_clk_div", NULL);
	hw = clk_hw_register_fixed_factor(NULL, "ahb_clk", "ahb_clk_div",
					0, 1, 1);
	clk_hw_register_clkdev(hw, "ahb_clk", NULL);
	clk_hw_register_clkdev(hw, "ls1x-dma", NULL);
	clk_hw_register_clkdev(hw, "stmmaceth", NULL);

	/* clock derived from AHB clk */
	hw = clk_hw_register_fixed_factor(NULL, "apb_clk", "ahb_clk", 0, 1,
					DIV_APB);
	clk_hw_register_clkdev(hw, "apb_clk", NULL);
	clk_hw_register_clkdev(hw, "ls1x-ac97", NULL);
	clk_hw_register_clkdev(hw, "ls1x-i2c", NULL);
	clk_hw_register_clkdev(hw, "ls1x-nand", NULL);
	clk_hw_register_clkdev(hw, "ls1x-pwmtimer", NULL);
	clk_hw_register_clkdev(hw, "ls1x-spi", NULL);
	clk_hw_register_clkdev(hw, "ls1x-wdt", NULL);
	clk_hw_register_clkdev(hw, "serial8250", NULL);
}
