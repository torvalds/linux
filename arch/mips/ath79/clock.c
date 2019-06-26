/*
 *  Atheros AR71XX/AR724X/AR913X common routines
 *
 *  Copyright (C) 2010-2011 Jaiganesh Narayanan <jnarayanan@atheros.com>
 *  Copyright (C) 2011 Gabor Juhos <juhosg@openwrt.org>
 *
 *  Parts of this file are based on Atheros' 2.6.15/2.6.31 BSP
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <dt-bindings/clock/ath79-clk.h>

#include <asm/div64.h>

#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>
#include "common.h"

#define AR71XX_BASE_FREQ	40000000
#define AR724X_BASE_FREQ	40000000

static struct clk *clks[ATH79_CLK_END];
static struct clk_onecell_data clk_data = {
	.clks = clks,
	.clk_num = ARRAY_SIZE(clks),
};

static const char * const clk_names[ATH79_CLK_END] = {
	[ATH79_CLK_CPU] = "cpu",
	[ATH79_CLK_DDR] = "ddr",
	[ATH79_CLK_AHB] = "ahb",
	[ATH79_CLK_REF] = "ref",
	[ATH79_CLK_MDIO] = "mdio",
};

static const char * __init ath79_clk_name(int type)
{
	BUG_ON(type >= ARRAY_SIZE(clk_names) || !clk_names[type]);
	return clk_names[type];
}

static void __init __ath79_set_clk(int type, const char *name, struct clk *clk)
{
	if (IS_ERR(clk))
		panic("failed to allocate %s clock structure", clk_names[type]);

	clks[type] = clk;
	clk_register_clkdev(clk, name, NULL);
}

static struct clk * __init ath79_set_clk(int type, unsigned long rate)
{
	const char *name = ath79_clk_name(type);
	struct clk *clk;

	clk = clk_register_fixed_rate(NULL, name, NULL, 0, rate);
	__ath79_set_clk(type, name, clk);
	return clk;
}

static struct clk * __init ath79_set_ff_clk(int type, const char *parent,
					    unsigned int mult, unsigned int div)
{
	const char *name = ath79_clk_name(type);
	struct clk *clk;

	clk = clk_register_fixed_factor(NULL, name, parent, 0, mult, div);
	__ath79_set_clk(type, name, clk);
	return clk;
}

static unsigned long __init ath79_setup_ref_clk(unsigned long rate)
{
	struct clk *clk = clks[ATH79_CLK_REF];

	if (clk)
		rate = clk_get_rate(clk);
	else
		clk = ath79_set_clk(ATH79_CLK_REF, rate);

	return rate;
}

static void __init ar71xx_clocks_init(void __iomem *pll_base)
{
	unsigned long ref_rate;
	unsigned long cpu_rate;
	unsigned long ddr_rate;
	unsigned long ahb_rate;
	u32 pll;
	u32 freq;
	u32 div;

	ref_rate = ath79_setup_ref_clk(AR71XX_BASE_FREQ);

	pll = __raw_readl(pll_base + AR71XX_PLL_REG_CPU_CONFIG);

	div = ((pll >> AR71XX_PLL_FB_SHIFT) & AR71XX_PLL_FB_MASK) + 1;
	freq = div * ref_rate;

	div = ((pll >> AR71XX_CPU_DIV_SHIFT) & AR71XX_CPU_DIV_MASK) + 1;
	cpu_rate = freq / div;

	div = ((pll >> AR71XX_DDR_DIV_SHIFT) & AR71XX_DDR_DIV_MASK) + 1;
	ddr_rate = freq / div;

	div = (((pll >> AR71XX_AHB_DIV_SHIFT) & AR71XX_AHB_DIV_MASK) + 1) * 2;
	ahb_rate = cpu_rate / div;

	ath79_set_clk(ATH79_CLK_CPU, cpu_rate);
	ath79_set_clk(ATH79_CLK_DDR, ddr_rate);
	ath79_set_clk(ATH79_CLK_AHB, ahb_rate);
}

static void __init ar724x_clocks_init(void __iomem *pll_base)
{
	u32 mult, div, ddr_div, ahb_div;
	u32 pll;

	ath79_setup_ref_clk(AR71XX_BASE_FREQ);

	pll = __raw_readl(pll_base + AR724X_PLL_REG_CPU_CONFIG);

	mult = ((pll >> AR724X_PLL_FB_SHIFT) & AR724X_PLL_FB_MASK);
	div = ((pll >> AR724X_PLL_REF_DIV_SHIFT) & AR724X_PLL_REF_DIV_MASK) * 2;

	ddr_div = ((pll >> AR724X_DDR_DIV_SHIFT) & AR724X_DDR_DIV_MASK) + 1;
	ahb_div = (((pll >> AR724X_AHB_DIV_SHIFT) & AR724X_AHB_DIV_MASK) + 1) * 2;

	ath79_set_ff_clk(ATH79_CLK_CPU, "ref", mult, div);
	ath79_set_ff_clk(ATH79_CLK_DDR, "ref", mult, div * ddr_div);
	ath79_set_ff_clk(ATH79_CLK_AHB, "ref", mult, div * ahb_div);
}

static void __init ar933x_clocks_init(void __iomem *pll_base)
{
	unsigned long ref_rate;
	u32 clock_ctrl;
	u32 ref_div;
	u32 ninit_mul;
	u32 out_div;

	u32 cpu_div;
	u32 ddr_div;
	u32 ahb_div;
	u32 t;

	t = ath79_reset_rr(AR933X_RESET_REG_BOOTSTRAP);
	if (t & AR933X_BOOTSTRAP_REF_CLK_40)
		ref_rate = (40 * 1000 * 1000);
	else
		ref_rate = (25 * 1000 * 1000);

	ath79_setup_ref_clk(ref_rate);

	clock_ctrl = __raw_readl(pll_base + AR933X_PLL_CLOCK_CTRL_REG);
	if (clock_ctrl & AR933X_PLL_CLOCK_CTRL_BYPASS) {
		ref_div = 1;
		ninit_mul = 1;
		out_div = 1;

		cpu_div = 1;
		ddr_div = 1;
		ahb_div = 1;
	} else {
		u32 cpu_config;
		u32 t;

		cpu_config = __raw_readl(pll_base + AR933X_PLL_CPU_CONFIG_REG);

		t = (cpu_config >> AR933X_PLL_CPU_CONFIG_REFDIV_SHIFT) &
		    AR933X_PLL_CPU_CONFIG_REFDIV_MASK;
		ref_div = t;

		ninit_mul = (cpu_config >> AR933X_PLL_CPU_CONFIG_NINT_SHIFT) &
		    AR933X_PLL_CPU_CONFIG_NINT_MASK;

		t = (cpu_config >> AR933X_PLL_CPU_CONFIG_OUTDIV_SHIFT) &
		    AR933X_PLL_CPU_CONFIG_OUTDIV_MASK;
		if (t == 0)
			t = 1;

		out_div = (1 << t);

		cpu_div = ((clock_ctrl >> AR933X_PLL_CLOCK_CTRL_CPU_DIV_SHIFT) &
		     AR933X_PLL_CLOCK_CTRL_CPU_DIV_MASK) + 1;

		ddr_div = ((clock_ctrl >> AR933X_PLL_CLOCK_CTRL_DDR_DIV_SHIFT) &
		      AR933X_PLL_CLOCK_CTRL_DDR_DIV_MASK) + 1;

		ahb_div = ((clock_ctrl >> AR933X_PLL_CLOCK_CTRL_AHB_DIV_SHIFT) &
		     AR933X_PLL_CLOCK_CTRL_AHB_DIV_MASK) + 1;
	}

	ath79_set_ff_clk(ATH79_CLK_CPU, "ref", ninit_mul,
			 ref_div * out_div * cpu_div);
	ath79_set_ff_clk(ATH79_CLK_DDR, "ref", ninit_mul,
			 ref_div * out_div * ddr_div);
	ath79_set_ff_clk(ATH79_CLK_AHB, "ref", ninit_mul,
			 ref_div * out_div * ahb_div);
}

static u32 __init ar934x_get_pll_freq(u32 ref, u32 ref_div, u32 nint, u32 nfrac,
				      u32 frac, u32 out_div)
{
	u64 t;
	u32 ret;

	t = ref;
	t *= nint;
	do_div(t, ref_div);
	ret = t;

	t = ref;
	t *= nfrac;
	do_div(t, ref_div * frac);
	ret += t;

	ret /= (1 << out_div);
	return ret;
}

static void __init ar934x_clocks_init(void __iomem *pll_base)
{
	unsigned long ref_rate;
	unsigned long cpu_rate;
	unsigned long ddr_rate;
	unsigned long ahb_rate;
	u32 pll, out_div, ref_div, nint, nfrac, frac, clk_ctrl, postdiv;
	u32 cpu_pll, ddr_pll;
	u32 bootstrap;
	void __iomem *dpll_base;

	dpll_base = ioremap(AR934X_SRIF_BASE, AR934X_SRIF_SIZE);

	bootstrap = ath79_reset_rr(AR934X_RESET_REG_BOOTSTRAP);
	if (bootstrap & AR934X_BOOTSTRAP_REF_CLK_40)
		ref_rate = 40 * 1000 * 1000;
	else
		ref_rate = 25 * 1000 * 1000;

	ref_rate = ath79_setup_ref_clk(ref_rate);

	pll = __raw_readl(dpll_base + AR934X_SRIF_CPU_DPLL2_REG);
	if (pll & AR934X_SRIF_DPLL2_LOCAL_PLL) {
		out_div = (pll >> AR934X_SRIF_DPLL2_OUTDIV_SHIFT) &
			  AR934X_SRIF_DPLL2_OUTDIV_MASK;
		pll = __raw_readl(dpll_base + AR934X_SRIF_CPU_DPLL1_REG);
		nint = (pll >> AR934X_SRIF_DPLL1_NINT_SHIFT) &
		       AR934X_SRIF_DPLL1_NINT_MASK;
		nfrac = pll & AR934X_SRIF_DPLL1_NFRAC_MASK;
		ref_div = (pll >> AR934X_SRIF_DPLL1_REFDIV_SHIFT) &
			  AR934X_SRIF_DPLL1_REFDIV_MASK;
		frac = 1 << 18;
	} else {
		pll = __raw_readl(pll_base + AR934X_PLL_CPU_CONFIG_REG);
		out_div = (pll >> AR934X_PLL_CPU_CONFIG_OUTDIV_SHIFT) &
			AR934X_PLL_CPU_CONFIG_OUTDIV_MASK;
		ref_div = (pll >> AR934X_PLL_CPU_CONFIG_REFDIV_SHIFT) &
			  AR934X_PLL_CPU_CONFIG_REFDIV_MASK;
		nint = (pll >> AR934X_PLL_CPU_CONFIG_NINT_SHIFT) &
		       AR934X_PLL_CPU_CONFIG_NINT_MASK;
		nfrac = (pll >> AR934X_PLL_CPU_CONFIG_NFRAC_SHIFT) &
			AR934X_PLL_CPU_CONFIG_NFRAC_MASK;
		frac = 1 << 6;
	}

	cpu_pll = ar934x_get_pll_freq(ref_rate, ref_div, nint,
				      nfrac, frac, out_div);

	pll = __raw_readl(dpll_base + AR934X_SRIF_DDR_DPLL2_REG);
	if (pll & AR934X_SRIF_DPLL2_LOCAL_PLL) {
		out_div = (pll >> AR934X_SRIF_DPLL2_OUTDIV_SHIFT) &
			  AR934X_SRIF_DPLL2_OUTDIV_MASK;
		pll = __raw_readl(dpll_base + AR934X_SRIF_DDR_DPLL1_REG);
		nint = (pll >> AR934X_SRIF_DPLL1_NINT_SHIFT) &
		       AR934X_SRIF_DPLL1_NINT_MASK;
		nfrac = pll & AR934X_SRIF_DPLL1_NFRAC_MASK;
		ref_div = (pll >> AR934X_SRIF_DPLL1_REFDIV_SHIFT) &
			  AR934X_SRIF_DPLL1_REFDIV_MASK;
		frac = 1 << 18;
	} else {
		pll = __raw_readl(pll_base + AR934X_PLL_DDR_CONFIG_REG);
		out_div = (pll >> AR934X_PLL_DDR_CONFIG_OUTDIV_SHIFT) &
			  AR934X_PLL_DDR_CONFIG_OUTDIV_MASK;
		ref_div = (pll >> AR934X_PLL_DDR_CONFIG_REFDIV_SHIFT) &
			   AR934X_PLL_DDR_CONFIG_REFDIV_MASK;
		nint = (pll >> AR934X_PLL_DDR_CONFIG_NINT_SHIFT) &
		       AR934X_PLL_DDR_CONFIG_NINT_MASK;
		nfrac = (pll >> AR934X_PLL_DDR_CONFIG_NFRAC_SHIFT) &
			AR934X_PLL_DDR_CONFIG_NFRAC_MASK;
		frac = 1 << 10;
	}

	ddr_pll = ar934x_get_pll_freq(ref_rate, ref_div, nint,
				      nfrac, frac, out_div);

	clk_ctrl = __raw_readl(pll_base + AR934X_PLL_CPU_DDR_CLK_CTRL_REG);

	postdiv = (clk_ctrl >> AR934X_PLL_CPU_DDR_CLK_CTRL_CPU_POST_DIV_SHIFT) &
		  AR934X_PLL_CPU_DDR_CLK_CTRL_CPU_POST_DIV_MASK;

	if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_CPU_PLL_BYPASS)
		cpu_rate = ref_rate;
	else if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_CPUCLK_FROM_CPUPLL)
		cpu_rate = cpu_pll / (postdiv + 1);
	else
		cpu_rate = ddr_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> AR934X_PLL_CPU_DDR_CLK_CTRL_DDR_POST_DIV_SHIFT) &
		  AR934X_PLL_CPU_DDR_CLK_CTRL_DDR_POST_DIV_MASK;

	if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_DDR_PLL_BYPASS)
		ddr_rate = ref_rate;
	else if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_DDRCLK_FROM_DDRPLL)
		ddr_rate = ddr_pll / (postdiv + 1);
	else
		ddr_rate = cpu_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> AR934X_PLL_CPU_DDR_CLK_CTRL_AHB_POST_DIV_SHIFT) &
		  AR934X_PLL_CPU_DDR_CLK_CTRL_AHB_POST_DIV_MASK;

	if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_AHB_PLL_BYPASS)
		ahb_rate = ref_rate;
	else if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_AHBCLK_FROM_DDRPLL)
		ahb_rate = ddr_pll / (postdiv + 1);
	else
		ahb_rate = cpu_pll / (postdiv + 1);

	ath79_set_clk(ATH79_CLK_CPU, cpu_rate);
	ath79_set_clk(ATH79_CLK_DDR, ddr_rate);
	ath79_set_clk(ATH79_CLK_AHB, ahb_rate);

	clk_ctrl = __raw_readl(pll_base + AR934X_PLL_SWITCH_CLOCK_CONTROL_REG);
	if (clk_ctrl & AR934X_PLL_SWITCH_CLOCK_CONTROL_MDIO_CLK_SEL)
		ath79_set_clk(ATH79_CLK_MDIO, 100 * 1000 * 1000);

	iounmap(dpll_base);
}

static void __init qca953x_clocks_init(void __iomem *pll_base)
{
	unsigned long ref_rate;
	unsigned long cpu_rate;
	unsigned long ddr_rate;
	unsigned long ahb_rate;
	u32 pll, out_div, ref_div, nint, frac, clk_ctrl, postdiv;
	u32 cpu_pll, ddr_pll;
	u32 bootstrap;

	bootstrap = ath79_reset_rr(QCA953X_RESET_REG_BOOTSTRAP);
	if (bootstrap &	QCA953X_BOOTSTRAP_REF_CLK_40)
		ref_rate = 40 * 1000 * 1000;
	else
		ref_rate = 25 * 1000 * 1000;

	ref_rate = ath79_setup_ref_clk(ref_rate);

	pll = __raw_readl(pll_base + QCA953X_PLL_CPU_CONFIG_REG);
	out_div = (pll >> QCA953X_PLL_CPU_CONFIG_OUTDIV_SHIFT) &
		  QCA953X_PLL_CPU_CONFIG_OUTDIV_MASK;
	ref_div = (pll >> QCA953X_PLL_CPU_CONFIG_REFDIV_SHIFT) &
		  QCA953X_PLL_CPU_CONFIG_REFDIV_MASK;
	nint = (pll >> QCA953X_PLL_CPU_CONFIG_NINT_SHIFT) &
	       QCA953X_PLL_CPU_CONFIG_NINT_MASK;
	frac = (pll >> QCA953X_PLL_CPU_CONFIG_NFRAC_SHIFT) &
	       QCA953X_PLL_CPU_CONFIG_NFRAC_MASK;

	cpu_pll = nint * ref_rate / ref_div;
	cpu_pll += frac * (ref_rate >> 6) / ref_div;
	cpu_pll /= (1 << out_div);

	pll = __raw_readl(pll_base + QCA953X_PLL_DDR_CONFIG_REG);
	out_div = (pll >> QCA953X_PLL_DDR_CONFIG_OUTDIV_SHIFT) &
		  QCA953X_PLL_DDR_CONFIG_OUTDIV_MASK;
	ref_div = (pll >> QCA953X_PLL_DDR_CONFIG_REFDIV_SHIFT) &
		  QCA953X_PLL_DDR_CONFIG_REFDIV_MASK;
	nint = (pll >> QCA953X_PLL_DDR_CONFIG_NINT_SHIFT) &
	       QCA953X_PLL_DDR_CONFIG_NINT_MASK;
	frac = (pll >> QCA953X_PLL_DDR_CONFIG_NFRAC_SHIFT) &
	       QCA953X_PLL_DDR_CONFIG_NFRAC_MASK;

	ddr_pll = nint * ref_rate / ref_div;
	ddr_pll += frac * (ref_rate >> 6) / (ref_div << 4);
	ddr_pll /= (1 << out_div);

	clk_ctrl = __raw_readl(pll_base + QCA953X_PLL_CLK_CTRL_REG);

	postdiv = (clk_ctrl >> QCA953X_PLL_CLK_CTRL_CPU_POST_DIV_SHIFT) &
		  QCA953X_PLL_CLK_CTRL_CPU_POST_DIV_MASK;

	if (clk_ctrl & QCA953X_PLL_CLK_CTRL_CPU_PLL_BYPASS)
		cpu_rate = ref_rate;
	else if (clk_ctrl & QCA953X_PLL_CLK_CTRL_CPUCLK_FROM_CPUPLL)
		cpu_rate = cpu_pll / (postdiv + 1);
	else
		cpu_rate = ddr_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> QCA953X_PLL_CLK_CTRL_DDR_POST_DIV_SHIFT) &
		  QCA953X_PLL_CLK_CTRL_DDR_POST_DIV_MASK;

	if (clk_ctrl & QCA953X_PLL_CLK_CTRL_DDR_PLL_BYPASS)
		ddr_rate = ref_rate;
	else if (clk_ctrl & QCA953X_PLL_CLK_CTRL_DDRCLK_FROM_DDRPLL)
		ddr_rate = ddr_pll / (postdiv + 1);
	else
		ddr_rate = cpu_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> QCA953X_PLL_CLK_CTRL_AHB_POST_DIV_SHIFT) &
		  QCA953X_PLL_CLK_CTRL_AHB_POST_DIV_MASK;

	if (clk_ctrl & QCA953X_PLL_CLK_CTRL_AHB_PLL_BYPASS)
		ahb_rate = ref_rate;
	else if (clk_ctrl & QCA953X_PLL_CLK_CTRL_AHBCLK_FROM_DDRPLL)
		ahb_rate = ddr_pll / (postdiv + 1);
	else
		ahb_rate = cpu_pll / (postdiv + 1);

	ath79_set_clk(ATH79_CLK_CPU, cpu_rate);
	ath79_set_clk(ATH79_CLK_DDR, ddr_rate);
	ath79_set_clk(ATH79_CLK_AHB, ahb_rate);
}

static void __init qca955x_clocks_init(void __iomem *pll_base)
{
	unsigned long ref_rate;
	unsigned long cpu_rate;
	unsigned long ddr_rate;
	unsigned long ahb_rate;
	u32 pll, out_div, ref_div, nint, frac, clk_ctrl, postdiv;
	u32 cpu_pll, ddr_pll;
	u32 bootstrap;

	bootstrap = ath79_reset_rr(QCA955X_RESET_REG_BOOTSTRAP);
	if (bootstrap &	QCA955X_BOOTSTRAP_REF_CLK_40)
		ref_rate = 40 * 1000 * 1000;
	else
		ref_rate = 25 * 1000 * 1000;

	ref_rate = ath79_setup_ref_clk(ref_rate);

	pll = __raw_readl(pll_base + QCA955X_PLL_CPU_CONFIG_REG);
	out_div = (pll >> QCA955X_PLL_CPU_CONFIG_OUTDIV_SHIFT) &
		  QCA955X_PLL_CPU_CONFIG_OUTDIV_MASK;
	ref_div = (pll >> QCA955X_PLL_CPU_CONFIG_REFDIV_SHIFT) &
		  QCA955X_PLL_CPU_CONFIG_REFDIV_MASK;
	nint = (pll >> QCA955X_PLL_CPU_CONFIG_NINT_SHIFT) &
	       QCA955X_PLL_CPU_CONFIG_NINT_MASK;
	frac = (pll >> QCA955X_PLL_CPU_CONFIG_NFRAC_SHIFT) &
	       QCA955X_PLL_CPU_CONFIG_NFRAC_MASK;

	cpu_pll = nint * ref_rate / ref_div;
	cpu_pll += frac * ref_rate / (ref_div * (1 << 6));
	cpu_pll /= (1 << out_div);

	pll = __raw_readl(pll_base + QCA955X_PLL_DDR_CONFIG_REG);
	out_div = (pll >> QCA955X_PLL_DDR_CONFIG_OUTDIV_SHIFT) &
		  QCA955X_PLL_DDR_CONFIG_OUTDIV_MASK;
	ref_div = (pll >> QCA955X_PLL_DDR_CONFIG_REFDIV_SHIFT) &
		  QCA955X_PLL_DDR_CONFIG_REFDIV_MASK;
	nint = (pll >> QCA955X_PLL_DDR_CONFIG_NINT_SHIFT) &
	       QCA955X_PLL_DDR_CONFIG_NINT_MASK;
	frac = (pll >> QCA955X_PLL_DDR_CONFIG_NFRAC_SHIFT) &
	       QCA955X_PLL_DDR_CONFIG_NFRAC_MASK;

	ddr_pll = nint * ref_rate / ref_div;
	ddr_pll += frac * ref_rate / (ref_div * (1 << 10));
	ddr_pll /= (1 << out_div);

	clk_ctrl = __raw_readl(pll_base + QCA955X_PLL_CLK_CTRL_REG);

	postdiv = (clk_ctrl >> QCA955X_PLL_CLK_CTRL_CPU_POST_DIV_SHIFT) &
		  QCA955X_PLL_CLK_CTRL_CPU_POST_DIV_MASK;

	if (clk_ctrl & QCA955X_PLL_CLK_CTRL_CPU_PLL_BYPASS)
		cpu_rate = ref_rate;
	else if (clk_ctrl & QCA955X_PLL_CLK_CTRL_CPUCLK_FROM_CPUPLL)
		cpu_rate = ddr_pll / (postdiv + 1);
	else
		cpu_rate = cpu_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> QCA955X_PLL_CLK_CTRL_DDR_POST_DIV_SHIFT) &
		  QCA955X_PLL_CLK_CTRL_DDR_POST_DIV_MASK;

	if (clk_ctrl & QCA955X_PLL_CLK_CTRL_DDR_PLL_BYPASS)
		ddr_rate = ref_rate;
	else if (clk_ctrl & QCA955X_PLL_CLK_CTRL_DDRCLK_FROM_DDRPLL)
		ddr_rate = cpu_pll / (postdiv + 1);
	else
		ddr_rate = ddr_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> QCA955X_PLL_CLK_CTRL_AHB_POST_DIV_SHIFT) &
		  QCA955X_PLL_CLK_CTRL_AHB_POST_DIV_MASK;

	if (clk_ctrl & QCA955X_PLL_CLK_CTRL_AHB_PLL_BYPASS)
		ahb_rate = ref_rate;
	else if (clk_ctrl & QCA955X_PLL_CLK_CTRL_AHBCLK_FROM_DDRPLL)
		ahb_rate = ddr_pll / (postdiv + 1);
	else
		ahb_rate = cpu_pll / (postdiv + 1);

	ath79_set_clk(ATH79_CLK_CPU, cpu_rate);
	ath79_set_clk(ATH79_CLK_DDR, ddr_rate);
	ath79_set_clk(ATH79_CLK_AHB, ahb_rate);
}

static void __init qca956x_clocks_init(void __iomem *pll_base)
{
	unsigned long ref_rate;
	unsigned long cpu_rate;
	unsigned long ddr_rate;
	unsigned long ahb_rate;
	u32 pll, out_div, ref_div, nint, hfrac, lfrac, clk_ctrl, postdiv;
	u32 cpu_pll, ddr_pll;
	u32 bootstrap;

	/*
	 * QCA956x timer init workaround has to be applied right before setting
	 * up the clock. Else, there will be no jiffies
	 */
	u32 misc;

	misc = ath79_reset_rr(AR71XX_RESET_REG_MISC_INT_ENABLE);
	misc |= MISC_INT_MIPS_SI_TIMERINT_MASK;
	ath79_reset_wr(AR71XX_RESET_REG_MISC_INT_ENABLE, misc);

	bootstrap = ath79_reset_rr(QCA956X_RESET_REG_BOOTSTRAP);
	if (bootstrap &	QCA956X_BOOTSTRAP_REF_CLK_40)
		ref_rate = 40 * 1000 * 1000;
	else
		ref_rate = 25 * 1000 * 1000;

	ref_rate = ath79_setup_ref_clk(ref_rate);

	pll = __raw_readl(pll_base + QCA956X_PLL_CPU_CONFIG_REG);
	out_div = (pll >> QCA956X_PLL_CPU_CONFIG_OUTDIV_SHIFT) &
		  QCA956X_PLL_CPU_CONFIG_OUTDIV_MASK;
	ref_div = (pll >> QCA956X_PLL_CPU_CONFIG_REFDIV_SHIFT) &
		  QCA956X_PLL_CPU_CONFIG_REFDIV_MASK;

	pll = __raw_readl(pll_base + QCA956X_PLL_CPU_CONFIG1_REG);
	nint = (pll >> QCA956X_PLL_CPU_CONFIG1_NINT_SHIFT) &
	       QCA956X_PLL_CPU_CONFIG1_NINT_MASK;
	hfrac = (pll >> QCA956X_PLL_CPU_CONFIG1_NFRAC_H_SHIFT) &
	       QCA956X_PLL_CPU_CONFIG1_NFRAC_H_MASK;
	lfrac = (pll >> QCA956X_PLL_CPU_CONFIG1_NFRAC_L_SHIFT) &
	       QCA956X_PLL_CPU_CONFIG1_NFRAC_L_MASK;

	cpu_pll = nint * ref_rate / ref_div;
	cpu_pll += (lfrac * ref_rate) / ((ref_div * 25) << 13);
	cpu_pll += (hfrac >> 13) * ref_rate / ref_div;
	cpu_pll /= (1 << out_div);

	pll = __raw_readl(pll_base + QCA956X_PLL_DDR_CONFIG_REG);
	out_div = (pll >> QCA956X_PLL_DDR_CONFIG_OUTDIV_SHIFT) &
		  QCA956X_PLL_DDR_CONFIG_OUTDIV_MASK;
	ref_div = (pll >> QCA956X_PLL_DDR_CONFIG_REFDIV_SHIFT) &
		  QCA956X_PLL_DDR_CONFIG_REFDIV_MASK;
	pll = __raw_readl(pll_base + QCA956X_PLL_DDR_CONFIG1_REG);
	nint = (pll >> QCA956X_PLL_DDR_CONFIG1_NINT_SHIFT) &
	       QCA956X_PLL_DDR_CONFIG1_NINT_MASK;
	hfrac = (pll >> QCA956X_PLL_DDR_CONFIG1_NFRAC_H_SHIFT) &
	       QCA956X_PLL_DDR_CONFIG1_NFRAC_H_MASK;
	lfrac = (pll >> QCA956X_PLL_DDR_CONFIG1_NFRAC_L_SHIFT) &
	       QCA956X_PLL_DDR_CONFIG1_NFRAC_L_MASK;

	ddr_pll = nint * ref_rate / ref_div;
	ddr_pll += (lfrac * ref_rate) / ((ref_div * 25) << 13);
	ddr_pll += (hfrac >> 13) * ref_rate / ref_div;
	ddr_pll /= (1 << out_div);

	clk_ctrl = __raw_readl(pll_base + QCA956X_PLL_CLK_CTRL_REG);

	postdiv = (clk_ctrl >> QCA956X_PLL_CLK_CTRL_CPU_POST_DIV_SHIFT) &
		  QCA956X_PLL_CLK_CTRL_CPU_POST_DIV_MASK;

	if (clk_ctrl & QCA956X_PLL_CLK_CTRL_CPU_PLL_BYPASS)
		cpu_rate = ref_rate;
	else if (clk_ctrl & QCA956X_PLL_CLK_CTRL_CPU_DDRCLK_FROM_CPUPLL)
		cpu_rate = ddr_pll / (postdiv + 1);
	else
		cpu_rate = cpu_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> QCA956X_PLL_CLK_CTRL_DDR_POST_DIV_SHIFT) &
		  QCA956X_PLL_CLK_CTRL_DDR_POST_DIV_MASK;

	if (clk_ctrl & QCA956X_PLL_CLK_CTRL_DDR_PLL_BYPASS)
		ddr_rate = ref_rate;
	else if (clk_ctrl & QCA956X_PLL_CLK_CTRL_CPU_DDRCLK_FROM_DDRPLL)
		ddr_rate = cpu_pll / (postdiv + 1);
	else
		ddr_rate = ddr_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> QCA956X_PLL_CLK_CTRL_AHB_POST_DIV_SHIFT) &
		  QCA956X_PLL_CLK_CTRL_AHB_POST_DIV_MASK;

	if (clk_ctrl & QCA956X_PLL_CLK_CTRL_AHB_PLL_BYPASS)
		ahb_rate = ref_rate;
	else if (clk_ctrl & QCA956X_PLL_CLK_CTRL_AHBCLK_FROM_DDRPLL)
		ahb_rate = ddr_pll / (postdiv + 1);
	else
		ahb_rate = cpu_pll / (postdiv + 1);

	ath79_set_clk(ATH79_CLK_CPU, cpu_rate);
	ath79_set_clk(ATH79_CLK_DDR, ddr_rate);
	ath79_set_clk(ATH79_CLK_AHB, ahb_rate);
}

