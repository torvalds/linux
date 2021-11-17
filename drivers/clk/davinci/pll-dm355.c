// SPDX-License-Identifier: GPL-2.0
/*
 * PLL clock descriptions for TI DM355
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 */

#include <linux/bitops.h>
#include <linux/clk/davinci.h>
#include <linux/clkdev.h>
#include <linux/init.h>
#include <linux/types.h>

#include "pll.h"

static const struct davinci_pll_clk_info dm355_pll1_info = {
	.name = "pll1",
	.pllm_mask = GENMASK(7, 0),
	.pllm_min = 92,
	.pllm_max = 184,
	.flags = PLL_HAS_CLKMODE | PLL_HAS_PREDIV | PLL_PREDIV_ALWAYS_ENABLED |
		 PLL_PREDIV_FIXED8 | PLL_HAS_POSTDIV |
		 PLL_POSTDIV_ALWAYS_ENABLED | PLL_POSTDIV_FIXED_DIV,
};

SYSCLK(1, pll1_sysclk1, pll1_pllen, 5, SYSCLK_FIXED_DIV | SYSCLK_ALWAYS_ENABLED);
SYSCLK(2, pll1_sysclk2, pll1_pllen, 5, SYSCLK_FIXED_DIV | SYSCLK_ALWAYS_ENABLED);
SYSCLK(3, pll1_sysclk3, pll1_pllen, 5, SYSCLK_ALWAYS_ENABLED);
SYSCLK(4, pll1_sysclk4, pll1_pllen, 5, SYSCLK_ALWAYS_ENABLED);

int dm355_pll1_init(struct device *dev, void __iomem *base, struct regmap *cfgchip)
{
	struct clk *clk;

	davinci_pll_clk_register(dev, &dm355_pll1_info, "ref_clk", base, cfgchip);

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk1, base);
	clk_register_clkdev(clk, "pll1_sysclk1", "dm355-psc");

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk2, base);
	clk_register_clkdev(clk, "pll1_sysclk2", "dm355-psc");

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk3, base);
	clk_register_clkdev(clk, "pll1_sysclk3", "dm355-psc");

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk4, base);
	clk_register_clkdev(clk, "pll1_sysclk4", "dm355-psc");

	clk = davinci_pll_auxclk_register(dev, "pll1_auxclk", base);
	clk_register_clkdev(clk, "pll1_auxclk", "dm355-psc");

	davinci_pll_sysclkbp_clk_register(dev, "pll1_sysclkbp", base);

	return 0;
}

static const struct davinci_pll_clk_info dm355_pll2_info = {
	.name = "pll2",
	.pllm_mask = GENMASK(7, 0),
	.pllm_min = 92,
	.pllm_max = 184,
	.flags = PLL_HAS_PREDIV | PLL_PREDIV_ALWAYS_ENABLED | PLL_HAS_POSTDIV |
		 PLL_POSTDIV_ALWAYS_ENABLED | PLL_POSTDIV_FIXED_DIV,
};

SYSCLK(1, pll2_sysclk1, pll2_pllen, 5, SYSCLK_FIXED_DIV | SYSCLK_ALWAYS_ENABLED);

int dm355_pll2_init(struct device *dev, void __iomem *base, struct regmap *cfgchip)
{
	davinci_pll_clk_register(dev, &dm355_pll2_info, "oscin", base, cfgchip);

	davinci_pll_sysclk_register(dev, &pll2_sysclk1, base);

	davinci_pll_sysclkbp_clk_register(dev, "pll2_sysclkbp", base);

	return 0;
}
