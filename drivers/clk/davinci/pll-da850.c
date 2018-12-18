// SPDX-License-Identifier: GPL-2.0
/*
 * PLL clock descriptions for TI DA850/OMAP-L138/AM18XX
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 */

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/clk/davinci.h>
#include <linux/clkdev.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/da8xx-cfgchip.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/types.h>

#include "pll.h"

#define OCSEL_OCSRC_OSCIN		0x14
#define OCSEL_OCSRC_PLL0_SYSCLK(n)	(0x16 + (n))
#define OCSEL_OCSRC_PLL1_OBSCLK		0x1e
#define OCSEL_OCSRC_PLL1_SYSCLK(n)	(0x16 + (n))

static const struct davinci_pll_clk_info da850_pll0_info = {
	.name = "pll0",
	.unlock_reg = CFGCHIP(0),
	.unlock_mask = CFGCHIP0_PLL_MASTER_LOCK,
	.pllm_mask = GENMASK(4, 0),
	.pllm_min = 4,
	.pllm_max = 32,
	.pllout_min_rate = 300000000,
	.pllout_max_rate = 600000000,
	.flags = PLL_HAS_CLKMODE | PLL_HAS_PREDIV | PLL_HAS_POSTDIV |
		 PLL_HAS_EXTCLKSRC,
};

/*
 * NB: Technically, the clocks flagged as SYSCLK_FIXED_DIV are "fixed ratio",
 * meaning that we could change the divider as long as we keep the correct
 * ratio between all of the clocks, but we don't support that because there is
 * currently not a need for it.
 */

SYSCLK(1, pll0_sysclk1, pll0_pllen, 5, SYSCLK_FIXED_DIV);
SYSCLK(2, pll0_sysclk2, pll0_pllen, 5, SYSCLK_FIXED_DIV);
SYSCLK(3, pll0_sysclk3, pll0_pllen, 5, 0);
SYSCLK(4, pll0_sysclk4, pll0_pllen, 5, SYSCLK_FIXED_DIV);
SYSCLK(5, pll0_sysclk5, pll0_pllen, 5, 0);
SYSCLK(6, pll0_sysclk6, pll0_pllen, 5, SYSCLK_ARM_RATE | SYSCLK_FIXED_DIV);
SYSCLK(7, pll0_sysclk7, pll0_pllen, 5, 0);

static const char * const da850_pll0_obsclk_parent_names[] = {
	"oscin",
	"pll0_sysclk1",
	"pll0_sysclk2",
	"pll0_sysclk3",
	"pll0_sysclk4",
	"pll0_sysclk5",
	"pll0_sysclk6",
	"pll0_sysclk7",
	"pll1_obsclk",
};

static u32 da850_pll0_obsclk_table[] = {
	OCSEL_OCSRC_OSCIN,
	OCSEL_OCSRC_PLL0_SYSCLK(1),
	OCSEL_OCSRC_PLL0_SYSCLK(2),
	OCSEL_OCSRC_PLL0_SYSCLK(3),
	OCSEL_OCSRC_PLL0_SYSCLK(4),
	OCSEL_OCSRC_PLL0_SYSCLK(5),
	OCSEL_OCSRC_PLL0_SYSCLK(6),
	OCSEL_OCSRC_PLL0_SYSCLK(7),
	OCSEL_OCSRC_PLL1_OBSCLK,
};

static const struct davinci_pll_obsclk_info da850_pll0_obsclk_info = {
	.name = "pll0_obsclk",
	.parent_names = da850_pll0_obsclk_parent_names,
	.num_parents = ARRAY_SIZE(da850_pll0_obsclk_parent_names),
	.table = da850_pll0_obsclk_table,
	.ocsrc_mask = GENMASK(4, 0),
};

