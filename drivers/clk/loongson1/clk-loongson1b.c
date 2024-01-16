// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2012-2016 Zhang, Keguang <keguang.zhang@gmail.com>
 */

#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/err.h>

#include <loongson1.h>
#include "clk.h"

#define OSC		(33 * 1000000)
#define DIV_APB		2

static DEFINE_SPINLOCK(_lock);

static unsigned long ls1x_pll_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	u32 pll, rate;

	pll = __raw_readl(LS1X_CLK_PLL_FREQ);
	rate = 12 + (pll & GENMASK(5, 0));
	rate *= OSC;
	rate >>= 1;

	return rate;
}

static const struct clk_ops ls1x_pll_clk_ops = {
	.recalc_rate = ls1x_pll_recalc_rate,
};

static const char *const cpu_parents[] = { "cpu_clk_div", "osc_clk", };
static const char *const ahb_parents[] = { "ahb_clk_div", "osc_clk", };
static const char *const dc_parents[] = { "dc_clk_div", "osc_clk", };

void __init ls1x_clk_init(void)
{
	struct clk_hw *hw;

	hw = clk_hw_register_fixed_rate(NULL, "osc_clk", NULL, 0, OSC);
	clk_hw_register_clkdev(hw, "osc_clk", NULL);

	/* clock derived from 33 MHz OSC clk */
	hw = clk_hw_register_pll(NULL, "pll_clk", "osc_clk",
				 &ls1x_pll_clk_ops, 0);
	clk_hw_register_clkdev(hw, "pll_clk", NULL);

	/* clock derived from PLL clk */
	/*                                 _____
	 *         _______________________|     |
	 * OSC ___/                       | MUX |___ CPU CLK
	 *        \___ PLL ___ CPU DIV ___|     |
	 *                                |_____|
	 */
	hw = clk_hw_register_divider(NULL, "cpu_clk_div", "pll_clk",
				   CLK_GET_RATE_NOCACHE, LS1X_CLK_PLL_DIV,
				   DIV_CPU_SHIFT, DIV_CPU_WIDTH,
				   CLK_DIVIDER_ONE_BASED |
				   CLK_DIVIDER_ROUND_CLOSEST, &_lock);
	clk_hw_register_clkdev(hw, "cpu_clk_div", NULL);
	hw = clk_hw_register_mux(NULL, "cpu_clk", cpu_parents,
			       ARRAY_SIZE(cpu_parents),
			       CLK_SET_RATE_NO_REPARENT, LS1X_CLK_PLL_DIV,
			       BYPASS_CPU_SHIFT, BYPASS_CPU_WIDTH, 0, &_lock);
	clk_hw_register_clkdev(hw, "cpu_clk", NULL);

	/*                                 _____
	 *         _______________________|     |
	 * OSC ___/                       | MUX |___ DC  CLK
	 *        \___ PLL ___ DC  DIV ___|     |
	 *                                |_____|
	 */
	hw = clk_hw_register_divider(NULL, "dc_clk_div", "pll_clk",
				   0, LS1X_CLK_PLL_DIV, DIV_DC_SHIFT,
				   DIV_DC_WIDTH, CLK_DIVIDER_ONE_BASED, &_lock);
	clk_hw_register_clkdev(hw, "dc_clk_div", NULL);
	hw = clk_hw_register_mux(NULL, "dc_clk", dc_parents,
			       ARRAY_SIZE(dc_parents),
			       CLK_SET_RATE_NO_REPARENT, LS1X_CLK_PLL_DIV,
			       BYPASS_DC_SHIFT, BYPASS_DC_WIDTH, 0, &_lock);
	clk_hw_register_clkdev(hw, "dc_clk", NULL);

	/*                                 _____
	 *         _______________________|     |
	 * OSC ___/                       | MUX |___ DDR CLK
	 *        \___ PLL ___ DDR DIV ___|     |
	 *                                |_____|
	 */
	hw = clk_hw_register_divider(NULL, "ahb_clk_div", "pll_clk",
				   0, LS1X_CLK_PLL_DIV, DIV_DDR_SHIFT,
				   DIV_DDR_WIDTH, CLK_DIVIDER_ONE_BASED,
				   &_lock);
	clk_hw_register_clkdev(hw, "ahb_clk_div", NULL);
	hw = clk_hw_register_mux(NULL, "ahb_clk", ahb_parents,
			       ARRAY_SIZE(ahb_parents),
			       CLK_SET_RATE_NO_REPARENT, LS1X_CLK_PLL_DIV,
			       BYPASS_DDR_SHIFT, BYPASS_DDR_WIDTH, 0, &_lock);
	clk_hw_register_clkdev(hw, "ahb_clk", NULL);
	clk_hw_register_clkdev(hw, "ls1x-dma", NULL);
	clk_hw_register_clkdev(hw, "stmmaceth", NULL);

	/* clock derived from AHB clk */
	/* APB clk is always half of the AHB clk */
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
