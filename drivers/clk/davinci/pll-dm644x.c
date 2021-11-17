// SPDX-License-Identifier: GPL-2.0
/*
 * PLL clock descriptions for TI DM644X
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 */

#include <linux/bitops.h>
#include <linux/clk/davinci.h>
#include <linux/clkdev.h>
#include <linux/init.h>
#include <linux/types.h>

#include "pll.h"

static const struct davinci_pll_clk_info dm644x_pll1_info = {
	.name = "pll1",
	.pllm_mask = GENMASK(4, 0),
	.pllm_min = 1,
	.pllm_max = 32,
	.pllout_min_rate = 400000000,
	.pllout_max_rate = 600000000, /* 810MHz @ 1.3V, -810 only */
	.flags = PLL_HAS_CLKMODE | PLL_HAS_POSTDIV,
};

SYSCLK(1, pll1_sysclk1, pll1_pllen, 4, SYSCLK_FIXED_DIV);
SYSCLK(2, pll1_sysclk2, pll1_pllen, 4, SYSCLK_FIXED_DIV);
SYSCLK(3, pll1_sysclk3, pll1_pllen, 4, SYSCLK_FIXED_DIV);
SYSCLK(5, pll1_sysclk5, pll1_pllen, 4, SYSCLK_FIXED_DIV);

int dm644x_pll1_init(struct device *dev, void __iomem *base, struct regmap *cfgchip)
{
	struct clk *clk;

	davinci_pll_clk_register(dev, &dm644x_pll1_info, "ref_clk", base, cfgchip);

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk1, base);
	clk_register_clkdev(clk, "pll1_sysclk1", "dm644x-psc");

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk2, base);
	clk_register_clkdev(clk, "pll1_sysclk2", "dm644x-psc");

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk3, base);
	clk_register_clkdev(clk, "pll1_sysclk3", "dm644x-psc");

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk5, base);
	clk_register_clkdev(clk, "pll1_sysclk5", "dm644x-psc");

	clk = davinci_pll_auxclk_register(dev, "pll1_auxclk", base);
	clk_register_clkdev(clk, "pll1_auxclk", "dm644x-psc");

	davinci_pll_sysclkbp_clk_register(dev, "pll1_sysclkbp", base);

	return 0;
}

static const struct davinci_pll_clk_info dm644x_pll2_info = {
	.name = "pll2",
	.pllm_mask = GENMASK(4, 0),
	.pllm_min = 1,
	.pllm_max = 32,
	.pllout_min_rate = 400000000,
	.pllout_max_rate = 900000000,
	.flags = PLL_HAS_POSTDIV | PLL_POSTDIV_FIXED_DIV,
};

SYSCLK(1, pll2_sysclk1, pll2_pllen, 4, 0);
SYSCLK(2, pll2_sysclk2, pll2_pllen, 4, 0);

int dm644x_pll2_init(struct device *dev, void __iomem *base, struct regmap *cfgchip)
{
	davinci_pll_clk_register(dev, &dm644x_pll2_info, "oscin", base, cfgchip);

	davinci_pll_sysclk_register(dev, &pll2_sysclk1, base);

	davinci_pll_sysclk_register(dev, &pll2_sysclk2, base);

	davinci_pll_sysclkbp_clk_register(dev, "pll2_sysclkbp", base);

	return 0;
}
