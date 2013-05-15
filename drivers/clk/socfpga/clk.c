/*
 *  Copyright 2011-2012 Calxeda, Inc.
 *  Copyright (C) 2012-2013 Altera Corporation <www.altera.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Based from clk-highbank.c
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>

/* Clock Manager offsets */
#define CLKMGR_CTRL    0x0
#define CLKMGR_BYPASS 0x4

/* Clock bypass bits */
#define MAINPLL_BYPASS (1<<0)
#define SDRAMPLL_BYPASS (1<<1)
#define SDRAMPLL_SRC_BYPASS (1<<2)
#define PERPLL_BYPASS (1<<3)
#define PERPLL_SRC_BYPASS (1<<4)

#define SOCFPGA_PLL_BG_PWRDWN		0
#define SOCFPGA_PLL_EXT_ENA		1
#define SOCFPGA_PLL_PWR_DOWN		2
#define SOCFPGA_PLL_DIVF_MASK		0x0000FFF8
#define SOCFPGA_PLL_DIVF_SHIFT	3
#define SOCFPGA_PLL_DIVQ_MASK		0x003F0000
#define SOCFPGA_PLL_DIVQ_SHIFT	16

extern void __iomem *clk_mgr_base_addr;

struct socfpga_clk {
	struct clk_gate hw;
	char *parent_name;
	char *clk_name;
	u32 fixed_div;
};
#define to_socfpga_clk(p) container_of(p, struct socfpga_clk, hw.hw)

static unsigned long clk_pll_recalc_rate(struct clk_hw *hwclk,
					 unsigned long parent_rate)
{
	struct socfpga_clk *socfpgaclk = to_socfpga_clk(hwclk);
	unsigned long divf, divq, vco_freq, reg;
	unsigned long bypass;

	reg = readl(socfpgaclk->hw.reg);
	bypass = readl(clk_mgr_base_addr + CLKMGR_BYPASS);
	if (bypass & MAINPLL_BYPASS)
		return parent_rate;

	divf = (reg & SOCFPGA_PLL_DIVF_MASK) >> SOCFPGA_PLL_DIVF_SHIFT;
	divq = (reg & SOCFPGA_PLL_DIVQ_MASK) >> SOCFPGA_PLL_DIVQ_SHIFT;
	vco_freq = parent_rate * (divf + 1);
	return vco_freq / (1 + divq);
}


static struct clk_ops clk_pll_ops = {
	.recalc_rate = clk_pll_recalc_rate,
};

static unsigned long clk_periclk_recalc_rate(struct clk_hw *hwclk,
					     unsigned long parent_rate)
{
	struct socfpga_clk *socfpgaclk = to_socfpga_clk(hwclk);
	u32 div;

	if (socfpgaclk->fixed_div)
		div = socfpgaclk->fixed_div;
	else
		div = ((readl(socfpgaclk->hw.reg) & 0x1ff) + 1);

	return parent_rate / div;
}

static const struct clk_ops periclk_ops = {
	.recalc_rate = clk_periclk_recalc_rate,
};

static __init struct clk *socfpga_clk_init(struct device_node *node,
	const struct clk_ops *ops)
{
	u32 reg;
	struct clk *clk;
	struct socfpga_clk *socfpga_clk;
	const char *clk_name = node->name;
	const char *parent_name;
	struct clk_init_data init;
	int rc;
	u32 fixed_div;

	rc = of_property_read_u32(node, "reg", &reg);
	if (WARN_ON(rc))
		return NULL;

	socfpga_clk = kzalloc(sizeof(*socfpga_clk), GFP_KERNEL);
	if (WARN_ON(!socfpga_clk))
		return NULL;

	socfpga_clk->hw.reg = clk_mgr_base_addr + reg;

	rc = of_property_read_u32(node, "fixed-divider", &fixed_div);
	if (rc)
		socfpga_clk->fixed_div = 0;
	else
		socfpga_clk->fixed_div = fixed_div;

	of_property_read_string(node, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = ops;
	init.flags = 0;
	parent_name = of_clk_get_parent_name(node, 0);
	init.parent_names = &parent_name;
	init.num_parents = 1;

	socfpga_clk->hw.hw.init = &init;

	if (strcmp(clk_name, "main_pll") || strcmp(clk_name, "periph_pll") ||
			strcmp(clk_name, "sdram_pll")) {
		socfpga_clk->hw.bit_idx = SOCFPGA_PLL_EXT_ENA;
		clk_pll_ops.enable = clk_gate_ops.enable;
		clk_pll_ops.disable = clk_gate_ops.disable;
	}

	clk = clk_register(NULL, &socfpga_clk->hw.hw);
	if (WARN_ON(IS_ERR(clk))) {
		kfree(socfpga_clk);
		return NULL;
	}
	rc = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	return clk;
}

static void __init socfpga_pll_init(struct device_node *node)
{
	socfpga_clk_init(node, &clk_pll_ops);
}
CLK_OF_DECLARE(socfpga_pll, "altr,socfpga-pll-clock", socfpga_pll_init);

static void __init socfpga_periph_init(struct device_node *node)
{
	socfpga_clk_init(node, &periclk_ops);
}
CLK_OF_DECLARE(socfpga_periph, "altr,socfpga-perip-clk", socfpga_periph_init);

void __init socfpga_init_clocks(void)
{
	struct clk *clk;
	int ret;

	clk = clk_register_fixed_factor(NULL, "smp_twd", "mpuclk", 0, 1, 4);
	ret = clk_register_clkdev(clk, NULL, "smp_twd");
	if (ret)
		pr_err("smp_twd alias not registered\n");
}
