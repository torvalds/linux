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
#include <mach/asv-exynos.h>
#include <mach/exynos5_bus.h>

#define CPUFREQ_LEVEL_END	(L15 + 1)

static int max_support_idx;
static int min_support_idx = (CPUFREQ_LEVEL_END - 1);
static struct clk *cpu_clk;
static struct clk *moutcore;
static struct clk *mout_mpll;
static struct clk *mout_apll;
static struct clk *fout_apll;
static struct exynos5_bus_mif_handle *mif_bus_freq;

struct cpufreq_clkdiv {
	unsigned int	index;
	unsigned int	clkdiv;
	unsigned int	clkdiv1;
};

static unsigned int exynos5250_volt_table[CPUFREQ_LEVEL_END];

static struct cpufreq_frequency_table exynos5250_freq_table[] = {
	{L0, 1700 * 1000},
	{L1, 1600 * 1000},
	{L2, 1500 * 1000},
	{L3, 1400 * 1000},
	{L4, 1300 * 1000},
	{L5, 1200 * 1000},
	{L6, 1100 * 1000},
	{L7, 1000 * 1000},
	{L8, 900 * 1000},
	{L9, 800 * 1000},
	{L10, 700 * 1000},
	{L11, 600 * 1000},
	{L12, 500 * 1000},
	{L13, 400 * 1000},
	{L14, 300 * 1000},
	{L15, 200 * 1000},
	{0, CPUFREQ_TABLE_END},
};

/* Minimum memory throughput in megabytes per second */
static int exynos5250_bus_table[CPUFREQ_LEVEL_END] = {
	800000, /* 1.7 GHz */
	800000, /* 1.6 GHz */
	800000, /* 1.5 GHz */
	800000, /* 1.4 GHz */
	667000, /* 1.3 GHz */
	667000, /* 1.2 GHz */
	667000, /* 1.1 GHz */
	400000, /* 1.0 GHz */
	400000, /* 900 MHz */
	400000,  /* 800 MHz */
	160000,  /* 700 MHz */
	160000,  /* 600 MHz */
	160000,  /* 500 MHz */
	0,  /* 400 MHz */
	0,  /* 300 MHz */
	0,    /* 200 MHz */
};

static int __init set_volt_table(void)
{
	unsigned int i;

	max_support_idx = L0;

	for (i = 0; i < CPUFREQ_LEVEL_END; i++) {
		exynos5250_volt_table[i] = asv_get_volt(ID_ARM, exynos5250_freq_table[i].frequency);
		if (exynos5250_volt_table[i] == 0) {
			pr_err("%s: invalid value\n", __func__);
			return -EINVAL;
		}
	}

	return 0;
}

static void exynos5250_set_frequency(unsigned int old_index,
	unsigned int new_index)
{
	/* MUX_CORE_SEL = MPLL, ARMCLK uses MPLL for lock time */
	clk_set_parent(moutcore, mout_mpll);

	if (old_index >= new_index)
		exynos5_bus_mif_update(mif_bus_freq, exynos5250_bus_table[new_index]);

	clk_set_rate(fout_apll,
		exynos5250_freq_table[new_index].frequency * 1000);

	if (old_index < new_index)
		exynos5_bus_mif_update(mif_bus_freq, exynos5250_bus_table[new_index]);

	/* MUX_CORE_SEL = APLL */
	clk_set_parent(moutcore, mout_apll);
}

static bool exynos5250_pms_change(unsigned int old_index,
	unsigned int new_index)
{
	/* Skip the apll change optimization, it won't happen very often */
	return true;
}

int __init exynos5250_cpufreq_init(struct exynos_dvfs_info *info)
{
	unsigned long rate;

	if (set_volt_table())
		return -EINVAL;

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

	fout_apll = clk_get(NULL, "fout_apll");
	if (IS_ERR(fout_apll))
		goto err_fout_apll;

	mif_bus_freq = exynos5_bus_mif_min(0);

	info->mpll_freq_khz = rate;
	/* 1000Mhz */
	info->pm_lock_idx = L7;
	/* 800Mhz */
	info->pll_safe_idx = L9;
	info->max_support_idx = max_support_idx;
	info->min_support_idx = min_support_idx;
	info->cpu_clk = cpu_clk;
	info->volt_table = exynos5250_volt_table;
	info->freq_table = exynos5250_freq_table;
	info->set_freq = exynos5250_set_frequency;
	info->need_apll_change = exynos5250_pms_change;

	return 0;

err_fout_apll:
	clk_put(mout_apll);
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