int da850_pll0_init(struct device *dev, void __iomem *base, struct regmap *cfgchip)
{
	struct clk *clk;

	davinci_pll_clk_register(dev, &da850_pll0_info, "ref_clk", base, cfgchip);

	clk = davinci_pll_sysclk_register(dev, &pll0_sysclk1, base);
	clk_register_clkdev(clk, "pll0_sysclk1", "da850-psc0");

	clk = davinci_pll_sysclk_register(dev, &pll0_sysclk2, base);
	clk_register_clkdev(clk, "pll0_sysclk2", "da850-psc0");
	clk_register_clkdev(clk, "pll0_sysclk2", "da850-psc1");
	clk_register_clkdev(clk, "pll0_sysclk2", "da850-async3-clksrc");

	clk = davinci_pll_sysclk_register(dev, &pll0_sysclk3, base);
	clk_register_clkdev(clk, "pll0_sysclk3", "da850-async1-clksrc");

	clk = davinci_pll_sysclk_register(dev, &pll0_sysclk4, base);
	clk_register_clkdev(clk, "pll0_sysclk4", "da850-psc0");
	clk_register_clkdev(clk, "pll0_sysclk4", "da850-psc1");

	davinci_pll_sysclk_register(dev, &pll0_sysclk5, base);

	clk = davinci_pll_sysclk_register(dev, &pll0_sysclk6, base);
	clk_register_clkdev(clk, "pll0_sysclk6", "da850-psc0");

	davinci_pll_sysclk_register(dev, &pll0_sysclk7, base);

	davinci_pll_auxclk_register(dev, "pll0_auxclk", base);

	clk = clk_register_fixed_factor(dev, "async2", "pll0_auxclk",
					CLK_IS_CRITICAL, 1, 1);

	clk_register_clkdev(clk, NULL, "i2c_davinci.1");
	clk_register_clkdev(clk, "timer0", NULL);
	clk_register_clkdev(clk, NULL, "davinci-wdt");

	davinci_pll_obsclk_register(dev, &da850_pll0_obsclk_info, base);

	return 0;
}

static const struct davinci_pll_sysclk_info *da850_pll0_sysclk_info[] = {
	&pll0_sysclk1,
	&pll0_sysclk2,
	&pll0_sysclk3,
	&pll0_sysclk4,
	&pll0_sysclk5,
	&pll0_sysclk6,
	&pll0_sysclk7,
	NULL
};

void of_da850_pll0_init(struct device_node *node)
{
	void __iomem *base;
	struct regmap *cfgchip;

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%s: ioremap failed\n", __func__);
		return;
	}

	cfgchip = syscon_regmap_lookup_by_compatible("ti,da830-cfgchip");

	of_davinci_pll_init(NULL, node, &da850_pll0_info,
			    &da850_pll0_obsclk_info,
			    da850_pll0_sysclk_info, 7, base, cfgchip);
}

static const struct davinci_pll_clk_info da850_pll1_info = {
	.name = "pll1",
	.unlock_reg = CFGCHIP(3),
	.unlock_mask = CFGCHIP3_PLL1_MASTER_LOCK,
	.pllm_mask = GENMASK(4, 0),
	.pllm_min = 4,
	.pllm_max = 32,
	.pllout_min_rate = 300000000,
	.pllout_max_rate = 600000000,
	.flags = PLL_HAS_POSTDIV,
};

SYSCLK(1, pll1_sysclk1, pll1_pllen, 5, SYSCLK_ALWAYS_ENABLED);
SYSCLK(2, pll1_sysclk2, pll1_pllen, 5, 0);
SYSCLK(3, pll1_sysclk3, pll1_pllen, 5, 0);

static const char * const da850_pll1_obsclk_parent_names[] = {
	"oscin",
	"pll1_sysclk1",
	"pll1_sysclk2",
	"pll1_sysclk3",
};

static u32 da850_pll1_obsclk_table[] = {
	OCSEL_OCSRC_OSCIN,
	OCSEL_OCSRC_PLL1_SYSCLK(1),
	OCSEL_OCSRC_PLL1_SYSCLK(2),
	OCSEL_OCSRC_PLL1_SYSCLK(3),
};

static const struct davinci_pll_obsclk_info da850_pll1_obsclk_info = {
	.name = "pll1_obsclk",
	.parent_names = da850_pll1_obsclk_parent_names,
	.num_parents = ARRAY_SIZE(da850_pll1_obsclk_parent_names),
	.table = da850_pll1_obsclk_table,
	.ocsrc_mask = GENMASK(4, 0),
};

int da850_pll1_init(struct device *dev, void __iomem *base, struct regmap *cfgchip)
{
	struct clk *clk;

	davinci_pll_clk_register(dev, &da850_pll1_info, "oscin", base, cfgchip);

	davinci_pll_sysclk_register(dev, &pll1_sysclk1, base);

	clk = davinci_pll_sysclk_register(dev, &pll1_sysclk2, base);
	clk_register_clkdev(clk, "pll1_sysclk2", "da850-async3-clksrc");

	davinci_pll_sysclk_register(dev, &pll1_sysclk3, base);

	davinci_pll_obsclk_register(dev, &da850_pll1_obsclk_info, base);

	return 0;
}

static const struct davinci_pll_sysclk_info *da850_pll1_sysclk_info[] = {
	&pll1_sysclk1,
	&pll1_sysclk2,
	&pll1_sysclk3,
	NULL
};

int of_da850_pll1_init(struct device *dev, void __iomem *base, struct regmap *cfgchip)
{
	return of_davinci_pll_init(dev, dev->of_node, &da850_pll1_info,
				   &da850_pll1_obsclk_info,
				   da850_pll1_sysclk_info, 3, base, cfgchip);
}
