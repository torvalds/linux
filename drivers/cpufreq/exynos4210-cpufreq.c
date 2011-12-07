/*
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
#include <linux/notifier.h>
#include <linux/suspend.h>

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

static struct cpufreq_freqs freqs;

static unsigned int locking_frequency;
static bool frequency_locked;
static DEFINE_MUTEX(cpufreq_lock);

enum cpufreq_level_index {
	L0, L1, L2, L3, L4, CPUFREQ_LEVEL_END,
};

static struct cpufreq_frequency_table exynos4_freq_table[] = {
	{L0, 1200*1000},
	{L1, 1000*1000},
	{L2, 800*1000},
	{L3, 500*1000},
	{L4, 200*1000},
	{0, CPUFREQ_TABLE_END},
};

static unsigned int clkdiv_cpu0[CPUFREQ_LEVEL_END][7] = {
	/*
	 * Clock divider value for following
	 * { DIVCORE, DIVCOREM0, DIVCOREM1, DIVPERIPH,
	 *		DIVATB, DIVPCLK_DBG, DIVAPLL }
	 */

	/* ARM L0: 1200MHz */
	{ 0, 3, 7, 3, 4, 1, 7 },

	/* ARM L1: 1000MHz */
	{ 0, 3, 7, 3, 4, 1, 7 },

	/* ARM L2: 800MHz */
	{ 0, 3, 7, 3, 3, 1, 7 },

	/* ARM L3: 500MHz */
	{ 0, 3, 7, 3, 3, 1, 7 },

	/* ARM L4: 200MHz */
	{ 0, 1, 3, 1, 3, 1, 0 },
};

static unsigned int clkdiv_cpu1[CPUFREQ_LEVEL_END][2] = {
	/*
	 * Clock divider value for following
	 * { DIVCOPY, DIVHPM }
	 */

	/* ARM L0: 1200MHz */
	{ 5, 0 },

	/* ARM L1: 1000MHz */
	{ 4, 0 },

	/* ARM L2: 800MHz */
	{ 3, 0 },

	/* ARM L3: 500MHz */
	{ 3, 0 },

	/* ARM L4: 200MHz */
	{ 3, 0 },
};

struct cpufreq_voltage_table {
	unsigned int	index;		/* any */
	unsigned int	arm_volt;	/* uV */
};

static struct cpufreq_voltage_table exynos4_volt_table[CPUFREQ_LEVEL_END] = {
	{
		.index		= L0,
		.arm_volt	= 1350000,
	}, {
		.index		= L1,
		.arm_volt	= 1300000,
	}, {
		.index		= L2,
		.arm_volt	= 1200000,
	}, {
		.index		= L3,
		.arm_volt	= 1100000,
	}, {
		.index		= L4,
		.arm_volt	= 1050000,
	},
};

static unsigned int exynos4_apll_pms_table[CPUFREQ_LEVEL_END] = {
	/* APLL FOUT L0: 1200MHz */
	((150 << 16) | (3 << 8) | 1),

	/* APLL FOUT L1: 1000MHz */
	((250 << 16) | (6 << 8) | 1),

	/* APLL FOUT L2: 800MHz */
	((200 << 16) | (6 << 8) | 1),

	/* APLL FOUT L3: 500MHz */
	((250 << 16) | (6 << 8) | 2),

	/* APLL FOUT L4: 200MHz */
	((200 << 16) | (6 << 8) | 3),
};

static int exynos4_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, exynos4_freq_table);
}

static unsigned int exynos4_getspeed(unsigned int cpu)
{
	return clk_get_rate(cpu_clk) / 1000;
}

