/*
 * Copyright (c) 2010-2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4X12 - CPU frequency scaling support
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
#include <linux/of.h>
#include <linux/of_address.h>

#include "exynos-cpufreq.h"

static struct clk *cpu_clk;
static struct clk *moutcore;
static struct clk *mout_mpll;
static struct clk *mout_apll;
static struct exynos_dvfs_info *cpufreq;

static unsigned int exynos4x12_volt_table[] = {
	1350000, 1287500, 1250000, 1187500, 1137500, 1087500, 1037500,
	1000000,  987500,  975000,  950000,  925000,  900000,  900000
};

static struct cpufreq_frequency_table exynos4x12_freq_table[] = {
	{CPUFREQ_BOOST_FREQ, L0, 1500 * 1000},
	{0, L1, 1400 * 1000},
	{0, L2, 1300 * 1000},
	{0, L3, 1200 * 1000},
	{0, L4, 1100 * 1000},
	{0, L5, 1000 * 1000},
	{0, L6,  900 * 1000},
	{0, L7,  800 * 1000},
	{0, L8,  700 * 1000},
	{0, L9,  600 * 1000},
	{0, L10, 500 * 1000},
	{0, L11, 400 * 1000},
	{0, L12, 300 * 1000},
	{0, L13, 200 * 1000},
	{0, 0, CPUFREQ_TABLE_END},
};

static struct apll_freq *apll_freq_4x12;

static struct apll_freq apll_freq_4212[] = {
	/*
	 * values:
	 * freq
	 * clock divider for CORE, COREM0, COREM1, PERIPH, ATB, PCLK_DBG, APLL, CORE2
	 * clock divider for COPY, HPM, RESERVED
	 * PLL M, P, S
	 */
	APLL_FREQ(1500, 0, 3, 7, 0, 6, 1, 2, 0, 6, 2, 0, 250, 4, 0),
	APLL_FREQ(1400, 0, 3, 7, 0, 6, 1, 2, 0, 6, 2, 0, 175, 3, 0),
	APLL_FREQ(1300, 0, 3, 7, 0, 5, 1, 2, 0, 5, 2, 0, 325, 6, 0),
	APLL_FREQ(1200, 0, 3, 7, 0, 5, 1, 2, 0, 5, 2, 0, 200, 4, 0),
	APLL_FREQ(1100, 0, 3, 6, 0, 4, 1, 2, 0, 4, 2, 0, 275, 6, 0),
	APLL_FREQ(1000, 0, 2, 5, 0, 4, 1, 1, 0, 4, 2, 0, 125, 3, 0),
	APLL_FREQ(900,  0, 2, 5, 0, 3, 1, 1, 0, 3, 2, 0, 150, 4, 0),
	APLL_FREQ(800,  0, 2, 5, 0, 3, 1, 1, 0, 3, 2, 0, 100, 3, 0),
	APLL_FREQ(700,  0, 2, 4, 0, 3, 1, 1, 0, 3, 2, 0, 175, 3, 1),
	APLL_FREQ(600,  0, 2, 4, 0, 3, 1, 1, 0, 3, 2, 0, 200, 4, 1),
	APLL_FREQ(500,  0, 2, 4, 0, 3, 1, 1, 0, 3, 2, 0, 125, 3, 1),
	APLL_FREQ(400,  0, 2, 4, 0, 3, 1, 1, 0, 3, 2, 0, 100, 3, 1),
	APLL_FREQ(300,  0, 2, 4, 0, 2, 1, 1, 0, 3, 2, 0, 200, 4, 2),
	APLL_FREQ(200,  0, 1, 3, 0, 1, 1, 1, 0, 3, 2, 0, 100, 3, 2),
};

static struct apll_freq apll_freq_4412[] = {
	/*
	 * values:
	 * freq
	 * clock divider for CORE, COREM0, COREM1, PERIPH, ATB, PCLK_DBG, APLL, CORE2
	 * clock divider for COPY, HPM, CORES
	 * PLL M, P, S
	 */
	APLL_FREQ(1500, 0, 3, 7, 0, 6, 1, 2, 0, 6, 0, 7, 250, 4, 0),
	APLL_FREQ(1400, 0, 3, 7, 0, 6, 1, 2, 0, 6, 0, 6, 175, 3, 0),
	APLL_FREQ(1300, 0, 3, 7, 0, 5, 1, 2, 0, 5, 0, 6, 325, 6, 0),
	APLL_FREQ(1200, 0, 3, 7, 0, 5, 1, 2, 0, 5, 0, 5, 200, 4, 0),
	APLL_FREQ(1100, 0, 3, 6, 0, 4, 1, 2, 0, 4, 0, 5, 275, 6, 0),
	APLL_FREQ(1000, 0, 2, 5, 0, 4, 1, 1, 0, 4, 0, 4, 125, 3, 0),
	APLL_FREQ(900,  0, 2, 5, 0, 3, 1, 1, 0, 3, 0, 4, 150, 4, 0),
	APLL_FREQ(800,  0, 2, 5, 0, 3, 1, 1, 0, 3, 0, 3, 100, 3, 0),
	APLL_FREQ(700,  0, 2, 4, 0, 3, 1, 1, 0, 3, 0, 3, 175, 3, 1),
	APLL_FREQ(600,  0, 2, 4, 0, 3, 1, 1, 0, 3, 0, 2, 200, 4, 1),
	APLL_FREQ(500,  0, 2, 4, 0, 3, 1, 1, 0, 3, 0, 2, 125, 3, 1),
	APLL_FREQ(400,  0, 2, 4, 0, 3, 1, 1, 0, 3, 0, 1, 100, 3, 1),
	APLL_FREQ(300,  0, 2, 4, 0, 2, 1, 1, 0, 3, 0, 1, 200, 4, 2),
	APLL_FREQ(200,  0, 1, 3, 0, 1, 1, 1, 0, 3, 0, 0, 100, 3, 2),
};

