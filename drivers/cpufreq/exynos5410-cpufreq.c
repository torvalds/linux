/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS5410 - CPU frequency scaling support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/pm_qos.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/cpufreq.h>
#include <mach/asv-exynos.h>

#include <plat/clock.h>

#define CPUFREQ_LEVEL_END_CA7	(L11 + 1)
#define CPUFREQ_LEVEL_END_CA15	(L18 + 1)

#undef PRINT_DIV_VAL

#define ENABLE_CLKOUT
#define SUPPORT_APLL_BYPASS

static int max_support_idx_CA7;
static int max_support_idx_CA15;
static int min_support_idx_CA7 = (CPUFREQ_LEVEL_END_CA7 - 1);
static int min_support_idx_CA15 = (CPUFREQ_LEVEL_END_CA15 - 1);

static struct clk *cpu_clk;
static struct clk *kfc_clk;
static struct clk *mout_cpu;
static struct clk *mout_cpu_kfc;
static struct clk *mout_mpll;
static struct clk *mout_apll;
static struct clk *mout_kpll;
static struct clk *fout_apll;
static struct clk *fout_kpll;

struct cpufreq_clkdiv {
	unsigned int	index;
	unsigned int	clkdiv;
	unsigned int	clkdiv1;
};

extern bool get_asv_is_bin2(void);

static unsigned int exynos5410_volt_table_CA7[CPUFREQ_LEVEL_END_CA7];
static unsigned int exynos5410_volt_table_CA15[CPUFREQ_LEVEL_END_CA15];
struct pm_qos_request exynos5_cpu_int_qos;