static void exynos4_set_clkdiv(unsigned int div_index)
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
	unsigned int arm_volt;
	int err = -EINVAL;

	freqs.old = exynos4_getspeed(policy->cpu);

	mutex_lock(&cpufreq_lock);

	if (frequency_locked && target_freq != locking_frequency) {
		err = -EAGAIN;
		goto out;
	}

	if (cpufreq_frequency_table_target(policy, exynos4_freq_table,
					   freqs.old, relation, &old_index))
		goto out;

	if (cpufreq_frequency_table_target(policy, exynos4_freq_table,
					   target_freq, relation, &index))
		goto out;

	err = 0;

	freqs.new = exynos4_freq_table[index].frequency;
	freqs.cpu = policy->cpu;

	if (freqs.new == freqs.old)
		goto out;

	/* get the voltage value */
	arm_volt = exynos4_volt_table[index].arm_volt;

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	/* control regulator */
	if (freqs.new > freqs.old) {
		/* Voltage up */
		regulator_set_voltage(arm_regulator, arm_volt, arm_volt);
	}

	/* Clock Configuration Procedure */
	exynos4_set_frequency(old_index, index);

	/* control regulator */
	if (freqs.new < freqs.old) {
		/* Voltage down */
		regulator_set_voltage(arm_regulator, arm_volt, arm_volt);
	}

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

out:
	mutex_unlock(&cpufreq_lock);
	return err;
}

#ifdef CONFIG_PM
/*
 * These suspend/resume are used as syscore_ops, it is already too
 * late to set regulator voltages at this stage.
 */
static int exynos4_cpufreq_suspend(struct cpufreq_policy *policy)
{
	return 0;
}

static int exynos4_cpufreq_resume(struct cpufreq_policy *policy)
{
	return 0;
}
#endif

/**
 * exynos4_cpufreq_pm_notifier - block CPUFREQ's activities in suspend-resume
 *			context
 * @notifier
 * @pm_event
 * @v
 *
 * While frequency_locked == true, target() ignores every frequency but
 * locking_frequency. The locking_frequency value is the initial frequency,
 * which is set by the bootloader. In order to eliminate possible
 * inconsistency in clock values, we save and restore frequencies during
 * suspend and resume and block CPUFREQ activities. Note that the standard
 * suspend/resume cannot be used as they are too deep (syscore_ops) for
 * regulator actions.
 */
static int exynos4_cpufreq_pm_notifier(struct notifier_block *notifier,
				       unsigned long pm_event, void *v)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(0); /* boot CPU */
	static unsigned int saved_frequency;
	unsigned int temp;

	mutex_lock(&cpufreq_lock);
	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		if (frequency_locked)
			goto out;
		frequency_locked = true;

		if (locking_frequency) {
			saved_frequency = exynos4_getspeed(0);

			mutex_unlock(&cpufreq_lock);
			exynos4_target(policy, locking_frequency,
				       CPUFREQ_RELATION_H);
			mutex_lock(&cpufreq_lock);
		}

		break;
	case PM_POST_SUSPEND:

		if (saved_frequency) {
			/*
			 * While frequency_locked, only locking_frequency
			 * is valid for target(). In order to use
			 * saved_frequency while keeping frequency_locked,
			 * we temporarly overwrite locking_frequency.
			 */
			temp = locking_frequency;
			locking_frequency = saved_frequency;

			mutex_unlock(&cpufreq_lock);
			exynos4_target(policy, locking_frequency,
				       CPUFREQ_RELATION_H);
			mutex_lock(&cpufreq_lock);

			locking_frequency = temp;
		}

		frequency_locked = false;
		break;
	}
out:
	mutex_unlock(&cpufreq_lock);

	return NOTIFY_OK;
}

static struct notifier_block exynos4_cpufreq_nb = {
	.notifier_call = exynos4_cpufreq_pm_notifier,
};

static int exynos4_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	int ret;

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

	ret = cpufreq_frequency_table_cpuinfo(policy, exynos4_freq_table);
	if (ret)
		return ret;

	cpufreq_frequency_table_get_attr(exynos4_freq_table, policy->cpu);

	return 0;
}

static int exynos4_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

static struct freq_attr *exynos4_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver exynos4_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= exynos4_verify_speed,
	.target		= exynos4_target,
	.get		= exynos4_getspeed,
	.init		= exynos4_cpufreq_cpu_init,
	.exit		= exynos4_cpufreq_cpu_exit,
	.name		= "exynos4_cpufreq",
	.attr		= exynos4_cpufreq_attr,
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

	locking_frequency = exynos4_getspeed(0);

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

	register_pm_notifier(&exynos4_cpufreq_nb);

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

	printk(KERN_ERR "%s: failed initialization\n", __func__);

	return -EINVAL;
}
late_initcall(exynos4_cpufreq_init);
