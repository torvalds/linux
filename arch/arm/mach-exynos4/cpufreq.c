/* linux/arch/arm/mach-exynos4/cpufreq.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - CPU frequency scaling support
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
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/cpufreq.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/regs-mem.h>

#include <plat/clock.h>
#include <plat/pm.h>

static struct clk *cpu_clk;
static struct clk *moutcore;
static struct clk *mout_mpll;
static struct clk *mout_apll;

static struct regulator *arm_regulator;
static struct regulator *int_regulator;

static struct cpufreq_freqs freqs;
static unsigned int memtype;

enum exynos4_memory_type {
	DDR2 = 4,
	LPDDR2,
	DDR3,
};

enum cpufreq_level_index {
	L0, L1, L2, L3, CPUFREQ_LEVEL_END,
};

static struct cpufreq_frequency_table exynos4_freq_table[] = {
	{L0, 1000*1000},
	{L1, 800*1000},
	{L2, 400*1000},
	{L3, 100*1000},
	{0, CPUFREQ_TABLE_END},
};

static unsigned int clkdiv_cpu0[CPUFREQ_LEVEL_END][7] = {
	/*
	 * Clock divider value for following
	 * { DIVCORE, DIVCOREM0, DIVCOREM1, DIVPERIPH,
	 *		DIVATB, DIVPCLK_DBG, DIVAPLL }
	 */

	/* ARM L0: 1000MHz */
	{ 0, 3, 7, 3, 3, 0, 1 },

	/* ARM L1: 800MHz */
	{ 0, 3, 7, 3, 3, 0, 1 },

	/* ARM L2: 400MHz */
	{ 0, 1, 3, 1, 3, 0, 1 },

	/* ARM L3: 100MHz */
	{ 0, 0, 1, 0, 3, 1, 1 },
};

static unsigned int clkdiv_cpu1[CPUFREQ_LEVEL_END][2] = {
	/*
	 * Clock divider value for following
	 * { DIVCOPY, DIVHPM }
	 */

	 /* ARM L0: 1000MHz */
	{ 3, 0 },

	/* ARM L1: 800MHz */
	{ 3, 0 },

	/* ARM L2: 400MHz */
	{ 3, 0 },

	/* ARM L3: 100MHz */
	{ 3, 0 },
};

static unsigned int clkdiv_dmc0[CPUFREQ_LEVEL_END][8] = {
	/*
	 * Clock divider value for following
	 * { DIVACP, DIVACP_PCLK, DIVDPHY, DIVDMC, DIVDMCD
	 *		DIVDMCP, DIVCOPY2, DIVCORE_TIMERS }
	 */

	/* DMC L0: 400MHz */
	{ 3, 1, 1, 1, 1, 1, 3, 1 },

	/* DMC L1: 400MHz */
	{ 3, 1, 1, 1, 1, 1, 3, 1 },

	/* DMC L2: 266.7MHz */
	{ 7, 1, 1, 2, 1, 1, 3, 1 },

	/* DMC L3: 200MHz */
	{ 7, 1, 1, 3, 1, 1, 3, 1 },
};

static unsigned int clkdiv_top[CPUFREQ_LEVEL_END][5] = {
	/*
	 * Clock divider value for following
	 * { DIVACLK200, DIVACLK100, DIVACLK160, DIVACLK133, DIVONENAND }
	 */

	/* ACLK200 L0: 200MHz */
	{ 3, 7, 4, 5, 1 },

	/* ACLK200 L1: 200MHz */
	{ 3, 7, 4, 5, 1 },

	/* ACLK200 L2: 160MHz */
	{ 4, 7, 5, 7, 1 },

	/* ACLK200 L3: 133.3MHz */
	{ 5, 7, 7, 7, 1 },
};

static unsigned int clkdiv_lr_bus[CPUFREQ_LEVEL_END][2] = {
	/*
	 * Clock divider value for following
	 * { DIVGDL/R, DIVGPL/R }
	 */

	/* ACLK_GDL/R L0: 200MHz */
	{ 3, 1 },

	/* ACLK_GDL/R L1: 200MHz */
	{ 3, 1 },

	/* ACLK_GDL/R L2: 160MHz */
	{ 4, 1 },

	/* ACLK_GDL/R L3: 133.3MHz */
	{ 5, 1 },
};

