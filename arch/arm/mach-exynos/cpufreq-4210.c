/* linux/arch/arm/mach-exynos/cpufreq-4210.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4210 - CPU frequency scaling support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/cpufreq.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/cpufreq.h>
#include <mach/asv.h>
#include <mach/sec_debug.h>

#include <plat/clock.h>

#define CPUFREQ_LEVEL_END	L6

static int max_support_idx;
static int min_support_idx = (CPUFREQ_LEVEL_END - 1);
static struct clk *cpu_clk;
static struct clk *moutcore;
static struct clk *mout_mpll;
static struct clk *mout_apll;

struct cpufreq_clkdiv {
	unsigned int	index;
	unsigned int	clkdiv;
};

static unsigned int exynos4210_volt_table[CPUFREQ_LEVEL_END];

static struct cpufreq_frequency_table exynos4210_freq_table[] = {
	{L0, 1400*1000},
	{L1, 1200*1000},
	{L2, 1000*1000},
	{L3, 800*1000},
	{L4, 500*1000},
	{L5, 200*1000},
	{0, CPUFREQ_TABLE_END},
};

static struct cpufreq_clkdiv exynos4210_clkdiv_table[] = {
	{L0, 0},
	{L1, 0},
	{L2, 0},
	{L3, 0},
	{L4, 0},
	{L5, 0},
};

static unsigned int clkdiv_cpu0[CPUFREQ_LEVEL_END][7] = {
	/*
	 * Clock divider value for following
	 * { DIVCORE, DIVCOREM0, DIVCOREM1, DIVPERIPH,
	 *		DIVATB, DIVPCLK_DBG, DIVAPLL }
	 */
	/* ARM L0: 1400MHz */
	{ 0, 3, 7, 3, 4, 1, 7 },

	/* ARM L1: 1200MHz */
	{ 0, 3, 7, 3, 4, 1, 7 },

	/* ARM L2: 1000MHz */
	{ 0, 3, 7, 3, 4, 1, 7 },

	/* ARM L3: 800MHz */
	{ 0, 3, 7, 3, 3, 1, 7 },

	/* ARM L4: 500MHz */
	{ 0, 3, 7, 3, 3, 1, 7 },

	/* ARM L5: 200MHz */
	{ 0, 1, 3, 1, 3, 1, 0 },
};

static unsigned int clkdiv_cpu1[CPUFREQ_LEVEL_END][2] = {
	/* Clock divider value for following
	 * { DIVCOPY, DIVHPM }
	 */
	/* ARM L0: 1400MHz */
	{ 5, 0 },

	/* ARM L1: 1200MHz */
	{ 5, 0 },

	/* ARM L2: 1000MHz */
	{ 4, 0 },

	/* ARM L3: 800MHz */
	{ 3, 0 },

	/* ARM L4: 500MHz */
	{ 3, 0 },

	/* ARM L5: 200MHz */
	{ 3, 0 },
};

static unsigned int exynos4_apll_pms_table[CPUFREQ_LEVEL_END] = {
	/* APLL FOUT L0: 1400MHz */
	((350<<16)|(6<<8)|(0x1)),

	/* APLL FOUT L1: 1200MHz */
	((150<<16)|(3<<8)|(0x1)),

	/* APLL FOUT L2: 1000MHz */
	((250<<16)|(6<<8)|(0x1)),

	/* APLL FOUT L3: 800MHz */
	((200<<16)|(6<<8)|(0x1)),

	/* APLL FOUT L4: 500MHz */
	((250<<16)|(6<<8)|(0x2)),

	/* APLL FOUT L5: 200MHz */
	((200<<16)|(6<<8)|(0x3)),
};

/*
 * ASV group voltage table
 */

static const unsigned int asv_voltage_A[CPUFREQ_LEVEL_END][8] = {
	/*
	 *	   SS, A1, A2, B1, B2, C1, C2, D
	 * @Dummy:
	 * @1200 :
	 * @1000 :
	 * @800	 :	ASV_VOLTAGE_TABLE
	 * @500  :
	 * @200  :
	 */
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 1350000, 1350000, 1300000, 1275000, 1250000, 1225000, 1200000, 1175000 },
	{ 1300000, 1250000, 1200000, 1175000, 1150000, 1125000, 1100000, 1075000 },
	{ 1200000, 1150000, 1100000, 1075000, 1050000, 1025000, 1000000, 975000 },
	{ 1100000, 1050000, 1000000, 975000, 975000, 950000, 925000, 925000 },
	{ 1050000, 1000000, 975000, 950000, 950000, 925000, 925000, 925000 },

};

static const unsigned int asv_voltage_B[CPUFREQ_LEVEL_END][5] = {
	/*
	 *	   S, A, B, C, D
	 * @1400 :
	 * @1200 :
	 * @1000 :
	 * @800	 :	ASV_VOLTAGE_TABLE
	 * @500	 :
	 * @200	 :
	 */
	{ 1350000, 1350000, 1300000, 1250000, 1225000 },
	{ 1325000, 1275000, 1225000, 1175000, 1150000 },
	{ 1225000, 1175000, 1125000, 1075000, 1050000 },
	{ 1150000, 1100000, 1050000, 1000000, 975000 },
	{ 1050000, 1000000, 950000, 950000, 950000 },
	{ 1025000, 975000, 950000, 950000, 950000 },

};

