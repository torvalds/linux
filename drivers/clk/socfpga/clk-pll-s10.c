// SPDX-License-Identifier:	GPL-2.0
/*
 * Copyright (C) 2017, Intel Corporation
 */
#include <linux/slab.h>
#include <linux/clk-provider.h>

#include "stratix10-clk.h"
#include "clk.h"

/* Clock Manager offsets */
#define CLK_MGR_PLL_CLK_SRC_SHIFT	16
#define CLK_MGR_PLL_CLK_SRC_MASK	0x3

/* PLL Clock enable bits */
#define SOCFPGA_PLL_POWER		0
#define SOCFPGA_PLL_RESET_MASK		0x2
#define SOCFPGA_PLL_REFDIV_MASK		0x00003F00
#define SOCFPGA_PLL_REFDIV_SHIFT	8
#define SOCFPGA_PLL_MDIV_MASK		0xFF000000
#define SOCFPGA_PLL_MDIV_SHIFT		24
#define SWCTRLBTCLKSEL_MASK		0x200
#define SWCTRLBTCLKSEL_SHIFT		9

#define SOCFPGA_BOOT_CLK		"boot_clk"

#define to_socfpga_clk(p) container_of(p, struct socfpga_pll, hw.hw)

static unsigned long clk_pll_recalc_rate(struct clk_hw *hwclk,
					 unsigned long parent_rate)
{
	struct socfpga_pll *socfpgaclk = to_socfpga_clk(hwclk);
	unsigned long mdiv;
	unsigned long refdiv;
	unsigned long reg;
	unsigned long long vco_freq;

	/* read VCO1 reg for numerator and denominator */
	reg = readl(socfpgaclk->hw.reg);
	refdiv = (reg & SOCFPGA_PLL_REFDIV_MASK) >> SOCFPGA_PLL_REFDIV_SHIFT;

	vco_freq = parent_rate;
	do_div(vco_freq, refdiv);

	/* Read mdiv and fdiv from the fdbck register */
	reg = readl(socfpgaclk->hw.reg + 0x4);
	mdiv = (reg & SOCFPGA_PLL_MDIV_MASK) >> SOCFPGA_PLL_MDIV_SHIFT;
	vco_freq = (unsigned long long)vco_freq * (mdiv + 6);

	return (unsigned long)vco_freq;
}

static unsigned long clk_boot_clk_recalc_rate(struct clk_hw *hwclk,
					 unsigned long parent_rate)
{
	struct socfpga_pll *socfpgaclk = to_socfpga_clk(hwclk);
	u32 div = 1;

	div = ((readl(socfpgaclk->hw.reg) &
		SWCTRLBTCLKSEL_MASK) >>
		SWCTRLBTCLKSEL_SHIFT);
	div += 1;
	return parent_rate /= div;
}


static u8 clk_pll_get_parent(struct clk_hw *hwclk)
{
	struct socfpga_pll *socfpgaclk = to_socfpga_clk(hwclk);
	u32 pll_src;

	pll_src = readl(socfpgaclk->hw.reg);
	return (pll_src >> CLK_MGR_PLL_CLK_SRC_SHIFT) &
		CLK_MGR_PLL_CLK_SRC_MASK;
}

static u8 clk_boot_get_parent(struct clk_hw *hwclk)
{
	struct socfpga_pll *socfpgaclk = to_socfpga_clk(hwclk);
	u32 pll_src;

	pll_src = readl(socfpgaclk->hw.reg);
	return (pll_src >> SWCTRLBTCLKSEL_SHIFT) &
		SWCTRLBTCLKSEL_MASK;
}

static int clk_pll_prepare(struct clk_hw *hwclk)
{
	struct socfpga_pll *socfpgaclk = to_socfpga_clk(hwclk);
	u32 reg;

	/* Bring PLL out of reset */
	reg = readl(socfpgaclk->hw.reg);
	reg |= SOCFPGA_PLL_RESET_MASK;
	writel(reg, socfpgaclk->hw.reg);

	return 0;
}

static struct clk_ops clk_pll_ops = {
	.recalc_rate = clk_pll_recalc_rate,
	.get_parent = clk_pll_get_parent,
	.prepare = clk_pll_prepare,
};

static struct clk_ops clk_boot_ops = {
	.recalc_rate = clk_boot_clk_recalc_rate,
	.get_parent = clk_boot_get_parent,
	.prepare = clk_pll_prepare,
};

struct clk *s10_register_pll(const char *name, const char * const *parent_names,
				    u8 num_parents, unsigned long flags,
				    void __iomem *reg, unsigned long offset)
{
	struct clk *clk;
	struct socfpga_pll *pll_clk;
	struct clk_init_data init;

	pll_clk = kzalloc(sizeof(*pll_clk), GFP_KERNEL);
	if (WARN_ON(!pll_clk))
		return NULL;

	pll_clk->hw.reg = reg + offset;

	if (streq(name, SOCFPGA_BOOT_CLK))
		init.ops = &clk_boot_ops;
	else
		init.ops = &clk_pll_ops;

	init.name = name;
	init.flags = flags;

	init.num_parents = num_parents;
	init.parent_names = parent_names;
	pll_clk->hw.hw.init = &init;

	pll_clk->hw.bit_idx = SOCFPGA_PLL_POWER;
	clk_pll_ops.enable = clk_gate_ops.enable;
	clk_pll_ops.disable = clk_gate_ops.disable;

	clk = clk_register(NULL, &pll_clk->hw.hw);
	if (WARN_ON(IS_ERR(clk))) {
		kfree(pll_clk);
		return NULL;
	}
	return clk;
}