struct cpufreq_voltage_table {
	unsigned int	index;		/* any */
	unsigned int	arm_volt;	/* uV */
	unsigned int	int_volt;
};

static struct cpufreq_voltage_table exynos4_volt_table[CPUFREQ_LEVEL_END] = {
	{
		.index		= L0,
		.arm_volt	= 1200000,
		.int_volt	= 1100000,
	}, {
		.index		= L1,
		.arm_volt	= 1100000,
		.int_volt	= 1100000,
	}, {
		.index		= L2,
		.arm_volt	= 1000000,
		.int_volt	= 1000000,
	}, {
		.index		= L3,
		.arm_volt	= 900000,
		.int_volt	= 1000000,
	},
};

static unsigned int exynos4_apll_pms_table[CPUFREQ_LEVEL_END] = {
	/* APLL FOUT L0: 1000MHz */
	((250 << 16) | (6 << 8) | 1),

	/* APLL FOUT L1: 800MHz */
	((200 << 16) | (6 << 8) | 1),

	/* APLL FOUT L2 : 400MHz */
	((200 << 16) | (6 << 8) | 2),

	/* APLL FOUT L3: 100MHz */
	((200 << 16) | (6 << 8) | 4),
};

int exynos4_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, exynos4_freq_table);
}

unsigned int exynos4_getspeed(unsigned int cpu)
{
	return clk_get_rate(cpu_clk) / 1000;
}

