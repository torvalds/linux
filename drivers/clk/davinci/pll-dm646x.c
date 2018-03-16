// SPDX-License-Identifier: GPL-2.0
/*
 * PLL clock descriptions for TI DM646X
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/init.h>
#include <linux/types.h>

#include "pll.h"

static const struct davinci_pll_clk_info dm646x_pll1_info = {
	.name = "pll1",
	.pllm_mask = GENMASK(4, 0),
	.pllm_min = 14,
	.pllm_max = 32,
	.flags = PLL_HAS_CLKMODE,
};

SYSCLK(1, pll1_sysclk1, pll1_pllen, 4, SYSCLK_FIXED_DIV);
SYSCLK(2, pll1_sysclk2, pll1_pllen, 4, SYSCLK_FIXED_DIV);
SYSCLK(3, pll1_sysclk3, pll1_pllen, 4, SYSCLK_FIXED_DIV);
SYSCLK(4, pll1_sysclk4, pll1_pllen, 4, 0);
SYSCLK(5, pll1_sysclk5, pll1_pllen, 4, 0);
SYSCLK(6, pll1_sysclk6, pll1_pllen, 4, 0);
SYSCLK(8, pll1_sysclk8, pll1_pllen, 4, 0);
SYSCLK(9, pll1_sysclk9, pll1_pllen, 4, 0);

int dm646x_pll1_init(struct device *dev, void __iomem *base)
{
	struct clk *clk;

	davinci_pll_clk_register(dev, &dm646x_pll1_info, "ref_clk", base);

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk1, base);
	clk_register_clkdev(clk, "pll1_sysclk1", "dm646x-psc");

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk2, base);
	clk_register_clkdev(clk, "pll1_sysclk2", "dm646x-psc");

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk3, base);
	clk_register_clkdev(clk, "pll1_sysclk3", "dm646x-psc");
	clk_register_clkdev(clk, NULL, "davinci-wdt");

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk4, base);
	clk_register_clkdev(clk, "pll1_sysclk4", "dm646x-psc");

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk5, base);
	clk_register_clkdev(clk, "pll1_sysclk5", "dm646x-psc");

	davinci_pll_sysclk_register(dev, &pll1_sysclk6, base);

	davinci_pll_sysclk_register(dev, &pll1_sysclk8, base);

	davinci_pll_sysclk_register(dev, &pll1_sysclk9, base);

	davinci_pll_sysclkbp_clk_register(dev, "pll1_sysclkbp", base);

	davinci_pll_auxclk_register(dev, "pll1_auxclk", base);

	return 0;
}

static const struct davinci_pll_clk_info dm646x_pll2_info = {
	.name = "pll2",
	.pllm_mask = GENMASK(4, 0),
	.pllm_min = 14,
	.pllm_max = 32,
	.flags = 0,
};

SYSCLK(1, pll2_sysclk1, pll2_pllen, 4, 0);

int dm646x_pll2_init(struct device *dev, void __iomem *base)
{
	davinci_pll_clk_register(dev, &dm646x_pll2_info, "oscin", base);

	davinci_pll_sysclk_register(dev, &pll2_sysclk1, base);

	return 0;
}
