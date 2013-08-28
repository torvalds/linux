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

#include <asm/div64.h>

#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>
#include "common.h"

#define AR71XX_BASE_FREQ	40000000
#define AR724X_BASE_FREQ	5000000
#define AR913X_BASE_FREQ	5000000

struct clk {
	unsigned long rate;
};

static struct clk ath79_ref_clk;
static struct clk ath79_cpu_clk;
static struct clk ath79_ddr_clk;
static struct clk ath79_ahb_clk;
static struct clk ath79_wdt_clk;
static struct clk ath79_uart_clk;

static void __init ar71xx_clocks_init(void)
{
	u32 pll;
	u32 freq;
	u32 div;

	ath79_ref_clk.rate = AR71XX_BASE_FREQ;

	pll = ath79_pll_rr(AR71XX_PLL_REG_CPU_CONFIG);

	div = ((pll >> AR71XX_PLL_DIV_SHIFT) & AR71XX_PLL_DIV_MASK) + 1;
	freq = div * ath79_ref_clk.rate;

	div = ((pll >> AR71XX_CPU_DIV_SHIFT) & AR71XX_CPU_DIV_MASK) + 1;
	ath79_cpu_clk.rate = freq / div;

	div = ((pll >> AR71XX_DDR_DIV_SHIFT) & AR71XX_DDR_DIV_MASK) + 1;
	ath79_ddr_clk.rate = freq / div;

	div = (((pll >> AR71XX_AHB_DIV_SHIFT) & AR71XX_AHB_DIV_MASK) + 1) * 2;
	ath79_ahb_clk.rate = ath79_cpu_clk.rate / div;

	ath79_wdt_clk.rate = ath79_ahb_clk.rate;
	ath79_uart_clk.rate = ath79_ahb_clk.rate;
}

static void __init ar724x_clocks_init(void)
{
	u32 pll;
	u32 freq;
	u32 div;

	ath79_ref_clk.rate = AR724X_BASE_FREQ;
	pll = ath79_pll_rr(AR724X_PLL_REG_CPU_CONFIG);

	div = ((pll >> AR724X_PLL_DIV_SHIFT) & AR724X_PLL_DIV_MASK);
	freq = div * ath79_ref_clk.rate;

	div = ((pll >> AR724X_PLL_REF_DIV_SHIFT) & AR724X_PLL_REF_DIV_MASK);
	freq *= div;

	ath79_cpu_clk.rate = freq;

	div = ((pll >> AR724X_DDR_DIV_SHIFT) & AR724X_DDR_DIV_MASK) + 1;
	ath79_ddr_clk.rate = freq / div;

	div = (((pll >> AR724X_AHB_DIV_SHIFT) & AR724X_AHB_DIV_MASK) + 1) * 2;
	ath79_ahb_clk.rate = ath79_cpu_clk.rate / div;

	ath79_wdt_clk.rate = ath79_ahb_clk.rate;
	ath79_uart_clk.rate = ath79_ahb_clk.rate;
}

static void __init ar913x_clocks_init(void)
{
	u32 pll;
	u32 freq;
	u32 div;

	ath79_ref_clk.rate = AR913X_BASE_FREQ;
	pll = ath79_pll_rr(AR913X_PLL_REG_CPU_CONFIG);

	div = ((pll >> AR913X_PLL_DIV_SHIFT) & AR913X_PLL_DIV_MASK);
	freq = div * ath79_ref_clk.rate;

	ath79_cpu_clk.rate = freq;

	div = ((pll >> AR913X_DDR_DIV_SHIFT) & AR913X_DDR_DIV_MASK) + 1;
	ath79_ddr_clk.rate = freq / div;

	div = (((pll >> AR913X_AHB_DIV_SHIFT) & AR913X_AHB_DIV_MASK) + 1) * 2;
	ath79_ahb_clk.rate = ath79_cpu_clk.rate / div;

	ath79_wdt_clk.rate = ath79_ahb_clk.rate;
	ath79_uart_clk.rate = ath79_ahb_clk.rate;
}

