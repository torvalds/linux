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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>

#include <asm/div64.h>

#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>
#include "common.h"

#define AR71XX_BASE_FREQ	40000000
#define AR724X_BASE_FREQ	5000000
#define AR913X_BASE_FREQ	5000000

static struct clk *clks[3];
static struct clk_onecell_data clk_data = {
	.clks = clks,
	.clk_num = ARRAY_SIZE(clks),
};

static struct clk *__init ath79_add_sys_clkdev(
	const char *id, unsigned long rate)
{
	struct clk *clk;
	int err;

	clk = clk_register_fixed_rate(NULL, id, NULL, CLK_IS_ROOT, rate);
	if (!clk)
		panic("failed to allocate %s clock structure", id);

	err = clk_register_clkdev(clk, id, NULL);
	if (err)
		panic("unable to register %s clock device", id);

	return clk;
}

static void __init ar71xx_clocks_init(void)
{
	unsigned long ref_rate;
	unsigned long cpu_rate;
	unsigned long ddr_rate;
	unsigned long ahb_rate;
	u32 pll;
	u32 freq;
	u32 div;

	ref_rate = AR71XX_BASE_FREQ;

	pll = ath79_pll_rr(AR71XX_PLL_REG_CPU_CONFIG);

	div = ((pll >> AR71XX_PLL_FB_SHIFT) & AR71XX_PLL_FB_MASK) + 1;
	freq = div * ref_rate;

	div = ((pll >> AR71XX_CPU_DIV_SHIFT) & AR71XX_CPU_DIV_MASK) + 1;
	cpu_rate = freq / div;

	div = ((pll >> AR71XX_DDR_DIV_SHIFT) & AR71XX_DDR_DIV_MASK) + 1;
	ddr_rate = freq / div;

	div = (((pll >> AR71XX_AHB_DIV_SHIFT) & AR71XX_AHB_DIV_MASK) + 1) * 2;
	ahb_rate = cpu_rate / div;

	ath79_add_sys_clkdev("ref", ref_rate);
	clks[0] = ath79_add_sys_clkdev("cpu", cpu_rate);
	clks[1] = ath79_add_sys_clkdev("ddr", ddr_rate);
	clks[2] = ath79_add_sys_clkdev("ahb", ahb_rate);

	clk_add_alias("wdt", NULL, "ahb", NULL);
	clk_add_alias("uart", NULL, "ahb", NULL);
}

static void __init ar724x_clocks_init(void)
{
	unsigned long ref_rate;
	unsigned long cpu_rate;
	unsigned long ddr_rate;
	unsigned long ahb_rate;
	u32 pll;
	u32 freq;
	u32 div;

	ref_rate = AR724X_BASE_FREQ;
	pll = ath79_pll_rr(AR724X_PLL_REG_CPU_CONFIG);

	div = ((pll >> AR724X_PLL_FB_SHIFT) & AR724X_PLL_FB_MASK);
	freq = div * ref_rate;

	div = ((pll >> AR724X_PLL_REF_DIV_SHIFT) & AR724X_PLL_REF_DIV_MASK);
	freq *= div;

	cpu_rate = freq;

	div = ((pll >> AR724X_DDR_DIV_SHIFT) & AR724X_DDR_DIV_MASK) + 1;
	ddr_rate = freq / div;

	div = (((pll >> AR724X_AHB_DIV_SHIFT) & AR724X_AHB_DIV_MASK) + 1) * 2;
	ahb_rate = cpu_rate / div;

	ath79_add_sys_clkdev("ref", ref_rate);
	clks[0] = ath79_add_sys_clkdev("cpu", cpu_rate);
	clks[1] = ath79_add_sys_clkdev("ddr", ddr_rate);
	clks[2] = ath79_add_sys_clkdev("ahb", ahb_rate);

	clk_add_alias("wdt", NULL, "ahb", NULL);
	clk_add_alias("uart", NULL, "ahb", NULL);
}

static void __init ar913x_clocks_init(void)
{
	unsigned long ref_rate;
	unsigned long cpu_rate;
	unsigned long ddr_rate;
	unsigned long ahb_rate;
	u32 pll;
	u32 freq;
	u32 div;

	ref_rate = AR913X_BASE_FREQ;
	pll = ath79_pll_rr(AR913X_PLL_REG_CPU_CONFIG);

	div = ((pll >> AR913X_PLL_FB_SHIFT) & AR913X_PLL_FB_MASK);
	freq = div * ref_rate;

	cpu_rate = freq;

	div = ((pll >> AR913X_DDR_DIV_SHIFT) & AR913X_DDR_DIV_MASK) + 1;
	ddr_rate = freq / div;

	div = (((pll >> AR913X_AHB_DIV_SHIFT) & AR913X_AHB_DIV_MASK) + 1) * 2;
	ahb_rate = cpu_rate / div;

	ath79_add_sys_clkdev("ref", ref_rate);
	clks[0] = ath79_add_sys_clkdev("cpu", cpu_rate);
	clks[1] = ath79_add_sys_clkdev("ddr", ddr_rate);
	clks[2] = ath79_add_sys_clkdev("ahb", ahb_rate);

	clk_add_alias("wdt", NULL, "ahb", NULL);
	clk_add_alias("uart", NULL, "ahb", NULL);
}

