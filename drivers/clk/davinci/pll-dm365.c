// SPDX-License-Identifier: GPL-2.0
/*
 * PLL clock descriptions for TI DM365
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 */

#include <linux/bitops.h>
#include <linux/clkdev.h>
#include <linux/clk/davinci.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "pll.h"

#define OCSEL_OCSRC_ENABLE	0

static const struct davinci_pll_clk_info dm365_pll1_info = {
	.name = "pll1",
	.pllm_mask = GENMASK(9, 0),
	.pllm_min = 1,
	.pllm_max = 1023,
	.flags = PLL_HAS_CLKMODE | PLL_HAS_PREDIV | PLL_HAS_POSTDIV |
		 PLL_POSTDIV_ALWAYS_ENABLED | PLL_PLLM_2X,
};

SYSCLK(1, pll1_sysclk1, pll1_pllen, 5, SYSCLK_ALWAYS_ENABLED);
SYSCLK(2, pll1_sysclk2, pll1_pllen, 5, SYSCLK_ALWAYS_ENABLED);
SYSCLK(3, pll1_sysclk3, pll1_pllen, 5, SYSCLK_ALWAYS_ENABLED);
SYSCLK(4, pll1_sysclk4, pll1_pllen, 5, SYSCLK_ALWAYS_ENABLED);
SYSCLK(5, pll1_sysclk5, pll1_pllen, 5, SYSCLK_ALWAYS_ENABLED);
SYSCLK(6, pll1_sysclk6, pll1_pllen, 5, SYSCLK_ALWAYS_ENABLED);
SYSCLK(7, pll1_sysclk7, pll1_pllen, 5, SYSCLK_ALWAYS_ENABLED);
SYSCLK(8, pll1_sysclk8, pll1_pllen, 5, SYSCLK_ALWAYS_ENABLED);
SYSCLK(9, pll1_sysclk9, pll1_pllen, 5, SYSCLK_ALWAYS_ENABLED);

/*
 * This is a bit of a hack to make OCSEL[OCSRC] on DM365 look like OCSEL[OCSRC]
 * on DA850. On DM365, OCSEL[OCSRC] is just an enable/disable bit instead of a
 * multiplexer. By modeling it as a single parent mux clock, the clock code will
 * still do the right thing in this case.
 */
static const char * const dm365_pll_obsclk_parent_names[] = {
	"oscin",
};

static u32 dm365_pll_obsclk_table[] = {
	OCSEL_OCSRC_ENABLE,
};

static const struct davinci_pll_obsclk_info dm365_pll1_obsclk_info = {
	.name = "pll1_obsclk",
	.parent_names = dm365_pll_obsclk_parent_names,
	.num_parents = ARRAY_SIZE(dm365_pll_obsclk_parent_names),
	.table = dm365_pll_obsclk_table,
	.ocsrc_mask = BIT(4),
};

int dm365_pll1_init(struct device *dev, void __iomem *base, struct regmap *cfgchip)
{
	struct clk *clk;

	davinci_pll_clk_register(dev, &dm365_pll1_info, "ref_clk", base, cfgchip);

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk1, base);
	clk_register_clkdev(clk, "pll1_sysclk1", "dm365-psc");

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk2, base);
	clk_register_clkdev(clk, "pll1_sysclk2", "dm365-psc");

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk3, base);
	clk_register_clkdev(clk, "pll1_sysclk3", "dm365-psc");

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk4, base);
	clk_register_clkdev(clk, "pll1_sysclk4", "dm365-psc");

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk5, base);
	clk_register_clkdev(clk, "pll1_sysclk5", "dm365-psc");

	davinci_pll_sysclk_register(dev, &pll1_sysclk6, base);

	davinci_pll_sysclk_register(dev, &pll1_sysclk7, base);

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk8, base);
	clk_register_clkdev(clk, "pll1_sysclk8", "dm365-psc");

	davinci_pll_sysclk_register(dev, &pll1_sysclk9, base);

	clk = davinci_pll_auxclk_register(dev, "pll1_auxclk", base);
	clk_register_clkdev(clk, "pll1_auxclk", "dm355-psc");

	davinci_pll_sysclkbp_clk_register(dev, "pll1_sysclkbp", base);

	davinci_pll_obsclk_register(dev, &dm365_pll1_obsclk_info, base);

	return 0;
}

static const struct davinci_pll_clk_info dm365_pll2_info = {
	.name = "pll2",
	.pllm_mask = GENMASK(9, 0),
	.pllm_min = 1,
	.pllm_max = 1023,
	.flags = PLL_HAS_PREDIV | PLL_HAS_POSTDIV | PLL_POSTDIV_ALWAYS_ENABLED |
		 PLL_PLLM_2X,
};

SYSCLK(1, pll2_sysclk1, pll2_pllen, 5, SYSCLK_ALWAYS_ENABLED);
SYSCLK(2, pll2_sysclk2, pll2_pllen, 5, SYSCLK_ALWAYS_ENABLED);
SYSCLK(3, pll2_sysclk3, pll2_pllen, 5, SYSCLK_ALWAYS_ENABLED);
SYSCLK(4, pll2_sysclk4, pll2_pllen, 5, SYSCLK_ALWAYS_ENABLED);
SYSCLK(5, pll2_sysclk5, pll2_pllen, 5, SYSCLK_ALWAYS_ENABLED);

static const struct davinci_pll_obsclk_info dm365_pll2_obsclk_info = {
	.name = "pll2_obsclk",
	.parent_names = dm365_pll_obsclk_parent_names,
	.num_parents = ARRAY_SIZE(dm365_pll_obsclk_parent_names),
	.table = dm365_pll_obsclk_table,
	.ocsrc_mask = BIT(4),
};

int dm365_pll2_init(struct device *dev, void __iomem *base, struct regmap *cfgchip)
{
	struct clk *clk;

	davinci_pll_clk_register(dev, &dm365_pll2_info, "oscin", base, cfgchip);

	davinci_pll_sysclk_register(dev, &pll2_sysclk1, base);

	clk = davinci_pll_sysclk_register(dev, &pll2_sysclk2, base);
	clk_register_clkdev(clk, "pll1_sysclk2", "dm365-psc");

	davinci_pll_sysclk_register(dev, &pll2_sysclk3, base);

	clk = davinci_pll_sysclk_register(dev, &pll2_sysclk4, base);
	clk_register_clkdev(clk, "pll1_sysclk4", "dm365-psc");

	davinci_pll_sysclk_register(dev, &pll2_sysclk5, base);

	davinci_pll_auxclk_register(dev, "pll2_auxclk", base);

	davinci_pll_obsclk_register(dev, &dm365_pll2_obsclk_info, base);

	return 0;
}
