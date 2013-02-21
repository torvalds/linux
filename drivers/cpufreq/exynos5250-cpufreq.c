/*
 * Copyright (c) 2010-20122Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS5250 - CPU frequency scaling support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/cpufreq.h>

static struct clk *cpu_clk;
static struct clk *moutcore;
static struct clk *mout_mpll;
static struct clk *mout_apll;

static unsigned int exynos5250_volt_table[] = {
	1300000, 1250000, 1225000, 1200000, 1150000,
	1125000, 1100000, 1075000, 1050000, 1025000,
	1012500, 1000000,  975000,  950000,  937500,
	925000
};

static struct cpufreq_frequency_table exynos5250_freq_table[] = {
	{L0, 1700 * 1000},
	{L1, 1600 * 1000},
	{L2, 1500 * 1000},
	{L3, 1400 * 1000},
	{L4, 1300 * 1000},
	{L5, 1200 * 1000},
	{L6, 1100 * 1000},
	{L7, 1000 * 1000},
	{L8,  900 * 1000},
	{L9,  800 * 1000},
	{L10, 700 * 1000},
	{L11, 600 * 1000},
	{L12, 500 * 1000},
	{L13, 400 * 1000},
	{L14, 300 * 1000},
	{L15, 200 * 1000},
	{0, CPUFREQ_TABLE_END},
};

static struct apll_freq apll_freq_5250[] = {
	/*
	 * values:
	 * freq
	 * clock divider for ARM, CPUD, ACP, PERIPH, ATB, PCLK_DBG, APLL, ARM2
	 * clock divider for COPY, HPM, RESERVED
	 * PLL M, P, S
	 */
	APLL_FREQ(1700, 0, 3, 7, 7, 7, 3, 5, 0, 0, 2, 0, 425, 6, 0),
	APLL_FREQ(1600, 0, 3, 7, 7, 7, 1, 4, 0, 0, 2, 0, 200, 3, 0),
	APLL_FREQ(1500, 0, 2, 7, 7, 7, 1, 4, 0, 0, 2, 0, 250, 4, 0),
	APLL_FREQ(1400, 0, 2, 7, 7, 6, 1, 4, 0, 0, 2, 0, 175, 3, 0),
	APLL_FREQ(1300, 0, 2, 7, 7, 6, 1, 3, 0, 0, 2, 0, 325, 6, 0),
	APLL_FREQ(1200, 0, 2, 7, 7, 5, 1, 3, 0, 0, 2, 0, 200, 4, 0),
	APLL_FREQ(1100, 0, 3, 7, 7, 5, 1, 3, 0, 0, 2, 0, 275, 6, 0),
	APLL_FREQ(1000, 0, 1, 7, 7, 4, 1, 2, 0, 0, 2, 0, 125, 3, 0),
	APLL_FREQ(900,  0, 1, 7, 7, 4, 1, 2, 0, 0, 2, 0, 150, 4, 0),
	APLL_FREQ(800,  0, 1, 7, 7, 4, 1, 2, 0, 0, 2, 0, 100, 3, 0),
	APLL_FREQ(700,  0, 1, 7, 7, 3, 1, 1, 0, 0, 2, 0, 175, 3, 1),
	APLL_FREQ(600,  0, 1, 7, 7, 3, 1, 1, 0, 0, 2, 0, 200, 4, 1),
	APLL_FREQ(500,  0, 1, 7, 7, 2, 1, 1, 0, 0, 2, 0, 125, 3, 1),
	APLL_FREQ(400,  0, 1, 7, 7, 2, 1, 1, 0, 0, 2, 0, 100, 3, 1),
	APLL_FREQ(300,  0, 1, 7, 7, 1, 1, 1, 0, 0, 2, 0, 200, 4, 2),
	APLL_FREQ(200,  0, 1, 7, 7, 1, 1, 1, 0, 0, 2, 0, 100, 3, 2),
};

static void set_clkdiv(unsigned int div_index)
{
	unsigned int tmp;

	/* Change Divider - CPU0 */

	tmp = apll_freq_5250[div_index].clk_div_cpu0;

	__raw_writel(tmp, EXYNOS5_CLKDIV_CPU0);

	while (__raw_readl(EXYNOS5_CLKDIV_STATCPU0) & 0x11111111)
		cpu_relax();

	/* Change Divider - CPU1 */
	tmp = apll_freq_5250[div_index].clk_div_cpu1;

	__raw_writel(tmp, EXYNOS5_CLKDIV_CPU1);

	while (__raw_readl(EXYNOS5_CLKDIV_STATCPU1) & 0x11)
		cpu_relax();
}