static void __init ath79_clocks_init_dt(struct device_node *np)
{
	struct clk *ref_clk;
	void __iomem *pll_base;

	ref_clk = of_clk_get(np, 0);
	if (!IS_ERR(ref_clk))
		clks[ATH79_CLK_REF] = ref_clk;

	pll_base = of_iomap(np, 0);
	if (!pll_base) {
		pr_err("%pOF: can't map pll registers\n", np);
		goto err_clk;
	}

	if (of_device_is_compatible(np, "qca,ar7100-pll"))
		ar71xx_clocks_init(pll_base);
	else if (of_device_is_compatible(np, "qca,ar7240-pll") ||
		 of_device_is_compatible(np, "qca,ar9130-pll"))
		ar724x_clocks_init(pll_base);
	else if (of_device_is_compatible(np, "qca,ar9330-pll"))
		ar933x_clocks_init(pll_base);
	else if (of_device_is_compatible(np, "qca,ar9340-pll"))
		ar934x_clocks_init(pll_base);
	else if (of_device_is_compatible(np, "qca,qca9530-pll"))
		qca953x_clocks_init(pll_base);
	else if (of_device_is_compatible(np, "qca,qca9550-pll"))
		qca955x_clocks_init(pll_base);
	else if (of_device_is_compatible(np, "qca,qca9560-pll"))
		qca956x_clocks_init(pll_base);

	if (!clks[ATH79_CLK_MDIO])
		clks[ATH79_CLK_MDIO] = clks[ATH79_CLK_REF];

	if (of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data)) {
		pr_err("%pOF: could not register clk provider\n", np);
		goto err_iounmap;
	}

	return;

err_iounmap:
	iounmap(pll_base);

err_clk:
	clk_put(ref_clk);
}

CLK_OF_DECLARE(ar7100_clk, "qca,ar7100-pll", ath79_clocks_init_dt);
CLK_OF_DECLARE(ar7240_clk, "qca,ar7240-pll", ath79_clocks_init_dt);
CLK_OF_DECLARE(ar9130_clk, "qca,ar9130-pll", ath79_clocks_init_dt);
CLK_OF_DECLARE(ar9330_clk, "qca,ar9330-pll", ath79_clocks_init_dt);
CLK_OF_DECLARE(ar9340_clk, "qca,ar9340-pll", ath79_clocks_init_dt);
CLK_OF_DECLARE(ar9530_clk, "qca,qca9530-pll", ath79_clocks_init_dt);
CLK_OF_DECLARE(ar9550_clk, "qca,qca9550-pll", ath79_clocks_init_dt);
CLK_OF_DECLARE(ar9560_clk, "qca,qca9560-pll", ath79_clocks_init_dt);