void exynos4_set_clkdiv(unsigned int div_index)
{
	unsigned int tmp;

	/* Change Divider - CPU0 */

	tmp = __raw_readl(S5P_CLKDIV_CPU);

	tmp &= ~(S5P_CLKDIV_CPU0_CORE_MASK | S5P_CLKDIV_CPU0_COREM0_MASK |
		S5P_CLKDIV_CPU0_COREM1_MASK | S5P_CLKDIV_CPU0_PERIPH_MASK |
		S5P_CLKDIV_CPU0_ATB_MASK | S5P_CLKDIV_CPU0_PCLKDBG_MASK |
		S5P_CLKDIV_CPU0_APLL_MASK);

	tmp |= ((clkdiv_cpu0[div_index][0] << S5P_CLKDIV_CPU0_CORE_SHIFT) |
		(clkdiv_cpu0[div_index][1] << S5P_CLKDIV_CPU0_COREM0_SHIFT) |
		(clkdiv_cpu0[div_index][2] << S5P_CLKDIV_CPU0_COREM1_SHIFT) |
		(clkdiv_cpu0[div_index][3] << S5P_CLKDIV_CPU0_PERIPH_SHIFT) |
		(clkdiv_cpu0[div_index][4] << S5P_CLKDIV_CPU0_ATB_SHIFT) |
		(clkdiv_cpu0[div_index][5] << S5P_CLKDIV_CPU0_PCLKDBG_SHIFT) |
		(clkdiv_cpu0[div_index][6] << S5P_CLKDIV_CPU0_APLL_SHIFT));

	__raw_writel(tmp, S5P_CLKDIV_CPU);

	do {
		tmp = __raw_readl(S5P_CLKDIV_STATCPU);
	} while (tmp & 0x1111111);

	/* Change Divider - CPU1 */

	tmp = __raw_readl(S5P_CLKDIV_CPU1);

	tmp &= ~((0x7 << 4) | 0x7);

	tmp |= ((clkdiv_cpu1[div_index][0] << 4) |
		(clkdiv_cpu1[div_index][1] << 0));

	__raw_writel(tmp, S5P_CLKDIV_CPU1);

	do {
		tmp = __raw_readl(S5P_CLKDIV_STATCPU1);
	} while (tmp & 0x11);

	/* Change Divider - DMC0 */

	tmp = __raw_readl(S5P_CLKDIV_DMC0);

	tmp &= ~(S5P_CLKDIV_DMC0_ACP_MASK | S5P_CLKDIV_DMC0_ACPPCLK_MASK |
		S5P_CLKDIV_DMC0_DPHY_MASK | S5P_CLKDIV_DMC0_DMC_MASK |
		S5P_CLKDIV_DMC0_DMCD_MASK | S5P_CLKDIV_DMC0_DMCP_MASK |
		S5P_CLKDIV_DMC0_COPY2_MASK | S5P_CLKDIV_DMC0_CORETI_MASK);

	tmp |= ((clkdiv_dmc0[div_index][0] << S5P_CLKDIV_DMC0_ACP_SHIFT) |
		(clkdiv_dmc0[div_index][1] << S5P_CLKDIV_DMC0_ACPPCLK_SHIFT) |
		(clkdiv_dmc0[div_index][2] << S5P_CLKDIV_DMC0_DPHY_SHIFT) |
		(clkdiv_dmc0[div_index][3] << S5P_CLKDIV_DMC0_DMC_SHIFT) |
		(clkdiv_dmc0[div_index][4] << S5P_CLKDIV_DMC0_DMCD_SHIFT) |
		(clkdiv_dmc0[div_index][5] << S5P_CLKDIV_DMC0_DMCP_SHIFT) |
		(clkdiv_dmc0[div_index][6] << S5P_CLKDIV_DMC0_COPY2_SHIFT) |
		(clkdiv_dmc0[div_index][7] << S5P_CLKDIV_DMC0_CORETI_SHIFT));

	__raw_writel(tmp, S5P_CLKDIV_DMC0);

	do {
		tmp = __raw_readl(S5P_CLKDIV_STAT_DMC0);
	} while (tmp & 0x11111111);

	/* Change Divider - TOP */

	tmp = __raw_readl(S5P_CLKDIV_TOP);

	tmp &= ~(S5P_CLKDIV_TOP_ACLK200_MASK | S5P_CLKDIV_TOP_ACLK100_MASK |
		S5P_CLKDIV_TOP_ACLK160_MASK | S5P_CLKDIV_TOP_ACLK133_MASK |
		S5P_CLKDIV_TOP_ONENAND_MASK);

	tmp |= ((clkdiv_top[div_index][0] << S5P_CLKDIV_TOP_ACLK200_SHIFT) |
		(clkdiv_top[div_index][1] << S5P_CLKDIV_TOP_ACLK100_SHIFT) |
		(clkdiv_top[div_index][2] << S5P_CLKDIV_TOP_ACLK160_SHIFT) |
		(clkdiv_top[div_index][3] << S5P_CLKDIV_TOP_ACLK133_SHIFT) |
		(clkdiv_top[div_index][4] << S5P_CLKDIV_TOP_ONENAND_SHIFT));

	__raw_writel(tmp, S5P_CLKDIV_TOP);

	do {
		tmp = __raw_readl(S5P_CLKDIV_STAT_TOP);
	} while (tmp & 0x11111);

	/* Change Divider - LEFTBUS */

	tmp = __raw_readl(S5P_CLKDIV_LEFTBUS);

	tmp &= ~(S5P_CLKDIV_BUS_GDLR_MASK | S5P_CLKDIV_BUS_GPLR_MASK);

	tmp |= ((clkdiv_lr_bus[div_index][0] << S5P_CLKDIV_BUS_GDLR_SHIFT) |
		(clkdiv_lr_bus[div_index][1] << S5P_CLKDIV_BUS_GPLR_SHIFT));

	__raw_writel(tmp, S5P_CLKDIV_LEFTBUS);

	do {
		tmp = __raw_readl(S5P_CLKDIV_STAT_LEFTBUS);
	} while (tmp & 0x11);

	/* Change Divider - RIGHTBUS */

	tmp = __raw_readl(S5P_CLKDIV_RIGHTBUS);

	tmp &= ~(S5P_CLKDIV_BUS_GDLR_MASK | S5P_CLKDIV_BUS_GPLR_MASK);

	tmp |= ((clkdiv_lr_bus[div_index][0] << S5P_CLKDIV_BUS_GDLR_SHIFT) |
		(clkdiv_lr_bus[div_index][1] << S5P_CLKDIV_BUS_GPLR_SHIFT));

	__raw_writel(tmp, S5P_CLKDIV_RIGHTBUS);

	do {
		tmp = __raw_readl(S5P_CLKDIV_STAT_RIGHTBUS);
	} while (tmp & 0x11);
}