static void __init ar933x_clocks_init(void)
{
	u32 clock_ctrl;
	u32 cpu_config;
	u32 freq;
	u32 t;

	t = ath79_reset_rr(AR933X_RESET_REG_BOOTSTRAP);
	if (t & AR933X_BOOTSTRAP_REF_CLK_40)
		ath79_ref_clk.rate = (40 * 1000 * 1000);
	else
		ath79_ref_clk.rate = (25 * 1000 * 1000);

	clock_ctrl = ath79_pll_rr(AR933X_PLL_CLOCK_CTRL_REG);
	if (clock_ctrl & AR933X_PLL_CLOCK_CTRL_BYPASS) {
		ath79_cpu_clk.rate = ath79_ref_clk.rate;
		ath79_ahb_clk.rate = ath79_ref_clk.rate;
		ath79_ddr_clk.rate = ath79_ref_clk.rate;
	} else {
		cpu_config = ath79_pll_rr(AR933X_PLL_CPU_CONFIG_REG);

		t = (cpu_config >> AR933X_PLL_CPU_CONFIG_REFDIV_SHIFT) &
		    AR933X_PLL_CPU_CONFIG_REFDIV_MASK;
		freq = ath79_ref_clk.rate / t;

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
		ath79_cpu_clk.rate = freq / t;

		t = ((clock_ctrl >> AR933X_PLL_CLOCK_CTRL_DDR_DIV_SHIFT) &
		      AR933X_PLL_CLOCK_CTRL_DDR_DIV_MASK) + 1;
		ath79_ddr_clk.rate = freq / t;

		t = ((clock_ctrl >> AR933X_PLL_CLOCK_CTRL_AHB_DIV_SHIFT) &
		     AR933X_PLL_CLOCK_CTRL_AHB_DIV_MASK) + 1;
		ath79_ahb_clk.rate = freq / t;
	}

	ath79_wdt_clk.rate = ath79_ahb_clk.rate;
	ath79_uart_clk.rate = ath79_ref_clk.rate;
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
	u32 pll, out_div, ref_div, nint, nfrac, frac, clk_ctrl, postdiv;
	u32 cpu_pll, ddr_pll;
	u32 bootstrap;
	void __iomem *dpll_base;

	dpll_base = ioremap(AR934X_SRIF_BASE, AR934X_SRIF_SIZE);

	bootstrap = ath79_reset_rr(AR934X_RESET_REG_BOOTSTRAP);
	if (bootstrap & AR934X_BOOTSTRAP_REF_CLK_40)
		ath79_ref_clk.rate = 40 * 1000 * 1000;
	else
		ath79_ref_clk.rate = 25 * 1000 * 1000;

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

	cpu_pll = ar934x_get_pll_freq(ath79_ref_clk.rate, ref_div, nint,
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

	ddr_pll = ar934x_get_pll_freq(ath79_ref_clk.rate, ref_div, nint,
				      nfrac, frac, out_div);

	clk_ctrl = ath79_pll_rr(AR934X_PLL_CPU_DDR_CLK_CTRL_REG);

	postdiv = (clk_ctrl >> AR934X_PLL_CPU_DDR_CLK_CTRL_CPU_POST_DIV_SHIFT) &
		  AR934X_PLL_CPU_DDR_CLK_CTRL_CPU_POST_DIV_MASK;

	if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_CPU_PLL_BYPASS)
		ath79_cpu_clk.rate = ath79_ref_clk.rate;
	else if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_CPUCLK_FROM_CPUPLL)
		ath79_cpu_clk.rate = cpu_pll / (postdiv + 1);
	else
		ath79_cpu_clk.rate = ddr_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> AR934X_PLL_CPU_DDR_CLK_CTRL_DDR_POST_DIV_SHIFT) &
		  AR934X_PLL_CPU_DDR_CLK_CTRL_DDR_POST_DIV_MASK;

	if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_DDR_PLL_BYPASS)
		ath79_ddr_clk.rate = ath79_ref_clk.rate;
	else if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_DDRCLK_FROM_DDRPLL)
		ath79_ddr_clk.rate = ddr_pll / (postdiv + 1);
	else
		ath79_ddr_clk.rate = cpu_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> AR934X_PLL_CPU_DDR_CLK_CTRL_AHB_POST_DIV_SHIFT) &
		  AR934X_PLL_CPU_DDR_CLK_CTRL_AHB_POST_DIV_MASK;

	if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_AHB_PLL_BYPASS)
		ath79_ahb_clk.rate = ath79_ref_clk.rate;
	else if (clk_ctrl & AR934X_PLL_CPU_DDR_CLK_CTRL_AHBCLK_FROM_DDRPLL)
		ath79_ahb_clk.rate = ddr_pll / (postdiv + 1);
	else
		ath79_ahb_clk.rate = cpu_pll / (postdiv + 1);

	ath79_wdt_clk.rate = ath79_ref_clk.rate;
	ath79_uart_clk.rate = ath79_ref_clk.rate;

	iounmap(dpll_base);
}