static void exynos4x12_set_clkdiv(unsigned int div_index)
{
	unsigned int tmp;

	/* Change Divider - CPU0 */

	tmp = apll_freq_4x12[div_index].clk_div_cpu0;

	__raw_writel(tmp, cpufreq->cmu_regs + EXYNOS4_CLKDIV_CPU);

	while (__raw_readl(cpufreq->cmu_regs + EXYNOS4_CLKDIV_STATCPU)
	       & 0x11111111)
		cpu_relax();

	/* Change Divider - CPU1 */
	tmp = apll_freq_4x12[div_index].clk_div_cpu1;

	__raw_writel(tmp, cpufreq->cmu_regs + EXYNOS4_CLKDIV_CPU1);

	do {
		cpu_relax();
		tmp = __raw_readl(cpufreq->cmu_regs + EXYNOS4_CLKDIV_STATCPU1);
	} while (tmp != 0x0);
}

static void exynos4x12_set_apll(unsigned int index)
{
	unsigned int tmp, freq = apll_freq_4x12[index].freq;

	/* MUX_CORE_SEL = MPLL, ARMCLK uses MPLL for lock time */
	clk_set_parent(moutcore, mout_mpll);

	do {
		cpu_relax();
		tmp = (__raw_readl(cpufreq->cmu_regs + EXYNOS4_CLKMUX_STATCPU)
			>> EXYNOS4_CLKSRC_CPU_MUXCORE_SHIFT);
		tmp &= 0x7;
	} while (tmp != 0x2);

	clk_set_rate(mout_apll, freq * 1000);

	/* MUX_CORE_SEL = APLL */
	clk_set_parent(moutcore, mout_apll);

	do {
		cpu_relax();
		tmp = __raw_readl(cpufreq->cmu_regs + EXYNOS4_CLKMUX_STATCPU);
		tmp &= EXYNOS4_CLKMUX_STATCPU_MUXCORE_MASK;
	} while (tmp != (0x1 << EXYNOS4_CLKSRC_CPU_MUXCORE_SHIFT));
}

static void exynos4x12_set_frequency(unsigned int old_index,
				  unsigned int new_index)
{
	if (old_index > new_index) {
		exynos4x12_set_clkdiv(new_index);
		exynos4x12_set_apll(new_index);
	} else if (old_index < new_index) {
		exynos4x12_set_apll(new_index);
		exynos4x12_set_clkdiv(new_index);
	}
}

int exynos4x12_cpufreq_init(struct exynos_dvfs_info *info)
{
	struct device_node *np;
	unsigned long rate;

	/*
	 * HACK: This is a temporary workaround to get access to clock
	 * controller registers directly and remove static mappings and
	 * dependencies on platform headers. It is necessary to enable
	 * Exynos multi-platform support and will be removed together with
	 * this whole driver as soon as Exynos gets migrated to use
	 * cpufreq-cpu0 driver.
	 */
	np = of_find_compatible_node(NULL, NULL, "samsung,exynos4412-clock");
	if (!np) {
		pr_err("%s: failed to find clock controller DT node\n",
			__func__);
		return -ENODEV;
	}

	info->cmu_regs = of_iomap(np, 0);
	if (!info->cmu_regs) {
		pr_err("%s: failed to map CMU registers\n", __func__);
		return -EFAULT;
	}

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

	if (info->type == EXYNOS_SOC_4212)
		apll_freq_4x12 = apll_freq_4212;
	else
		apll_freq_4x12 = apll_freq_4412;

	info->mpll_freq_khz = rate;
	/* 800Mhz */
	info->pll_safe_idx = L7;
	info->cpu_clk = cpu_clk;
	info->volt_table = exynos4x12_volt_table;
	info->freq_table = exynos4x12_freq_table;
	info->set_freq = exynos4x12_set_frequency;

	cpufreq = info;

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