static void exynos4_set_apll(unsigned int index)
{
	unsigned int tmp;

	/* 1. MUX_CORE_SEL = MPLL, ARMCLK uses MPLL for lock time */
	clk_set_parent(moutcore, mout_mpll);

	do {
		tmp = (__raw_readl(S5P_CLKMUX_STATCPU)
			>> S5P_CLKSRC_CPU_MUXCORE_SHIFT);
		tmp &= 0x7;
	} while (tmp != 0x2);

	/* 2. Set APLL Lock time */
	__raw_writel(S5P_APLL_LOCKTIME, S5P_APLL_LOCK);

	/* 3. Change PLL PMS values */
	tmp = __raw_readl(S5P_APLL_CON0);
	tmp &= ~((0x3ff << 16) | (0x3f << 8) | (0x7 << 0));
	tmp |= exynos4_apll_pms_table[index];
	__raw_writel(tmp, S5P_APLL_CON0);

	/* 4. wait_lock_time */
	do {
		tmp = __raw_readl(S5P_APLL_CON0);
	} while (!(tmp & (0x1 << S5P_APLLCON0_LOCKED_SHIFT)));

	/* 5. MUX_CORE_SEL = APLL */
	clk_set_parent(moutcore, mout_apll);

	do {
		tmp = __raw_readl(S5P_CLKMUX_STATCPU);
		tmp &= S5P_CLKMUX_STATCPU_MUXCORE_MASK;
	} while (tmp != (0x1 << S5P_CLKSRC_CPU_MUXCORE_SHIFT));
}

static void exynos4_set_frequency(unsigned int old_index, unsigned int new_index)
{
	unsigned int tmp;

	if (old_index > new_index) {
		/* The frequency changing to L0 needs to change apll */
		if (freqs.new == exynos4_freq_table[L0].frequency) {
			/* 1. Change the system clock divider values */
			exynos4_set_clkdiv(new_index);

			/* 2. Change the apll m,p,s value */
			exynos4_set_apll(new_index);
		} else {
			/* 1. Change the system clock divider values */
			exynos4_set_clkdiv(new_index);

			/* 2. Change just s value in apll m,p,s value */
			tmp = __raw_readl(S5P_APLL_CON0);
			tmp &= ~(0x7 << 0);
			tmp |= (exynos4_apll_pms_table[new_index] & 0x7);
			__raw_writel(tmp, S5P_APLL_CON0);
		}
	}

	else if (old_index < new_index) {
		/* The frequency changing from L0 needs to change apll */
		if (freqs.old == exynos4_freq_table[L0].frequency) {
			/* 1. Change the apll m,p,s value */
			exynos4_set_apll(new_index);

			/* 2. Change the system clock divider values */
			exynos4_set_clkdiv(new_index);
		} else {
			/* 1. Change just s value in apll m,p,s value */
			tmp = __raw_readl(S5P_APLL_CON0);
			tmp &= ~(0x7 << 0);
			tmp |= (exynos4_apll_pms_table[new_index] & 0x7);
			__raw_writel(tmp, S5P_APLL_CON0);

			/* 2. Change the system clock divider values */
			exynos4_set_clkdiv(new_index);
		}
	}
}

static int exynos4_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	unsigned int index, old_index;
	unsigned int arm_volt, int_volt;

	freqs.old = exynos4_getspeed(policy->cpu);

	if (cpufreq_frequency_table_target(policy, exynos4_freq_table,
					   freqs.old, relation, &old_index))
		return -EINVAL;

	if (cpufreq_frequency_table_target(policy, exynos4_freq_table,
					   target_freq, relation, &index))
		return -EINVAL;

	freqs.new = exynos4_freq_table[index].frequency;
	freqs.cpu = policy->cpu;

	if (freqs.new == freqs.old)
		return 0;

	/* get the voltage value */
	arm_volt = exynos4_volt_table[index].arm_volt;
	int_volt = exynos4_volt_table[index].int_volt;

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	/* control regulator */
	if (freqs.new > freqs.old) {
		/* Voltage up */
		regulator_set_voltage(arm_regulator, arm_volt, arm_volt);
		regulator_set_voltage(int_regulator, int_volt, int_volt);
	}

	/* Clock Configuration Procedure */
	exynos4_set_frequency(old_index, index);

	/* control regulator */
	if (freqs.new < freqs.old) {
		/* Voltage down */
		regulator_set_voltage(arm_regulator, arm_volt, arm_volt);
		regulator_set_voltage(int_regulator, int_volt, int_volt);
	}

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