static void set_apll(unsigned int new_index,
			     unsigned int old_index)
{
	unsigned int tmp, pdiv;

	/* 1. MUX_CORE_SEL = MPLL, ARMCLK uses MPLL for lock time */
	clk_set_parent(moutcore, mout_mpll);

	do {
		cpu_relax();
		tmp = (__raw_readl(EXYNOS5_CLKMUX_STATCPU) >> 16);
		tmp &= 0x7;
	} while (tmp != 0x2);

	/* 2. Set APLL Lock time */
	pdiv = ((apll_freq_5250[new_index].mps >> 8) & 0x3f);

	__raw_writel((pdiv * 250), EXYNOS5_APLL_LOCK);

	/* 3. Change PLL PMS values */
	tmp = __raw_readl(EXYNOS5_APLL_CON0);
	tmp &= ~((0x3ff << 16) | (0x3f << 8) | (0x7 << 0));
	tmp |= apll_freq_5250[new_index].mps;
	__raw_writel(tmp, EXYNOS5_APLL_CON0);

	/* 4. wait_lock_time */
	do {
		cpu_relax();
		tmp = __raw_readl(EXYNOS5_APLL_CON0);
	} while (!(tmp & (0x1 << 29)));

	/* 5. MUX_CORE_SEL = APLL */
	clk_set_parent(moutcore, mout_apll);

	do {
		cpu_relax();
		tmp = __raw_readl(EXYNOS5_CLKMUX_STATCPU);
		tmp &= (0x7 << 16);
	} while (tmp != (0x1 << 16));

}

static bool exynos5250_pms_change(unsigned int old_index, unsigned int new_index)
{
	unsigned int old_pm = apll_freq_5250[old_index].mps >> 8;
	unsigned int new_pm = apll_freq_5250[new_index].mps >> 8;

	return (old_pm == new_pm) ? 0 : 1;
}

static void exynos5250_set_frequency(unsigned int old_index,
				  unsigned int new_index)
{
	unsigned int tmp;

	if (old_index > new_index) {
		if (!exynos5250_pms_change(old_index, new_index)) {
			/* 1. Change the system clock divider values */
			set_clkdiv(new_index);
			/* 2. Change just s value in apll m,p,s value */
			tmp = __raw_readl(EXYNOS5_APLL_CON0);
			tmp &= ~(0x7 << 0);
			tmp |= apll_freq_5250[new_index].mps & 0x7;
			__raw_writel(tmp, EXYNOS5_APLL_CON0);

		} else {
			/* Clock Configuration Procedure */
			/* 1. Change the system clock divider values */
			set_clkdiv(new_index);
			/* 2. Change the apll m,p,s value */
			set_apll(new_index, old_index);
		}
	} else if (old_index < new_index) {
		if (!exynos5250_pms_change(old_index, new_index)) {
			/* 1. Change just s value in apll m,p,s value */
			tmp = __raw_readl(EXYNOS5_APLL_CON0);
			tmp &= ~(0x7 << 0);
			tmp |= apll_freq_5250[new_index].mps & 0x7;
			__raw_writel(tmp, EXYNOS5_APLL_CON0);
			/* 2. Change the system clock divider values */
			set_clkdiv(new_index);
		} else {
			/* Clock Configuration Procedure */
			/* 1. Change the apll m,p,s value */
			set_apll(new_index, old_index);
			/* 2. Change the system clock divider values */
			set_clkdiv(new_index);
		}
	}
}

int exynos5250_cpufreq_init(struct exynos_dvfs_info *info)
{
	unsigned long rate;

	cpu_clk = clk_get(NULL, "armclk");
	if (IS_ERR(cpu_clk))
		return PTR_ERR(cpu_clk);

	moutcore = clk_get(NULL, "mout_cpu");
	if (IS_ERR(moutcore))
		goto err_moutcore;

	mout_mpll = clk_get(NULL, "mout_mpll");
	if (IS_ERR(mout_mpll))
		goto err_mout_mpll;

	rate = clk_get_rate(mout_mpll) / 1000;

	mout_apll = clk_get(NULL, "mout_apll");
	if (IS_ERR(mout_apll))
		goto err_mout_apll;

	info->mpll_freq_khz = rate;
	/* 800Mhz */
	info->pll_safe_idx = L9;
	info->cpu_clk = cpu_clk;
	info->volt_table = exynos5250_volt_table;
	info->freq_table = exynos5250_freq_table;
	info->set_freq = exynos5250_set_frequency;
	info->need_apll_change = exynos5250_pms_change;

	return 0;

err_mout_apll:
	clk_put(mout_mpll);
err_mout_mpll:
	clk_put(moutcore);
err_moutcore:
	clk_put(cpu_clk);

	pr_err("%s: failed initialization\n", __func__);
	return -EINVAL;
}
EXPORT_SYMBOL(exynos5250_cpufreq_init);
