/*
 *  Atheros AR71XX/AR724X/AR913X common routines
 *
 *  Copyright (C) 2011 Gabor Juhos <juhosg@openwrt.org>
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