static void __init ar933x_clocks_init(void)
{
	unsigned long ref_rate;
	unsigned long cpu_rate;
	unsigned long ddr_rate;
	unsigned long ahb_rate;
	u32 clock_ctrl;
	u32 cpu_config;
	u32 freq;
	u32 t;

	t = ath79_reset_rr(AR933X_RESET_REG_BOOTSTRAP);
	if (t & AR933X_BOOTSTRAP_REF_CLK_40)
		ref_rate = (40 * 1000 * 1000);
	else
		ref_rate = (25 * 1000 * 1000);

	clock_ctrl = ath79_pll_rr(AR933X_PLL_CLOCK_CTRL_REG);
	if (clock_ctrl & AR933X_PLL_CLOCK_CTRL_BYPASS) {
		cpu_rate = ref_rate;
		ahb_rate = ref_rate;
		ddr_rate = ref_rate;
	} else {
		cpu_config = ath79_pll_rr(AR933X_PLL_CPU_CONFIG_REG);

		t = (cpu_config >> AR933X_PLL_CPU_CONFIG_REFDIV_SHIFT) &
		    AR933X_PLL_CPU_CONFIG_REFDIV_MASK;
		freq = ref_rate / t;

		t = (cpu_config >> AR933X_PLL_CPU_CONFIG_NINT_SHIFT) &
		    AR933X_PLL_CPU_CONFIG_NINT_MASK;
		freq *= t;

		t = (cpu_config >> AR933X_PLL_CPU_CONFIG_OUTDIV_SHIFT) &
		    AR933X_PLL_CPU_CONFIG_OUTDIV_MASK;
		if (t == 0)
			t = 1;

		freq >>= t;

		t = ((clock_ctrl >> AR933X_PLL_CLOCK_CTRL_CPU_DIV_SHIFT) &
		     AR933X_PLL_CLOCK_CTRL_CPU_DIV_MASK) + 1;
		cpu_rate = freq / t;

		t = ((clock_ctrl >> AR933X_PLL_CLOCK_CTRL_DDR_DIV_SHIFT) &
		      AR933X_PLL_CLOCK_CTRL_DDR_DIV_MASK) + 1;
		ddr_rate = freq / t;

		t = ((clock_ctrl >> AR933X_PLL_CLOCK_CTRL_AHB_DIV_SHIFT) &
		     AR933X_PLL_CLOCK_CTRL_AHB_DIV_MASK) + 1;
		ahb_rate = freq / t;
	}

	ath79_add_sys_clkdev("ref", ref_rate);
	clks[0] = ath79_add_sys_clkdev("cpu", cpu_rate);
	clks[1] = ath79_add_sys_clkdev("ddr", ddr_rate);
	clks[2] = ath79_add_sys_clkdev("ahb", ahb_rate);

	clk_add_alias("wdt", NULL, "ahb", NULL);
	clk_add_alias("uart", NULL, "ref", NULL);
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

static void __init ar934x_clocks_init(void)
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
		pll = ath79_pll_rr(AR934X_PLL_CPU_CONFIG_REG);
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
		pll = ath79_pll_rr(AR934X_PLL_DDR_CONFIG_REG);
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

	clk_ctrl = ath79_pll_rr(AR934X_PLL_CPU_DDR_CLK_CTRL_REG);

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

	ath79_add_sys_clkdev("ref", ref_rate);
	clks[0] = ath79_add_sys_clkdev("cpu", cpu_rate);
	clks[1] = ath79_add_sys_clkdev("ddr", ddr_rate);
	clks[2] = ath79_add_sys_clkdev("ahb", ahb_rate);

	clk_add_alias("wdt", NULL, "ref", NULL);
	clk_add_alias("uart", NULL, "ref", NULL);

	iounmap(dpll_base);
}

static void __init qca955x_clocks_init(void)
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

	pll = ath79_pll_rr(QCA955X_PLL_CPU_CONFIG_REG);
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

	pll = ath79_pll_rr(QCA955X_PLL_DDR_CONFIG_REG);
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

	clk_ctrl = ath79_pll_rr(QCA955X_PLL_CLK_CTRL_REG);

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

	ath79_add_sys_clkdev("ref", ref_rate);
	clks[0] = ath79_add_sys_clkdev("cpu", cpu_rate);
	clks[1] = ath79_add_sys_clkdev("ddr", ddr_rate);
	clks[2] = ath79_add_sys_clkdev("ahb", ahb_rate);

	clk_add_alias("wdt", NULL, "ref", NULL);
	clk_add_alias("uart", NULL, "ref", NULL);
}

void __init ath79_clocks_init(void)
{
	if (soc_is_ar71xx())
		ar71xx_clocks_init();
	else if (soc_is_ar724x())
		ar724x_clocks_init();
	else if (soc_is_ar913x())
		ar913x_clocks_init();
	else if (soc_is_ar933x())
		ar933x_clocks_init();
	else if (soc_is_ar934x())
		ar934x_clocks_init();
	else if (soc_is_qca955x())
		qca955x_clocks_init();
	else
		BUG();

	of_clk_init(NULL);
}

unsigned long __init
ath79_get_sys_clk_rate(const char *id)
{
	struct clk *clk;
	unsigned long rate;

	clk = clk_get(NULL, id);
	if (IS_ERR(clk))
		panic("unable to get %s clock, err=%d", id, (int) PTR_ERR(clk));

	rate = clk_get_rate(clk);
	clk_put(clk);

	return rate;
}

#ifdef CONFIG_OF
static void __init ath79_clocks_init_dt(struct device_node *np)
{
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
}

CLK_OF_DECLARE(ar7100, "qca,ar7100-pll", ath79_clocks_init_dt);
CLK_OF_DECLARE(ar7240, "qca,ar7240-pll", ath79_clocks_init_dt);
CLK_OF_DECLARE(ar9130, "qca,ar9130-pll", ath79_clocks_init_dt);
CLK_OF_DECLARE(ar9330, "qca,ar9330-pll", ath79_clocks_init_dt);
CLK_OF_DECLARE(ar9340, "qca,ar9340-pll", ath79_clocks_init_dt);
CLK_OF_DECLARE(ar9550, "qca,qca9550-pll", ath79_clocks_init_dt);
#endif