static void set_clkdiv(unsigned int div_index)
{
	unsigned int tmp;
	/* Change Divider - CPU0 */

	tmp = exynos4210_clkdiv_table[div_index].clkdiv;

	__raw_writel(tmp, EXYNOS4_CLKDIV_CPU);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STATCPU);
	} while (tmp & 0x1111111);

	/* Change Divider - CPU1 */
	tmp = __raw_readl(EXYNOS4_CLKDIV_CPU1);

	tmp &= ~((0x7 << 4) | (0x7));

	tmp |= ((clkdiv_cpu1[div_index][0] << 4) |
		(clkdiv_cpu1[div_index][1] << 0));

	__raw_writel(tmp, EXYNOS4_CLKDIV_CPU1);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STATCPU1);
	} while (tmp & 0x11);
}

static void set_apll(unsigned int new_index,
			     unsigned int old_index)
{
	unsigned int tmp;

	/* 1. MUX_CORE_SEL = MPLL,
	 * ARMCLK uses MPLL for lock time */
	if (clk_set_parent(moutcore, mout_mpll))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				mout_mpll->name, moutcore->name);

	do {
		tmp = (__raw_readl(EXYNOS4_CLKMUX_STATCPU)
			>> EXYNOS4_CLKSRC_CPU_MUXCORE_SHIFT);
		tmp &= 0x7;
	} while (tmp != 0x2);

	/* 2. Set APLL Lock time */
	__raw_writel(EXYNOS4_APLL_LOCKTIME, EXYNOS4_APLL_LOCK);

	/* 3. Change PLL PMS values */
	tmp = __raw_readl(EXYNOS4_APLL_CON0);
	tmp &= ~((0x3ff << 16) | (0x3f << 8) | (0x7 << 0));
	tmp |= exynos4_apll_pms_table[new_index];
	__raw_writel(tmp, EXYNOS4_APLL_CON0);

	/* 4. wait_lock_time */
	do {
		tmp = __raw_readl(EXYNOS4_APLL_CON0);
	} while (!(tmp & (0x1 << EXYNOS4_APLLCON0_LOCKED_SHIFT)));

	/* 5. MUX_CORE_SEL = APLL */
	if (clk_set_parent(moutcore, mout_apll))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
				mout_apll->name, moutcore->name);

	do {
		tmp = __raw_readl(EXYNOS4_CLKMUX_STATCPU);
		tmp &= EXYNOS4_CLKMUX_STATCPU_MUXCORE_MASK;
	} while (tmp != (0x1 << EXYNOS4_CLKSRC_CPU_MUXCORE_SHIFT));

}

bool exynos4210_pms_change(unsigned int old_index, unsigned int new_index)
{
	unsigned int old_pm = (exynos4_apll_pms_table[old_index] >> 8);
	unsigned int new_pm = (exynos4_apll_pms_table[new_index] >> 8);

	return (old_pm == new_pm) ? 0 : 1;
}