static struct cpufreq_frequency_table exynos5410_freq_table_CA7[] = {
	{L0, 1300 * 1000},
	{L1, 1200 * 1000},
	{L2, 1100 * 1000},
	{L3, 1000 * 1000},
	{L4,  900 * 1000},
	{L5,  800 * 1000},
	{L6,  700 * 1000},
	{L7,  600 * 1000},
	{L8,  500 * 1000},
	{L9,  400 * 1000},
	{L10, 300 * 1000},
	{L11, 200 * 1000},
	{0, CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table exynos5410_freq_table_CA15[] = {
	{L0,  2000 * 1000},
	{L1,  1900 * 1000},
	{L2,  1800 * 1000},
	{L3,  1700 * 1000},
	{L4,  1600 * 1000},
	{L5,  1500 * 1000},
	{L6,  1400 * 1000},
	{L7,  1300 * 1000},
	{L8,  1200 * 1000},
	{L9,  1100 * 1000},
	{L10, 1000 * 1000},
	{L11,  900 * 1000},
	{L12,  800 * 1000},
	{L13,  700 * 1000},
	{L14,  600 * 1000},
	{L15,  500 * 1000},
	{L16,  400 * 1000},
	{L17,  300 * 1000},
	{L18,  200 * 1000},
	{0, CPUFREQ_TABLE_END},
};

static struct cpufreq_clkdiv exynos5410_clkdiv_table_CA7[CPUFREQ_LEVEL_END_CA7];
static struct cpufreq_clkdiv exynos5410_clkdiv_table_CA15[CPUFREQ_LEVEL_END_CA15];

static unsigned int clkdiv_cpu0_5410_CA7[CPUFREQ_LEVEL_END_CA7][5] = {
	/*
	 * Clock divider value for following
	 * { KFC, ACLK, HPM, PCLK, KPLL }
	 */

	/* ARM L0: 1.3GHz */
	{ 0, 2, 7, 6, 3 },

	/* ARM L1: 1.2GMHz */
	{ 0, 2, 7, 5, 3 },

	/* ARM L2: 1.1GMHz */
	{ 0, 2, 7, 5, 3 },

	/* ARM L3: 1GHz */
	{ 0, 2, 7, 5, 3 },

	/* ARM L4: 900MHz */
	{ 0, 2, 7, 5, 3 },

	/* ARM L5: 800MHz */
	{ 0, 2, 7, 5, 3 },

	/* ARM L6: 700MHz */
	{ 0, 2, 7, 4, 3 },

	/* ARM L7: 600MHz */
	{ 0, 2, 7, 4, 3 },

	/* ARM L8: 500MHz */
	{ 0, 2, 7, 4, 3 },

	/* ARM L9: 400MHz */
	{ 0, 2, 7, 3, 3 },

	/* ARM L10: 300MHz */
	{ 0, 2, 7, 3, 3 },

	/* ARM L11: 200MHz */
	{ 0, 2, 7, 3, 3 },
};

static unsigned int clkdiv_cpu0_5410_CA15[CPUFREQ_LEVEL_END_CA15][7] = {
	/*
	 * Clock divider value for following
	 * { ARM, CPUD, ACP, ATB, PCLK_DBG, APLL, ARM2}
	 */

	/* ARM L0: 2.0GHz */
	{ 0, 2, 0, 7, 7, 3, 0 },

	/* ARM L1: 1.9GHz */
	{ 0, 2, 0, 7, 7, 3, 0 },

	/* ARM L2: 1.8GHz */
	{ 0, 2, 0, 7, 7, 3, 0 },

	/* ARM L3: 1.7GHz */
	{ 0, 2, 0, 7, 7, 3, 0 },

	/* ARM L4: 1.6GHz */
	{ 0, 2, 0, 7, 7, 3, 0 },

	/* ARM L5: 1.5GHz */
	{ 0, 2, 0, 7, 7, 3, 0 },

	/* ARM L6: 1.4GHz */
	{ 0, 2, 0, 7, 7, 3, 0 },

	/* ARM L7: 1.3GHz */
	{ 0, 2, 0, 7, 7, 3, 0 },

	/* ARM L8: 1.2GHz */
	{ 0, 2, 0, 7, 7, 3, 0 },

	/* ARM L9: 1.1GHz */
	{ 0, 2, 0, 7, 7, 3, 0 },

	/* ARM L10: 1000MHz */
	{ 0, 2, 0, 6, 6, 3, 0 },

	/* ARM L11: 900MHz */
	{ 0, 2, 0, 6, 6, 3, 0 },

	/* ARM L12: 800MHz */
	{ 0, 2, 0, 5, 5, 3, 0 },

	/* ARM L13: 700MHz */
	{ 0, 2, 0, 5, 5, 3, 0 },

	/* ARM L14: 600MHz */
	{ 0, 2, 0, 4, 4, 3, 0 },

	/* ARM L15: 500MHz */
	{ 0, 2, 0, 3, 3, 3, 0 },

	/* ARM L16: 400MHz */
	{ 0, 2, 0, 3, 3, 3, 0 },

	/* ARM L17: 300MHz */
	{ 0, 2, 0, 3, 3, 3, 0 },

	/* ARM L18: 200MHz */
	{ 0, 2, 0, 3, 3, 3, 0 },
};

unsigned int clkdiv_cpu1_5410_CA15[CPUFREQ_LEVEL_END_CA15][2] = {
	/*
	 * Clock divider value for following
	 * { copy, HPM }
	 */

	/* ARM L0: 2.0GHz */
	{ 7, 7 },

	/* ARM L1: 1.9GHz */
	{ 7, 7 },

	/* ARM L2: 1.8GHz */
	{ 7, 7 },

	/* ARM L3: 1.7GHz */
	{ 7, 7 },

	/* ARM L4: 1.6GHz */
	{ 7, 7 },

	/* ARM L5: 1.5GHz */
	{ 7, 7 },

	/* ARM L6: 1.4GHz */
	{ 7, 7 },

	/* ARM L7: 1.3GHz */
	{ 7, 7 },

	/* ARM L8: 1.2GHz */
	{ 7, 7 },

	/* ARM L9: 1.1GHz */
	{ 7, 7 },

	/* ARM L10: 1GHz */
	{ 7, 7 },

	/* ARM L11: 900MHz */
	{ 7, 7 },

	/* ARM L12: 800MHz */
	{ 7, 7 },

	/* ARM L13: 700MHz */
	{ 7, 7 },

	/* ARM L14: 600MHz */
	{ 7, 7 },

	/* ARM L15: 500MHz */
	{ 7, 7 },

	/* ARM L16: 400MHz */
	{ 7, 7 },

	/* ARM L17: 300MHz */
	{ 7, 7 },

	/* ARM L18: 200MHz */
	{ 7, 7 },
};

static unsigned int exynos5410_kpll_pms_table_CA7[CPUFREQ_LEVEL_END_CA7] = {
	/* KPLL FOUT L0: 1.3GHz */
	((325 << 16) | (6 << 8) | (0x0)),

	/* KPLL FOUT L1: 1.2GHz */
	((200 << 16) | (2 << 8) | (0x1)),

	/* KPLL FOUT L2: 1.1GHz */
	((275 << 16) | (3 << 8) | (0x1)),

	/* KPLL FOUT L3: 1GHz */
	((250 << 16) | (3 << 8) | (0x1)),

	/* KPLL FOUT L4: 900MHz */
	((150 << 16) | (2 << 8) | (0x1)),

	/* KPLL FOUT L5: 800MHz */
	((200 << 16) | (3 << 8) | (0x1)),

	/* KPLL FOUT L6: 700MHz */
	((175 << 16) | (3 << 8) | (0x1)),

	/* KPLL FOUT L7: 600MHz */
	((200 << 16) | (2 << 8) | (0x2)),

	/* KPLL FOUT L8: 500MHz */
	((250 << 16) | (3 << 8) | (0x2)),

	/* KPLL FOUT L9: 400MHz */
	((200 << 16) | (3 << 8) | (0x2)),

	/* KPLL FOUT L10: 300MHz */
	((200 << 16) | (2 << 8) | (0x3)),

	/* KPLL FOUT L11: 200MHz */
	((200 << 16) | (3 << 8) | (0x3)),
};

static unsigned int exynos5410_apll_pms_table_CA15[CPUFREQ_LEVEL_END_CA15] = {
	/* APLL FOUT L0: 2Hz */
	((250 << 16) | (3 << 8) | (0x0)),

	/* APLL FOUT L1: 1.9GHz */
	((475 << 16) | (6 << 8) | (0x0)),

	/* APLL FOUT L2: 1.8GHz */
	((225 << 16) | (3 << 8) | (0x0)),

	/* APLL FOUT L3: 1.7GHz */
	((425 << 16) | (6 << 8) | (0x0)),

	/* APLL FOUT L4: 1.6GHz */
	((200 << 16) | (3 << 8) | (0x0)),

	/* APLL FOUT L5: 1.5GHz */
	((250 << 16) | (4 << 8) | (0x0)),

	/* APLL FOUT L6: 1.4GHz */
	((175 << 16) | (3 << 8) | (0x0)),

	/* APLL FOUT L7: 1.3GHz */
	((325 << 16) | (6 << 8) | (0x0)),

	/* APLL FOUT L8: 1.2GHz */
	((200 << 16) | (2 << 8) | (0x1)),

	/* APLL FOUT L9: 1.1GHz */
	((275 << 16) | (3 << 8) | (0x1)),

	/* APLL FOUT L10: 1GHz */
	((250 << 16) | (3 << 8) | (0x1)),

	/* APLL FOUT L11: 900MHz */
	((150 << 16) | (2 << 8) | (0x1)),

	/* APLL FOUT L12: 800MHz */
	((200 << 16) | (3 << 8) | (0x1)),

	/* APLL FOUT L13: 700MHz */
	((175 << 16) | (3 << 8) | (0x1)),

	/* APLL FOUT L14: 600MHz */
	((200 << 16) | (2 << 8) | (0x2)),

	/* APLL FOUT L15: 500MHz */
	((250 << 16) | (3 << 8) | (0x2)),

	/* APLL FOUT L16: 400MHz */
	((200 << 16) | (3 << 8) | (0x2)),

	/* APLL FOUT L17: 300MHz */
	((400 << 16) | (4 << 8) | (0x3)),

	/* APLL FOUT L18: 200MHz */
	((200 << 16) | (3 << 8) | (0x3)),
};

/*
 * ASV group voltage table
 */

static const unsigned int asv_voltage_5410_CA7[CPUFREQ_LEVEL_END_CA7] = {
	1225000,	/* LO 1300 */
	1225000,	/* L1 1200 */
	1225000,	/* L2 1100 */
	1225000,	/* L3 1000 */
	1225000,	/* L4  900 */
	1225000,	/* L5  800 */
	1225000,	/* L6  700 */
	1225000,	/* L7  600 */
	1125000,	/* L8  500 */
	 975000,	/* L9  400 */
	 900000,	/* L10 300 */
	 900000,	/* L11 200 */
};

static const unsigned int asv_voltage_5410_CA7_evt1[CPUFREQ_LEVEL_END_CA7] = {
	1387500,	/* L0 1300 */
	1387500,	/* L1 1200 */
	1387500,	/* L2 1100 */
	1387500,	/* L3 1000 */
	1387500,	/* L4  900 */
	1387500,	/* L5  800 */
	1300000,	/* L6  700 */
	1200000,	/* L7  600 */
	1087500,	/* L8  500 */
	1012500,	/* L9  400 */
	 937500,	/* L10 300 */
	 900000,	/* L11 200 */
};

static const unsigned int asv_voltage_5410_CA15[CPUFREQ_LEVEL_END_CA15] = {
	1300000,	/* L0  2000 */
	1300000,	/* L1  1900 */
	1300000,	/* L2  1800 */
	1300000,	/* L3  1700 */
	1300000,	/* L4  1600 */
	1300000,	/* L5  1500 */
	1300000,	/* L6  1400 */
	1300000,	/* L7  1300 */
	1300000,	/* L8  1200 */
	1300000,	/* L9  1100 */
	1225000,	/* L10 1000 */
	1175000,	/* L11  900 */
	1100000,	/* L12  800 */
	1075000,	/* L13  700 */
	1000000,	/* L14  600 */
	 950000,	/* L15  500 */
	 900000,	/* L16  400 */
	 825000,	/* L17  300 */
	 800000,	/* L18  200 */
};

static const unsigned int asv_voltage_5410_CA15_evt1[CPUFREQ_LEVEL_END_CA15] = {
	1300000,	/* L0  2000 */
	1300000,	/* L1  1900 */
	1300000,	/* L2  1800 */
	1300000,	/* L3  1700 */
	1237500,	/* L4  1600 */
	1200000,	/* L5  1500 */
	1150000,	/* L6  1400 */
	1100000,	/* L7  1300 */
	1075000,	/* L8  1200 */
	1037500,	/* L9  1100 */
	1000000,	/* L10 1000 */
	 962500,	/* L11  900 */
	 925000,	/* L12  800 */
	 900000,	/* L13  700 */
	 900000,	/* L14  600 */
	 900000,	/* L15  500 */
	 900000,	/* L16  400 */
	 900000,	/* L17  300 */
	 900000,	/* L18  200 */
};

/*
 * This frequency value is selected as a max dvfs level depends
 * on the number of big cluster's working cpus
 * If one big cpu is working and other cpus are LITTLE, big cpu
 * can go to max_op_freq_b[0] frequency
 */
#ifdef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
static const unsigned int exynos5410_max_op_freq_b_evt1[NR_CPUS + 1] = {
	UINT_MAX,
	1000000,
	1000000,
	1000000,
	1000000,
};

static const unsigned int exynos5410_max_op_freq_b_evt2[NR_CPUS + 1] = {
	UINT_MAX,
	1600000,
	1600000,
	1600000,
	1600000,
};

static const unsigned int exynos5410_max_op_freq_b_bin2[NR_CPUS + 1] = {
	UINT_MAX,
	1400000,
	1400000,
	1400000,
	1400000,
};
#else
static const unsigned int exynos5410_max_op_freq_b_evt1[NR_CPUS + 1] = {
	UINT_MAX,
	1000000,
	1000000,
	1000000,
	1000000,
};

static const unsigned int exynos5410_max_op_freq_b_evt2[NR_CPUS + 1] = {
	UINT_MAX,
	1600000,
	1600000,
	1600000,
	1600000,
};
#endif

static void exynos5410_set_ema_CA15(unsigned int target_volt)
{
	unsigned int tmp;

	tmp = __raw_readl(EXYNOS5410_ARM_EMA_CTRL);

	if ((target_volt <= EXYNOS5410_ARM_EMA_BASE_VOLT) &&
			(tmp & ~EXYNOS5410_ARM_WAS_ENABLE)) {
		tmp |= EXYNOS5410_ARM_WAS_ENABLE;
		__raw_writel(tmp, EXYNOS5410_ARM_EMA_CTRL);
	} else if ((target_volt > EXYNOS5410_ARM_EMA_BASE_VOLT) &&
			(tmp & EXYNOS5410_ARM_WAS_ENABLE)) {
		tmp &= ~EXYNOS5410_ARM_WAS_ENABLE;
		__raw_writel(tmp, EXYNOS5410_ARM_EMA_CTRL);
	}
};

static void exynos5410_set_clkdiv_CA7(unsigned int div_index)
{
	unsigned int tmp;
	unsigned int mask;

	/* Change Divider - KFC0 */

	tmp = exynos5410_clkdiv_table_CA7[div_index].clkdiv;

	__raw_writel(tmp, EXYNOS5_CLKDIV_KFC0);

	mask = (1 << EXYNOS5_CLKDIV_KFC0_CORE_SHIFT) |
		(1 << EXYNOS5_CLKDIV_KFC0_ACLK_SHIFT) |
		(1 << EXYNOS5_CLKDIV_KFC0_HPM_SHIFT) |
		(1 << EXYNOS5_CLKDIV_KFC0_PCLK_SHIFT) |
		(1 << EXYNOS5_CLKDIV_KFC0_KPLL_SHIFT);

	wait_clkdiv_stable_time(EXYNOS5_CLKDIV_STAT_KFC0, mask, 0);

#ifdef PRINT_DIV_VAL
	tmp = __raw_readl(EXYNOS5_CLKDIV_KFC0);
	pr_info("DIV_KFC0[0x%x]\n", tmp);
#endif
}

static void exynos5410_set_clkdiv_CA15(unsigned int div_index)
{
	unsigned int tmp;
	unsigned int mask;

	/* Change Divider - CPU0 */

	tmp = exynos5410_clkdiv_table_CA15[div_index].clkdiv;

	__raw_writel(tmp, EXYNOS5_CLKDIV_CPU0);

	mask = (1 << EXYNOS5_CLKDIV_CPU0_CORE_SHIFT) |
		(1 << EXYNOS5_CLKDIV_CPU0_CPUD_SHIFT) |
		(1 << EXYNOS5_CLKDIV_CPU0_ACP_SHIFT) |
		(1 << EXYNOS5_CLKDIV_CPU0_ATB_SHIFT) |
		(1 << EXYNOS5_CLKDIV_CPU0_PCLKDBG_SHIFT) |
		(1 << EXYNOS5_CLKDIV_CPU0_APLL_SHIFT) |
		(1 << EXYNOS5_CLKDIV_CPU0_CORE2_SHIFT);

	wait_clkdiv_stable_time(EXYNOS5_CLKDIV_STATCPU0, mask, 0);

#ifdef PRINT_DIV_VAL
	tmp = __raw_readl(EXYNOS5_CLKDIV_CPU0);
	pr_info("DIV_CPU0[0x%x]\n", tmp);
#endif

	/* Change Divider - CPU1 */
	tmp = exynos5410_clkdiv_table_CA15[div_index].clkdiv1;

	__raw_writel(tmp, EXYNOS5_CLKDIV_CPU1);

	mask = (1 << EXYNOS5_CLKDIV_CPU1_COPY_SHIFT) |
		(1 << EXYNOS5_CLKDIV_CPU1_HPM_SHIFT);

	wait_clkdiv_stable_time(EXYNOS5_CLKDIV_STATCPU1, mask, 0);

#ifdef PRINT_DIV_VAL
	tmp = __raw_readl(EXYNOS5_CLKDIV_CPU1);
	pr_info("DIV_CPU1[0x%x]\n", tmp);
#endif
}

static void exynos5410_set_kpll_CA7(unsigned int new_index, unsigned int old_index)
{
	unsigned int tmp, pdiv;

	/* 0. before change to MPLL, set div for MPLL output */
	if ((new_index < L5) && (old_index < L5))
		exynos5410_set_clkdiv_CA7(L5); /* pll_safe_index of CA7 */

	/* 1. MUX_CORE_SEL = MPLL, KFCCLK uses MPLL for lock time */
	if (clk_set_parent(mout_cpu_kfc, mout_mpll))
		pr_err("Unable to set parent %s of clock %s.\n",
				mout_mpll->name, mout_cpu_kfc->name);

	do {
		cpu_relax();
		tmp = (__raw_readl(EXYNOS5_CLKMUX_STAT_KFC)
			>> EXYNOS5_CLKSRC_KFC_MUXCORE_SHIFT);
		tmp &= 0x7;
	} while (tmp != 0x2);

	/* 2. Set APLL Lock time */
	pdiv = ((exynos5410_kpll_pms_table_CA7[new_index] >> 8) & 0x3f);

	__raw_writel((pdiv * 250), EXYNOS5_KPLL_LOCK);

	/* 3. Change PLL PMS values */
	tmp = __raw_readl(EXYNOS5_KPLL_CON0);
	tmp &= ~((0x3ff << 16) | (0x3f << 8) | (0x7 << 0));
	tmp |= exynos5410_kpll_pms_table_CA7[new_index];
	__raw_writel(tmp, EXYNOS5_KPLL_CON0);

	/* 4. wait_lock_time */
	do {
		cpu_relax();
		tmp = __raw_readl(EXYNOS5_KPLL_CON0);
	} while (!(tmp & (0x1 << EXYNOS5_KPLLCON0_LOCKED_SHIFT)));

	/* 5. MUX_CORE_SEL = KPLL */
	if (clk_set_parent(mout_cpu_kfc, mout_kpll))
		pr_err("Unable to set parent %s of clock %s.\n",
				mout_kpll->name, mout_cpu_kfc->name);

	do {
		cpu_relax();
		tmp = __raw_readl(EXYNOS5_CLKMUX_STAT_KFC);
		tmp &= EXYNOS5_CLKMUX_STATKFC_MUXCORE_MASK;
	} while (tmp != (0x1 << EXYNOS5_CLKSRC_KFC_MUXCORE_SHIFT));

	/* 6. restore original div value */
	if ((new_index < L5) && (old_index < L5))
		exynos5410_set_clkdiv_CA7(new_index);

}

static void exynos5410_set_apll_CA15(unsigned int new_index, unsigned int old_index)
{
	unsigned int tmp, pdiv;

	/* 1. MUX_CORE_SEL = MPLL, ARMCLK uses MPLL for lock time */
	if (clk_set_parent(mout_cpu, mout_mpll))
		pr_err(KERN_ERR "Unable to set parent %s of clock %s.\n",
			mout_mpll->name, mout_cpu->name);

	do {
		cpu_relax();
		tmp = (__raw_readl(EXYNOS5_CLKMUX_STATCPU)
			>> EXYNOS5_CLKSRC_CPU_MUXCORE_SHIFT);
		tmp &= 0x7;
	} while (tmp != 0x2);

	/* 2. Set APLL Lock time */
	pdiv = ((exynos5410_apll_pms_table_CA15[new_index] >> 8) & 0x3f);

	__raw_writel((pdiv * 250), EXYNOS5_APLL_LOCK);

	/* 3. Change PLL PMS values */
	tmp = __raw_readl(EXYNOS5_APLL_CON0);
	tmp &= ~((0x3ff << 16) | (0x3f << 8) | (0x7 << 0));
	tmp |= exynos5410_apll_pms_table_CA15[new_index];
	 __raw_writel(tmp, EXYNOS5_APLL_CON0);

	/* 4. wait_lock_time */
	do {
		cpu_relax();
		tmp = __raw_readl(EXYNOS5_APLL_CON0);
	} while (!(tmp & (0x1 << EXYNOS5_APLLCON0_LOCKED_SHIFT)));

	/* 5. MUX_CORE_SEL = APLL */
	if (clk_set_parent(mout_cpu, mout_apll))
		pr_err("Unable to set parent %s of clock %s.\n",
				mout_apll->name, mout_cpu->name);

	do {
		cpu_relax();
		tmp = __raw_readl(EXYNOS5_CLKMUX_STATCPU);
		tmp &= EXYNOS5_CLKMUX_STATCPU_MUXCORE_MASK;
	} while (tmp != (0x1 << EXYNOS5_CLKSRC_CPU_MUXCORE_SHIFT));
}

static bool exynos5410_pms_change_CA7(unsigned int old_index,
				      unsigned int new_index)
{
	unsigned int old_pm = (exynos5410_kpll_pms_table_CA7[old_index] >> 8);
	unsigned int new_pm = (exynos5410_kpll_pms_table_CA7[new_index] >> 8);

	return (old_pm == new_pm) ? 0 : 1;
}

static bool exynos5410_pms_change_CA15(unsigned int old_index,
				       unsigned int new_index)
{
	unsigned int old_pm = (exynos5410_apll_pms_table_CA15[old_index] >> 8);
	unsigned int new_pm = (exynos5410_apll_pms_table_CA15[new_index] >> 8);

	return (old_pm == new_pm) ? 0 : 1;
}

static void exynos5410_set_frequency_CA7(unsigned int old_index,
					 unsigned int new_index)
{
	unsigned int tmp;

	if (pm_qos_request_active(&exynos5_cpu_int_qos))
		pm_qos_update_request(&exynos5_cpu_int_qos, 0);

	if (old_index > new_index) {
		if (!exynos5410_pms_change_CA7(old_index, new_index)) {
			/* 1. Change the system clock divider values */
			exynos5410_set_clkdiv_CA7(new_index);
			/* 2. Change just s value in apll m,p,s value */
			tmp = __raw_readl(EXYNOS5_KPLL_CON0);
			tmp &= ~(0x7 << 0);
			tmp |= (exynos5410_kpll_pms_table_CA7[new_index] & 0x7);
			__raw_writel(tmp, EXYNOS5_KPLL_CON0);

		} else {
			/* Clock Configuration Procedure */
			/* 1. Change the system clock divider values */
			exynos5410_set_clkdiv_CA7(new_index);
			/* 2. Change the apll m,p,s value */
			exynos5410_set_kpll_CA7(new_index, old_index);
		}
	} else if (old_index < new_index) {
		if (!exynos5410_pms_change_CA7(old_index, new_index)) {
			/* 1. Change just s value in apll m,p,s value */
			tmp = __raw_readl(EXYNOS5_KPLL_CON0);
			tmp &= ~(0x7 << 0);
			tmp |= (exynos5410_kpll_pms_table_CA7[new_index] & 0x7);
			__raw_writel(tmp, EXYNOS5_KPLL_CON0);
			/* 2. Change the system clock divider values */
			exynos5410_set_clkdiv_CA7(new_index);
		} else {
			/* Clock Configuration Procedure */
			/* 1. Change the apll m,p,s value */
			exynos5410_set_kpll_CA7(new_index, old_index);
			/* 2. Change the system clock divider values */
			exynos5410_set_clkdiv_CA7(new_index);
		}
	}

	clk_set_rate(fout_kpll, exynos5410_freq_table_CA7[new_index].frequency * 1000);
}

static void exynos5410_set_frequency_CA15(unsigned int old_index,
					  unsigned int new_index)
{
	unsigned int tmp;

	if (old_index > new_index) {
		/* Before change clock rate, lock INT clock to garantee INT LVcc */
		if ((old_index > L2) && (new_index <= L2)) {
			if (pm_qos_request_active(&exynos5_cpu_int_qos))
				pm_qos_update_request(&exynos5_cpu_int_qos, 700000);
		} else if ((old_index > L6) && (new_index <= L6)) {
			if (pm_qos_request_active(&exynos5_cpu_int_qos))
				pm_qos_update_request(&exynos5_cpu_int_qos, 160000);
		}

		if (!exynos5410_pms_change_CA15(old_index, new_index)) {
			/* 1. Change the system clock divider values */
			exynos5410_set_clkdiv_CA15(new_index);
			/* 2. Change just s value in apll m,p,s value */
			tmp = __raw_readl(EXYNOS5_APLL_CON0);
			tmp &= ~(0x7 << 0);
			tmp |= (exynos5410_apll_pms_table_CA15[new_index] & 0x7);
			__raw_writel(tmp, EXYNOS5_APLL_CON0);

		} else {
			/* Clock Configuration Procedure */
			/* 1. Change the system clock divider values */
			exynos5410_set_clkdiv_CA15(new_index);
			/* 2. Change the apll m,p,s value */
			exynos5410_set_apll_CA15(new_index, old_index);
		}
	} else if (old_index < new_index) {
		if (!exynos5410_pms_change_CA15(old_index, new_index)) {
			/* 1. Change just s value in apll m,p,s value */
			tmp = __raw_readl(EXYNOS5_APLL_CON0);
			tmp &= ~(0x7 << 0);
			tmp |= (exynos5410_apll_pms_table_CA15[new_index] & 0x7);
			__raw_writel(tmp, EXYNOS5_APLL_CON0);
			/* 2. Change the system clock divider values */
			exynos5410_set_clkdiv_CA15(new_index);
		} else {
			/* Clock Configuration Procedure */
			/* 1. Change the apll m,p,s value */
			exynos5410_set_apll_CA15(new_index, old_index);
			/* 2. Change the system clock divider values */
			exynos5410_set_clkdiv_CA15(new_index);
		}

		if ((old_index <= L2) && (new_index > L2)) {
			if (pm_qos_request_active(&exynos5_cpu_int_qos))
				pm_qos_update_request(&exynos5_cpu_int_qos, 0);
		}
	}

	clk_set_rate(fout_apll, exynos5410_freq_table_CA15[new_index].frequency * 1000);
}

static void __init set_volt_table_CA7(void)
{
	unsigned int i;
	unsigned int asv_volt __maybe_unused;

	for (i = 0; i < CPUFREQ_LEVEL_END_CA7; i++) {
		/* FIXME: need to update voltage table for REV1 */
		if (samsung_rev() < EXYNOS5410_REV_1_0) {
			exynos5410_volt_table_CA7[i] = asv_voltage_5410_CA7[i];
		} else {
			asv_volt = get_match_volt(ID_KFC, exynos5410_freq_table_CA7[i].frequency);

			if (!asv_volt)
				exynos5410_volt_table_CA7[i] = asv_voltage_5410_CA7_evt1[i];
			else
				exynos5410_volt_table_CA7[i] = asv_volt;
		}

		pr_info("CPUFREQ of CA7  L%d : %d uV\n", i,
				exynos5410_volt_table_CA7[i]);
	}

	max_support_idx_CA7 = L1;

	exynos5410_freq_table_CA7[L0].frequency = CPUFREQ_ENTRY_INVALID;
	if (samsung_rev() < EXYNOS5410_REV_2_0) {
		exynos5410_freq_table_CA7[L1].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA7[L2].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA7[L3].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA7[L4].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA7[L5].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA7[L6].frequency = CPUFREQ_ENTRY_INVALID;

		max_support_idx_CA7 = L7;
	}

	if (get_asv_is_bin2()) {
		exynos5410_freq_table_CA7[L1].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA7[L2].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA7[L3].frequency = CPUFREQ_ENTRY_INVALID;
		max_support_idx_CA7 = L4;
	}

	exynos5410_freq_table_CA7[L9].frequency = CPUFREQ_ENTRY_INVALID;
	exynos5410_freq_table_CA7[L10].frequency = CPUFREQ_ENTRY_INVALID;
	exynos5410_freq_table_CA7[L11].frequency = CPUFREQ_ENTRY_INVALID;

	min_support_idx_CA7 = L8;
}

static void __init set_volt_table_CA15(void)
{
	unsigned int i;
	unsigned int asv_volt __maybe_unused;

	for (i = 0; i < CPUFREQ_LEVEL_END_CA15; i++) {
		if (samsung_rev() < EXYNOS5410_REV_1_0) {
			exynos5410_volt_table_CA15[i] = asv_voltage_5410_CA15[i];
		} else {
			asv_volt = get_match_volt(ID_ARM, exynos5410_freq_table_CA15[i].frequency);

			if (!asv_volt)
				exynos5410_volt_table_CA15[i] = asv_voltage_5410_CA15_evt1[i];
			else
				exynos5410_volt_table_CA15[i] = asv_volt;
		}

		pr_info("CPUFREQ of CA15 L%d : %d uV\n", i,
				exynos5410_volt_table_CA15[i]);
	}

	max_support_idx_CA15 = L4;

	exynos5410_freq_table_CA15[L0].frequency = CPUFREQ_ENTRY_INVALID;
	exynos5410_freq_table_CA15[L1].frequency = CPUFREQ_ENTRY_INVALID;
	exynos5410_freq_table_CA15[L2].frequency = CPUFREQ_ENTRY_INVALID;
	exynos5410_freq_table_CA15[L3].frequency = CPUFREQ_ENTRY_INVALID;
	if (samsung_rev() < EXYNOS5410_REV_2_0) {
		exynos5410_freq_table_CA15[L4].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA15[L5].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA15[L6].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA15[L7].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA15[L8].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA15[L9].frequency = CPUFREQ_ENTRY_INVALID;

		max_support_idx_CA15 = L10;
	}

	if (get_asv_is_bin2()) {
		exynos5410_freq_table_CA15[L2].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA15[L3].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA15[L4].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA15[L5].frequency = CPUFREQ_ENTRY_INVALID;

		max_support_idx_CA15 = L6;
	}

	if (samsung_rev() < EXYNOS5410_REV_2_0) {
		exynos5410_freq_table_CA15[L17].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA15[L18].frequency = CPUFREQ_ENTRY_INVALID;

		min_support_idx_CA15 = L16;
	} else {
		exynos5410_freq_table_CA15[L13].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA15[L14].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA15[L15].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA15[L16].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA15[L17].frequency = CPUFREQ_ENTRY_INVALID;
		exynos5410_freq_table_CA15[L18].frequency = CPUFREQ_ENTRY_INVALID;

		min_support_idx_CA15 = L12;
	}
}

int __init exynos5410_cpufreq_CA7_init(struct exynos_dvfs_info *info)
{
	int i;
	unsigned int tmp;
	unsigned long rate;

	set_volt_table_CA7();

	kfc_clk = clk_get(NULL, "kfcclk");
	if (IS_ERR(cpu_clk))
		return PTR_ERR(cpu_clk);

	mout_cpu_kfc = clk_get(NULL, "mout_cpu_kfc");
	if (IS_ERR(mout_cpu))
		goto err_mout_cpu;

	mout_mpll = clk_get(NULL, "mout_mpll");
	if (IS_ERR(mout_mpll))
		goto err_mout_mpll;

	rate = clk_get_rate(mout_mpll) / 1000;

	mout_kpll = clk_get(NULL, "mout_kpll");
	if (IS_ERR(mout_kpll))
		goto err_mout_kpll;

	fout_kpll = clk_get(NULL, "fout_kpll");
	if (IS_ERR(fout_kpll))
		goto err_fout_kpll;

	for (i = L0; i < CPUFREQ_LEVEL_END_CA7; i++) {
		exynos5410_clkdiv_table_CA7[i].index = i;

		tmp = __raw_readl(EXYNOS5_CLKDIV_KFC0);

		tmp &= ~(EXYNOS5_CLKDIV_KFC0_CORE_MASK |
			EXYNOS5_CLKDIV_KFC0_ACLK_MASK |
			EXYNOS5_CLKDIV_KFC0_HPM_MASK |
			EXYNOS5_CLKDIV_KFC0_PCLK_MASK |
			EXYNOS5_CLKDIV_KFC0_KPLL_MASK);

		tmp |= ((clkdiv_cpu0_5410_CA7[i][0] << EXYNOS5_CLKDIV_KFC0_CORE_SHIFT) |
			(clkdiv_cpu0_5410_CA7[i][1] << EXYNOS5_CLKDIV_KFC0_ACLK_SHIFT) |
			(clkdiv_cpu0_5410_CA7[i][2] << EXYNOS5_CLKDIV_KFC0_HPM_SHIFT) |
			(clkdiv_cpu0_5410_CA7[i][3] << EXYNOS5_CLKDIV_KFC0_PCLK_SHIFT)|
			(clkdiv_cpu0_5410_CA7[i][4] << EXYNOS5_CLKDIV_KFC0_KPLL_SHIFT));

		exynos5410_clkdiv_table_CA7[i].clkdiv = tmp;
	}

	info->mpll_freq_khz = rate;
	info->pm_lock_idx = L0;
	info->pll_safe_idx = L5;
	info->max_support_idx = max_support_idx_CA7;
	info->min_support_idx = min_support_idx_CA7;
	info->cpu_clk = kfc_clk;

	if (samsung_rev() < EXYNOS5410_REV_2_0)
		info->max_op_freqs = exynos5410_max_op_freq_b_evt1;
	else
		info->max_op_freqs = exynos5410_max_op_freq_b_evt2;

	info->volt_table = exynos5410_volt_table_CA7;
	info->freq_table = exynos5410_freq_table_CA7;
	info->set_freq = exynos5410_set_frequency_CA7;
	info->need_apll_change = exynos5410_pms_change_CA7;

#ifdef ENABLE_CLKOUT
	tmp = __raw_readl(EXYNOS5_CLKOUT_CMU_KFC);
	tmp &= ~0xffff;
	tmp |= 0x1904;
	__raw_writel(tmp, EXYNOS5_CLKOUT_CMU_KFC);
#endif

	return 0;

err_fout_kpll:
	clk_put(fout_kpll);
err_mout_kpll:
	clk_put(mout_mpll);
err_mout_mpll:
	clk_put(mout_cpu);
err_mout_cpu:
	clk_put(cpu_clk);

	pr_debug("%s: failed initialization\n", __func__);
	return -EINVAL;
}

int __init exynos5410_cpufreq_CA15_init(struct exynos_dvfs_info *info)
{
	int i;
	unsigned int tmp;
	unsigned long rate;

	set_volt_table_CA15();

	cpu_clk = clk_get(NULL, "armclk");
	if (IS_ERR(cpu_clk))
		return PTR_ERR(cpu_clk);

	mout_cpu = clk_get(NULL, "mout_cpu");
	if (IS_ERR(mout_cpu))
		goto err_mout_cpu;

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

	for (i = L0; i < CPUFREQ_LEVEL_END_CA15; i++) {
		exynos5410_clkdiv_table_CA15[i].index = i;

		tmp = __raw_readl(EXYNOS5_CLKDIV_CPU0);

		tmp &= ~(EXYNOS5_CLKDIV_CPU0_CORE_MASK |
			EXYNOS5_CLKDIV_CPU0_CPUD_MASK |
			EXYNOS5_CLKDIV_CPU0_ACP_MASK |
			EXYNOS5_CLKDIV_CPU0_ATB_MASK |
			EXYNOS5_CLKDIV_CPU0_PCLKDBG_MASK |
			EXYNOS5_CLKDIV_CPU0_APLL_MASK |
			EXYNOS5_CLKDIV_CPU0_CORE2_MASK);

		tmp |= ((clkdiv_cpu0_5410_CA15[i][0] << EXYNOS5_CLKDIV_CPU0_CORE_SHIFT) |
			(clkdiv_cpu0_5410_CA15[i][1] << EXYNOS5_CLKDIV_CPU0_CPUD_SHIFT) |
			(clkdiv_cpu0_5410_CA15[i][2] << EXYNOS5_CLKDIV_CPU0_ACP_SHIFT) |
			(clkdiv_cpu0_5410_CA15[i][3] << EXYNOS5_CLKDIV_CPU0_ATB_SHIFT) |
			(clkdiv_cpu0_5410_CA15[i][4] << EXYNOS5_CLKDIV_CPU0_PCLKDBG_SHIFT) |
			(clkdiv_cpu0_5410_CA15[i][5] << EXYNOS5_CLKDIV_CPU0_APLL_SHIFT) |
			(clkdiv_cpu0_5410_CA15[i][6] << EXYNOS5_CLKDIV_CPU0_CORE2_SHIFT));

		exynos5410_clkdiv_table_CA15[i].clkdiv = tmp;

		tmp = __raw_readl(EXYNOS5_CLKDIV_CPU1);

		tmp &= ~(EXYNOS5_CLKDIV_CPU1_COPY_MASK |
			EXYNOS5_CLKDIV_CPU1_HPM_MASK);
		tmp |= ((clkdiv_cpu1_5410_CA15[i][0] << EXYNOS5_CLKDIV_CPU1_COPY_SHIFT) |
			(clkdiv_cpu1_5410_CA15[i][1] << EXYNOS5_CLKDIV_CPU1_HPM_SHIFT));

		exynos5410_clkdiv_table_CA15[i].clkdiv1 = tmp;
	}

	info->mpll_freq_khz = rate;
	info->pm_lock_idx = L0;
	info->pll_safe_idx = L12;
	info->max_support_idx = max_support_idx_CA15;
	info->min_support_idx = min_support_idx_CA15;
#ifdef SUPPORT_APLL_BYPASS
	info->cpu_clk = fout_apll;
	pr_info("fout_apll[%lu]\n", clk_get_rate(fout_apll));
#else
	info->cpu_clk = cpu_clk;
#endif
	if (samsung_rev() < EXYNOS5410_REV_2_0)
		info->max_op_freqs = exynos5410_max_op_freq_b_evt1;
	else
		info->max_op_freqs = exynos5410_max_op_freq_b_evt2;

	if(get_asv_is_bin2()) {
		info->max_op_freqs = exynos5410_max_op_freq_b_bin2;
	}

	info->volt_table = exynos5410_volt_table_CA15;
	info->freq_table = exynos5410_freq_table_CA15;
	info->set_freq = exynos5410_set_frequency_CA15;
	info->set_ema = exynos5410_set_ema_CA15;
	info->need_apll_change = exynos5410_pms_change_CA15;

#ifdef ENABLE_CLKOUT
	tmp = __raw_readl(EXYNOS5_CLKOUT_CMU_CPU);
	tmp &= ~0xffff;
	tmp |= 0x1904;
	__raw_writel(tmp, EXYNOS5_CLKOUT_CMU_CPU);
#endif

	return 0;

err_fout_apll:
	clk_put(fout_apll);
err_mout_apll:
	clk_put(mout_mpll);
err_mout_mpll:
	clk_put(mout_cpu);
err_mout_cpu:
	clk_put(cpu_clk);

	pr_debug("%s: failed initialization\n", __func__);
	return -EINVAL;
}
