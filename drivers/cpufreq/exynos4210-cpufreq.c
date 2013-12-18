/*
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4210 - CPU frequency scaling support
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

#include "exynos-cpufreq.h"

static struct clk *cpu_clk;
static struct clk *moutcore;
static struct clk *mout_mpll;
static struct clk *mout_apll;

static unsigned int exynos4210_volt_table[] = {
	1250000, 1150000, 1050000, 975000, 950000,
};

static struct cpufreq_frequency_table exynos4210_freq_table[] = {
	{L0, 1200 * 1000},
	{L1, 1000 * 1000},
	{L2,  800 * 1000},
	{L3,  500 * 1000},
	{L4,  200 * 1000},
	{0, CPUFREQ_TABLE_END},
};

static struct apll_freq apll_freq_4210[] = {
	/*
	 * values:
	 * freq
	 * clock divider for CORE, COREM0, COREM1, PERIPH, ATB, PCLK_DBG, APLL, RESERVED
	 * clock divider for COPY, HPM, RESERVED
	 * PLL M, P, S
	 */
	APLL_FREQ(1200, 0, 3, 7, 3, 4, 1, 7, 0, 5, 0, 0, 150, 3, 1),
	APLL_FREQ(1000, 0, 3, 7, 3, 4, 1, 7, 0, 4, 0, 0, 250, 6, 1),
	APLL_FREQ(800,  0, 3, 7, 3, 3, 1, 7, 0, 3, 0, 0, 200, 6, 1),
	APLL_FREQ(500,  0, 3, 7, 3, 3, 1, 7, 0, 3, 0, 0, 250, 6, 2),
	APLL_FREQ(200,  0, 1, 3, 1, 3, 1, 0, 0, 3, 0, 0, 200, 6, 3),
};

static void exynos4210_set_clkdiv(unsigned int div_index)
{
	unsigned int tmp;

	/* Change Divider - CPU0 */

	tmp = apll_freq_4210[div_index].clk_div_cpu0;

	__raw_writel(tmp, EXYNOS4_CLKDIV_CPU);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STATCPU);
	} while (tmp & 0x1111111);

	/* Change Divider - CPU1 */

	tmp = apll_freq_4210[div_index].clk_div_cpu1;

	__raw_writel(tmp, EXYNOS4_CLKDIV_CPU1);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STATCPU1);
	} while (tmp & 0x11);
}

static void exynos4210_set_apll(unsigned int index)
{
	unsigned int tmp, freq = apll_freq_4210[index].freq;

	/* MUX_CORE_SEL = MPLL, ARMCLK uses MPLL for lock time */
	clk_set_parent(moutcore, mout_mpll);

	do {
		tmp = (__raw_readl(EXYNOS4_CLKMUX_STATCPU)
			>> EXYNOS4_CLKSRC_CPU_MUXCORE_SHIFT);
		tmp &= 0x7;
	} while (tmp != 0x2);

	clk_set_rate(mout_apll, freq * 1000);

	/* MUX_CORE_SEL = APLL */
	clk_set_parent(moutcore, mout_apll);

	do {
		tmp = __raw_readl(EXYNOS4_CLKMUX_STATCPU);
		tmp &= EXYNOS4_CLKMUX_STATCPU_MUXCORE_MASK;
	} while (tmp != (0x1 << EXYNOS4_CLKSRC_CPU_MUXCORE_SHIFT));
}

static void exynos4210_set_frequency(unsigned int old_index,
				     unsigned int new_index)
{
	if (old_index > new_index) {
		exynos4210_set_clkdiv(new_index);
		exynos4210_set_apll(new_index);
	} else if (old_index < new_index) {
		exynos4210_set_apll(new_index);
		exynos4210_set_clkdiv(new_index);
	}
}

int exynos4210_cpufreq_init(struct exynos_dvfs_info *info)
{
	unsigned long rate;

	cpu_clk = clk_get(NULL, "armclk");
	if (IS_ERR(cpu_clk))
		return PTR_ERR(cpu_clk);

	moutcore = clk_get(NULL, "moutcore");
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
	info->pll_safe_idx = L2;
	info->cpu_clk = cpu_clk;
	info->volt_table = exynos4210_volt_table;
	info->freq_table = exynos4210_freq_table;
	info->set_freq = exynos4210_set_frequency;

	return 0;

err_mout_apll:
	clk_put(mout_mpll);
err_mout_mpll:
	clk_put(moutcore);
err_moutcore:
	clk_put(cpu_clk);

	pr_debug("%s: failed initialization\n", __func__);
	return -EINVAL;
}
EXPORT_SYMBOL(exynos4210_cpufreq_init);
