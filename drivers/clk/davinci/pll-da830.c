// SPDX-License-Identifier: GPL-2.0
/*
 * PLL clock descriptions for TI DA830/OMAP-L137/AM17XX
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 */

#include <linux/clkdev.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/types.h>

#include "pll.h"

static const struct davinci_pll_clk_info da830_pll_info = {
	.name = "pll0",
	.pllm_mask = GENMASK(4, 0),
	.pllm_min = 4,
	.pllm_max = 32,
	.pllout_min_rate = 300000000,
	.pllout_max_rate = 600000000,
	.flags = PLL_HAS_CLKMODE | PLL_HAS_PREDIV | PLL_HAS_POSTDIV,
};

/*
 * NB: Technically, the clocks flagged as SYSCLK_FIXED_DIV are "fixed ratio",
 * meaning that we could change the divider as long as we keep the correct
 * ratio between all of the clocks, but we don't support that because there is
 * currently not a need for it.
 */

SYSCLK(2, pll0_sysclk2, pll0_pllen, 5, SYSCLK_FIXED_DIV);
SYSCLK(3, pll0_sysclk3, pll0_pllen, 5, 0);
SYSCLK(4, pll0_sysclk4, pll0_pllen, 5, SYSCLK_FIXED_DIV);
SYSCLK(5, pll0_sysclk5, pll0_pllen, 5, 0);
SYSCLK(6, pll0_sysclk6, pll0_pllen, 5, SYSCLK_FIXED_DIV);
SYSCLK(7, pll0_sysclk7, pll0_pllen, 5, 0);

int da830_pll_init(struct device *dev, void __iomem *base)
{
	struct clk *clk;

	davinci_pll_clk_register(dev, &da830_pll_info, "ref_clk", base);

	clk = davinci_pll_sysclk_register(dev, &pll0_sysclk2, base);
	clk_register_clkdev(clk, "pll0_sysclk2", "da830-psc0");
	clk_register_clkdev(clk, "pll0_sysclk2", "da830-psc1");

	clk = davinci_pll_sysclk_register(dev, &pll0_sysclk3, base);
	clk_register_clkdev(clk, "pll0_sysclk3", "da830-psc0");

	clk = davinci_pll_sysclk_register(dev, &pll0_sysclk4, base);
	clk_register_clkdev(clk, "pll0_sysclk4", "da830-psc0");
	clk_register_clkdev(clk, "pll0_sysclk4", "da830-psc1");

	clk = davinci_pll_sysclk_register(dev, &pll0_sysclk5, base);
	clk_register_clkdev(clk, "pll0_sysclk5", "da830-psc1");

	clk = davinci_pll_sysclk_register(dev, &pll0_sysclk6, base);
	clk_register_clkdev(clk, "pll0_sysclk6", "da830-psc0");

	clk = davinci_pll_sysclk_register(dev, &pll0_sysclk7, base);

	clk = davinci_pll_auxclk_register(dev, "pll0_auxclk", base);
	clk_register_clkdev(clk, NULL, "i2c_davinci.1");
	clk_register_clkdev(clk, "timer0", NULL);
	clk_register_clkdev(clk, NULL, "davinci-wdt");

	return 0;
}