static void __init qca955x_clocks_init(void)
{
	u32 pll, out_div, ref_div, nint, frac, clk_ctrl, postdiv;
	u32 cpu_pll, ddr_pll;
	u32 bootstrap;

	bootstrap = ath79_reset_rr(QCA955X_RESET_REG_BOOTSTRAP);
	if (bootstrap &	QCA955X_BOOTSTRAP_REF_CLK_40)
		ath79_ref_clk.rate = 40 * 1000 * 1000;
	else
		ath79_ref_clk.rate = 25 * 1000 * 1000;

	pll = ath79_pll_rr(QCA955X_PLL_CPU_CONFIG_REG);
	out_div = (pll >> QCA955X_PLL_CPU_CONFIG_OUTDIV_SHIFT) &
		  QCA955X_PLL_CPU_CONFIG_OUTDIV_MASK;
	ref_div = (pll >> QCA955X_PLL_CPU_CONFIG_REFDIV_SHIFT) &
		  QCA955X_PLL_CPU_CONFIG_REFDIV_MASK;
	nint = (pll >> QCA955X_PLL_CPU_CONFIG_NINT_SHIFT) &
	       QCA955X_PLL_CPU_CONFIG_NINT_MASK;
	frac = (pll >> QCA955X_PLL_CPU_CONFIG_NFRAC_SHIFT) &
	       QCA955X_PLL_CPU_CONFIG_NFRAC_MASK;

	cpu_pll = nint * ath79_ref_clk.rate / ref_div;
	cpu_pll += frac * ath79_ref_clk.rate / (ref_div * (1 << 6));
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

	ddr_pll = nint * ath79_ref_clk.rate / ref_div;
	ddr_pll += frac * ath79_ref_clk.rate / (ref_div * (1 << 10));
	ddr_pll /= (1 << out_div);

	clk_ctrl = ath79_pll_rr(QCA955X_PLL_CLK_CTRL_REG);

	postdiv = (clk_ctrl >> QCA955X_PLL_CLK_CTRL_CPU_POST_DIV_SHIFT) &
		  QCA955X_PLL_CLK_CTRL_CPU_POST_DIV_MASK;

	if (clk_ctrl & QCA955X_PLL_CLK_CTRL_CPU_PLL_BYPASS)
		ath79_cpu_clk.rate = ath79_ref_clk.rate;
	else if (clk_ctrl & QCA955X_PLL_CLK_CTRL_CPUCLK_FROM_CPUPLL)
		ath79_cpu_clk.rate = ddr_pll / (postdiv + 1);
	else
		ath79_cpu_clk.rate = cpu_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> QCA955X_PLL_CLK_CTRL_DDR_POST_DIV_SHIFT) &
		  QCA955X_PLL_CLK_CTRL_DDR_POST_DIV_MASK;

	if (clk_ctrl & QCA955X_PLL_CLK_CTRL_DDR_PLL_BYPASS)
		ath79_ddr_clk.rate = ath79_ref_clk.rate;
	else if (clk_ctrl & QCA955X_PLL_CLK_CTRL_DDRCLK_FROM_DDRPLL)
		ath79_ddr_clk.rate = cpu_pll / (postdiv + 1);
	else
		ath79_ddr_clk.rate = ddr_pll / (postdiv + 1);

	postdiv = (clk_ctrl >> QCA955X_PLL_CLK_CTRL_AHB_POST_DIV_SHIFT) &
		  QCA955X_PLL_CLK_CTRL_AHB_POST_DIV_MASK;

	if (clk_ctrl & QCA955X_PLL_CLK_CTRL_AHB_PLL_BYPASS)
		ath79_ahb_clk.rate = ath79_ref_clk.rate;
	else if (clk_ctrl & QCA955X_PLL_CLK_CTRL_AHBCLK_FROM_DDRPLL)
		ath79_ahb_clk.rate = ddr_pll / (postdiv + 1);
	else
		ath79_ahb_clk.rate = cpu_pll / (postdiv + 1);

	ath79_wdt_clk.rate = ath79_ref_clk.rate;
	ath79_uart_clk.rate = ath79_ref_clk.rate;
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

	pr_info("Clocks: CPU:%lu.%03luMHz, DDR:%lu.%03luMHz, AHB:%lu.%03luMHz, "
		"Ref:%lu.%03luMHz",
		ath79_cpu_clk.rate / 1000000,
		(ath79_cpu_clk.rate / 1000) % 1000,
		ath79_ddr_clk.rate / 1000000,
		(ath79_ddr_clk.rate / 1000) % 1000,
		ath79_ahb_clk.rate / 1000000,
		(ath79_ahb_clk.rate / 1000) % 1000,
		ath79_ref_clk.rate / 1000000,
		(ath79_ref_clk.rate / 1000) % 1000);
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

/*
 * Linux clock API
 */
struct clk *clk_get(struct device *dev, const char *id)
{
	if (!strcmp(id, "ref"))
		return &ath79_ref_clk;

	if (!strcmp(id, "cpu"))
		return &ath79_cpu_clk;

	if (!strcmp(id, "ddr"))
		return &ath79_ddr_clk;

	if (!strcmp(id, "ahb"))
		return &ath79_ahb_clk;

	if (!strcmp(id, "wdt"))
		return &ath79_wdt_clk;

	if (!strcmp(id, "uart"))
		return &ath79_uart_clk;

	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(clk_get);

int clk_enable(struct clk *clk)
{
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);
