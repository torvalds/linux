// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright 2011-2012 Calxeda, Inc.
 *  Copyright (C) 2012-2013 Altera Corporation <www.altera.com>
 *
 * Based from clk-highbank.c
 */
#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "clk.h"

/* Clock bypass bits */
#define MAINPLL_BYPASS		(1<<0)
#define SDRAMPLL_BYPASS		(1<<1)
#define SDRAMPLL_SRC_BYPASS	(1<<2)
#define PERPLL_BYPASS		(1<<3)
#define PERPLL_SRC_BYPASS	(1<<4)

#define SOCFPGA_PLL_BG_PWRDWN		0
#define SOCFPGA_PLL_EXT_ENA		1
#define SOCFPGA_PLL_PWR_DOWN		2
#define SOCFPGA_PLL_DIVF_MASK		0x0000FFF8
#define SOCFPGA_PLL_DIVF_SHIFT		3
#define SOCFPGA_PLL_DIVQ_MASK		0x003F0000
#define SOCFPGA_PLL_DIVQ_SHIFT		16

#define CLK_MGR_PLL_CLK_SRC_SHIFT	22
#define CLK_MGR_PLL_CLK_SRC_MASK	0x3

#define to_socfpga_clk(p) container_of(p, struct socfpga_pll, hw.hw)

void __iomem *clk_mgr_base_addr;

static unsigned long clk_pll_recalc_rate(struct clk_hw *hwclk,
					 unsigned long parent_rate)
{
	struct socfpga_pll *socfpgaclk = to_socfpga_clk(hwclk);
	unsigned long divf, divq, reg;
	unsigned long long vco_freq;
	unsigned long bypass;

	reg = readl(socfpgaclk->hw.reg);
	bypass = readl(clk_mgr_base_addr + CLKMGR_BYPASS);
	if (bypass & MAINPLL_BYPASS)
		return parent_rate;

	divf = (reg & SOCFPGA_PLL_DIVF_MASK) >> SOCFPGA_PLL_DIVF_SHIFT;
	divq = (reg & SOCFPGA_PLL_DIVQ_MASK) >> SOCFPGA_PLL_DIVQ_SHIFT;
	vco_freq = (unsigned long long)parent_rate * (divf + 1);
	do_div(vco_freq, (1 + divq));
	return (unsigned long)vco_freq;
}

static u8 clk_pll_get_parent(struct clk_hw *hwclk)
{
	u32 pll_src;
	struct socfpga_pll *socfpgaclk = to_socfpga_clk(hwclk);

	pll_src = readl(socfpgaclk->hw.reg);
	return (pll_src >> CLK_MGR_PLL_CLK_SRC_SHIFT) &
			CLK_MGR_PLL_CLK_SRC_MASK;
}

static const struct clk_ops clk_pll_ops = {
	.recalc_rate = clk_pll_recalc_rate,
	.get_parent = clk_pll_get_parent,
};

static __init struct clk *__socfpga_pll_init(struct device_node *node,
	const struct clk_ops *ops)
{
	u32 reg;
	struct clk *clk;
	struct socfpga_pll *pll_clk;
	const char *clk_name = node->name;
	const char *parent_name[SOCFPGA_MAX_PARENTS];
	struct clk_init_data init;
	struct device_node *clkmgr_np;
	int rc;

	of_property_read_u32(node, "reg", &reg);

	pll_clk = kzalloc(sizeof(*pll_clk), GFP_KERNEL);
	if (WARN_ON(!pll_clk))
		return NULL;

	clkmgr_np = of_find_compatible_node(NULL, NULL, "altr,clk-mgr");
	clk_mgr_base_addr = of_iomap(clkmgr_np, 0);
	of_node_put(clkmgr_np);
	BUG_ON(!clk_mgr_base_addr);
	pll_clk->hw.reg = clk_mgr_base_addr + reg;

	of_property_read_string(node, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = ops;
	init.flags = 0;

	init.num_parents = of_clk_parent_fill(node, parent_name, SOCFPGA_MAX_PARENTS);
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

void __init socfpga_pll_init(struct device_node *node)
{
	__socfpga_pll_init(node, &clk_pll_ops);
}