static void exynos4210_set_frequency(unsigned int old_index,
				  unsigned int new_index)
{
	unsigned int tmp;

	sec_debug_aux_log(SEC_DEBUG_AUXLOG_CPU_BUS_CLOCK_CHANGE,
			"%s: old_index=%d, new_index=%d(%ps)",
			__func__, old_index, new_index,
			__builtin_return_address(0));

	if (old_index > new_index) {
		if (!exynos4210_pms_change(old_index, new_index)) {
			/* 1. Change the system clock divider values */
			set_clkdiv(new_index);
			/* 2. Change just s value in apll m,p,s value */
			tmp = __raw_readl(EXYNOS4_APLL_CON0);
			tmp &= ~(0x7 << 0);
			tmp |= (exynos4_apll_pms_table[new_index] & 0x7);
			__raw_writel(tmp, EXYNOS4_APLL_CON0);

		} else {
			/* Clock Configuration Procedure */
			/* 1. Change the system clock divider values */
			set_clkdiv(new_index);
			/* 2. Change the apll m,p,s value */
			set_apll(new_index, old_index);
		}
	} else if (old_index < new_index) {
		if (!exynos4210_pms_change(old_index, new_index)) {
			/* 1. Change just s value in apll m,p,s value */
			tmp = __raw_readl(EXYNOS4_APLL_CON0);
			tmp &= ~(0x7 << 0);
			tmp |= (exynos4_apll_pms_table[new_index] & 0x7);
			__raw_writel(tmp, EXYNOS4_APLL_CON0);
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

static void __init set_volt_table(void)
{
	unsigned int asv_group = 0;
	bool for_1400 = false, for_1200 = false, for_1000 = false;
	unsigned int tmp;
	unsigned int i;

	tmp = exynos_result_of_asv;

	asv_group = (tmp & 0xF);

	switch (tmp  & (SUPPORT_FREQ_MASK << SUPPORT_FREQ_SHIFT)) {
	case SUPPORT_1400MHZ:
#if defined(CONFIG_EXYNOS4210_1200MHZ_SUPPORT)
		for_1200 = true;
		max_support_idx = L1;
#else
		for_1400 = true;
		max_support_idx = L0;
#endif
		break;
	case SUPPORT_1200MHZ:
		for_1200 = true;
		max_support_idx = L1;
		break;
	case SUPPORT_1000MHZ:
		for_1000 = true;
		max_support_idx = L2;
		break;
	default:
		for_1000 = true;
		max_support_idx = L2;
		break;
	}

	/*
	 * If ASV group is S, can not support 1.4GHz
	 * Disabling table entry
	 */
	if ((asv_group == 0) || !for_1400)
		exynos4210_freq_table[L0].frequency = CPUFREQ_ENTRY_INVALID;

	if (for_1000)
		exynos4210_freq_table[L1].frequency = CPUFREQ_ENTRY_INVALID;

	printk(KERN_INFO "DVFS : VDD_ARM Voltage table set with %d Group\n", asv_group);

	if (for_1400) {
		for (i = 0 ; i < CPUFREQ_LEVEL_END ; i++) {
				exynos4210_volt_table[i] =
					asv_voltage_B[i][asv_group];
		}
	} else {
		for (i = 0 ; i < CPUFREQ_LEVEL_END ; i++) {
				exynos4210_volt_table[i] =
					asv_voltage_A[i][asv_group];
		}
	}
}

#if defined(CONFIG_REGULATOR_MAX8997)
extern void max8997_set_arm_voltage_table(int *voltage_table, int arr_size);

static void exynos4210_cpufreq_set_pmic_vol_table(void)
{
	int vol_table[CPUFREQ_LEVEL_END];
	int i;

	for (i = 0; i < CPUFREQ_LEVEL_END; i++)
		vol_table[i] = exynos4210_volt_table[i];

	max8997_set_arm_voltage_table(vol_table, CPUFREQ_LEVEL_END);
}
#else
static void exynos4210_cpufreq_set_pmic_vol_table(void)
{
	/*Do Nothing*/
}
#endif /* CONFIG_MACH_U1 */


int exynos4210_cpufreq_init(struct exynos_dvfs_info *info)
{
	int i;
	unsigned int tmp;
	unsigned long rate;

	set_volt_table();
	exynos4210_cpufreq_set_pmic_vol_table();

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

	tmp = __raw_readl(EXYNOS4_CLKDIV_CPU);

	for (i = L0; i <  CPUFREQ_LEVEL_END; i++) {
		tmp &= ~(EXYNOS4_CLKDIV_CPU0_CORE_MASK |
			EXYNOS4_CLKDIV_CPU0_COREM0_MASK |
			EXYNOS4_CLKDIV_CPU0_COREM1_MASK |
			EXYNOS4_CLKDIV_CPU0_PERIPH_MASK |
			EXYNOS4_CLKDIV_CPU0_ATB_MASK |
			EXYNOS4_CLKDIV_CPU0_PCLKDBG_MASK |
			EXYNOS4_CLKDIV_CPU0_APLL_MASK);

		tmp |= ((clkdiv_cpu0[i][0] << EXYNOS4_CLKDIV_CPU0_CORE_SHIFT) |
			(clkdiv_cpu0[i][1] << EXYNOS4_CLKDIV_CPU0_COREM0_SHIFT) |
			(clkdiv_cpu0[i][2] << EXYNOS4_CLKDIV_CPU0_COREM1_SHIFT) |
			(clkdiv_cpu0[i][3] << EXYNOS4_CLKDIV_CPU0_PERIPH_SHIFT) |
			(clkdiv_cpu0[i][4] << EXYNOS4_CLKDIV_CPU0_ATB_SHIFT) |
			(clkdiv_cpu0[i][5] << EXYNOS4_CLKDIV_CPU0_PCLKDBG_SHIFT) |
			(clkdiv_cpu0[i][6] << EXYNOS4_CLKDIV_CPU0_APLL_SHIFT));

		exynos4210_clkdiv_table[i].clkdiv = tmp;
	}

	info->mpll_freq_khz = rate;
	info->pm_lock_idx = L3;
	info->pll_safe_idx = L2;
	info->max_support_idx = max_support_idx;
	info->min_support_idx = min_support_idx;
	info->cpu_clk = cpu_clk;
	info->volt_table = exynos4210_volt_table;
	info->freq_table = exynos4210_freq_table;
	info->set_freq = exynos4210_set_frequency;
	info->need_apll_change = exynos4210_pms_change;

	return 0;

err_mout_apll:
	if (!IS_ERR(mout_mpll))
		clk_put(mout_mpll);
err_mout_mpll:
	if (!IS_ERR(moutcore))
		clk_put(moutcore);
err_moutcore:
	if (!IS_ERR(cpu_clk))
		clk_put(cpu_clk);

	pr_debug("%s: failed initialization\n", __func__);
	return -EINVAL;
}
EXPORT_SYMBOL(exynos4210_cpufreq_init);