#ifdef CONFIG_PM
static int exynos4_cpufreq_suspend(struct cpufreq_policy *policy)
{
	return 0;
}

static int exynos4_cpufreq_resume(struct cpufreq_policy *policy)
{
	return 0;
}
#endif

static int exynos4_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	policy->cur = policy->min = policy->max = exynos4_getspeed(policy->cpu);

	cpufreq_frequency_table_get_attr(exynos4_freq_table, policy->cpu);

	/* set the transition latency value */
	policy->cpuinfo.transition_latency = 100000;

	/*
	 * EXYNOS4 multi-core processors has 2 cores
	 * that the frequency cannot be set independently.
	 * Each cpu is bound to the same speed.
	 * So the affected cpu is all of the cpus.
	 */
	cpumask_setall(policy->cpus);

	return cpufreq_frequency_table_cpuinfo(policy, exynos4_freq_table);
}

static struct cpufreq_driver exynos4_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= exynos4_verify_speed,
	.target		= exynos4_target,
	.get		= exynos4_getspeed,
	.init		= exynos4_cpufreq_cpu_init,
	.name		= "exynos4_cpufreq",
#ifdef CONFIG_PM
	.suspend	= exynos4_cpufreq_suspend,
	.resume		= exynos4_cpufreq_resume,
#endif
};

static int __init exynos4_cpufreq_init(void)
{
	cpu_clk = clk_get(NULL, "armclk");
	if (IS_ERR(cpu_clk))
		return PTR_ERR(cpu_clk);

	moutcore = clk_get(NULL, "moutcore");
	if (IS_ERR(moutcore))
		goto out;

	mout_mpll = clk_get(NULL, "mout_mpll");
	if (IS_ERR(mout_mpll))
		goto out;

	mout_apll = clk_get(NULL, "mout_apll");
	if (IS_ERR(mout_apll))
		goto out;

	arm_regulator = regulator_get(NULL, "vdd_arm");
	if (IS_ERR(arm_regulator)) {
		printk(KERN_ERR "failed to get resource %s\n", "vdd_arm");
		goto out;
	}

	int_regulator = regulator_get(NULL, "vdd_int");
	if (IS_ERR(int_regulator)) {
		printk(KERN_ERR "failed to get resource %s\n", "vdd_int");
		goto out;
	}

	/*
	 * Check DRAM type.
	 * Because DVFS level is different according to DRAM type.
	 */
	memtype = __raw_readl(S5P_VA_DMC0 + S5P_DMC0_MEMCON_OFFSET);
	memtype = (memtype >> S5P_DMC0_MEMTYPE_SHIFT);
	memtype &= S5P_DMC0_MEMTYPE_MASK;

	if ((memtype < DDR2) && (memtype > DDR3)) {
		printk(KERN_ERR "%s: wrong memtype= 0x%x\n", __func__, memtype);
		goto out;
	} else {
		printk(KERN_DEBUG "%s: memtype= 0x%x\n", __func__, memtype);
	}

	return cpufreq_register_driver(&exynos4_driver);

out:
	if (!IS_ERR(cpu_clk))
		clk_put(cpu_clk);

	if (!IS_ERR(moutcore))
		clk_put(moutcore);

	if (!IS_ERR(mout_mpll))
		clk_put(mout_mpll);

	if (!IS_ERR(mout_apll))
		clk_put(mout_apll);

	if (!IS_ERR(arm_regulator))
		regulator_put(arm_regulator);

	if (!IS_ERR(int_regulator))
		regulator_put(int_regulator);

	printk(KERN_ERR "%s: failed initialization\n", __func__);

	return -EINVAL;
}
late_initcall(exynos4_cpufreq_init);
