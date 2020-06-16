// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Altera Corporation. All rights reserved
 */
#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "clk.h"

/* Clock Manager offsets */
#define CLK_MGR_PLL_CLK_SRC_SHIFT	8
#define CLK_MGR_PLL_CLK_SRC_MASK	0x3

/* Clock bypass bits */
#define SOCFPGA_PLL_BG_PWRDWN		0
#define SOCFPGA_PLL_PWR_DOWN		1
#define SOCFPGA_PLL_EXT_ENA		2
#define SOCFPGA_PLL_DIVF_MASK		0x00001FFF
#define SOCFPGA_PLL_DIVF_SHIFT	0
#define SOCFPGA_PLL_DIVQ_MASK		0x003F0000
#define SOCFPGA_PLL_DIVQ_SHIFT	16
#define SOCFGPA_MAX_PARENTS	5

#define SOCFPGA_MAIN_PLL_CLK		"main_pll"
#define SOCFPGA_PERIP_PLL_CLK		"periph_pll"

#define to_socfpga_clk(p) container_of(p, struct socfpga_pll, hw.hw)

void __iomem *clk_mgr_a10_base_addr;

static unsigned long clk_pll_recalc_rate(struct clk_hw *hwclk,
					 unsigned long parent_rate)
{
	struct socfpga_pll *socfpgaclk = to_socfpga_clk(hwclk);
	unsigned long divf, divq, reg;
	unsigned long long vco_freq;

	/* read VCO1 reg for numerator and denominator */
	reg = readl(socfpgaclk->hw.reg + 0x4);
	divf = (reg & SOCFPGA_PLL_DIVF_MASK) >> SOCFPGA_PLL_DIVF_SHIFT;
	divq = (reg & SOCFPGA_PLL_DIVQ_MASK) >> SOCFPGA_PLL_DIVQ_SHIFT;
	vco_freq = (unsigned long long)parent_rate * (divf + 1);
	do_div(vco_freq, (1 + divq));
	return (unsigned long)vco_freq;
}

static u8 clk_pll_get_parent(struct clk_hw *hwclk)
{
	struct socfpga_pll *socfpgaclk = to_socfpga_clk(hwclk);
	u32 pll_src;

	pll_src = readl(socfpgaclk->hw.reg);

	return (pll_src >> CLK_MGR_PLL_CLK_SRC_SHIFT) &
		CLK_MGR_PLL_CLK_SRC_MASK;
}

static const struct clk_ops clk_pll_ops = {
	.recalc_rate = clk_pll_recalc_rate,
	.get_parent = clk_pll_get_parent,
};

static struct clk * __init __socfpga_pll_init(struct device_node *node,
	const struct clk_ops *ops)
{
	u32 reg;
	struct clk *clk;
	struct socfpga_pll *pll_clk;
	const char *clk_name = node->name;
	const char *parent_name[SOCFGPA_MAX_PARENTS];
	struct clk_init_data init;
	struct device_node *clkmgr_np;
	int rc;
	int i = 0;

	of_property_read_u32(node, "reg", &reg);

	pll_clk = kzalloc(sizeof(*pll_clk), GFP_KERNEL);
	if (WARN_ON(!pll_clk))
		return NULL;

	clkmgr_np = of_find_compatible_node(NULL, NULL, "altr,clk-mgr");
	clk_mgr_a10_base_addr = of_iomap(clkmgr_np, 0);
	of_node_put(clkmgr_np);
	BUG_ON(!clk_mgr_a10_base_addr);
	pll_clk->hw.reg = clk_mgr_a10_base_addr + reg;

	of_property_read_string(node, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = ops;
	init.flags = 0;

	while (i < SOCFGPA_MAX_PARENTS && (parent_name[i] =
			of_clk_get_parent_name(node, i)) != NULL)
		i++;
	init.num_parents = i;
	init.parent_names = parent_name;
	pll_clk->hw.hw.init = &init;

	pll_clk->hw.bit_idx = SOCFPGA_PLL_EXT_ENA;

	clk = clk_register(NULL, &pll_clk->hw.hw);
	if (WARN_ON(IS_ERR(clk))) {
		kfree(pll_clk);
		return NULL;
	}
	rc = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	return clk;
}

void __init socfpga_a10_pll_init(struct device_node *node)
{
	__socfpga_pll_init(node, &clk_pll_ops);
}
