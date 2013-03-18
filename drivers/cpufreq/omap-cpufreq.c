/*
 *  CPU frequency scaling for OMAP using OPP information
 *
 *  Copyright (C) 2005 Nokia Corporation
 *  Written by Tony Lindgren <tony@atomide.com>
 *
 *  Based on cpu-sa1110.c, Copyright (C) 2001 Russell King
 *
 * Copyright (C) 2007-2011 Texas Instruments, Inc.
 * - OMAP3/4 support by Rajendra Nayak, Santosh Shilimkar
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/opp.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <asm/smp_plat.h>
#include <asm/cpu.h>

/* OPP tolerance in percentage */
#define	OPP_TOLERANCE	4

static struct cpufreq_frequency_table *freq_table;
static atomic_t freq_table_users = ATOMIC_INIT(0);
static struct clk *mpu_clk;
static struct device *mpu_dev;
static struct regulator *mpu_reg;

static int omap_verify_speed(struct cpufreq_policy *policy)
{
	if (!freq_table)
		return -EINVAL;
	return cpufreq_frequency_table_verify(policy, freq_table);
}

static unsigned int omap_getspeed(unsigned int cpu)
{
	unsigned long rate;

	if (cpu >= NR_CPUS)
		return 0;

	rate = clk_get_rate(mpu_clk) / 1000;
	return rate;
}

static int omap_target(struct cpufreq_policy *policy,
		       unsigned int target_freq,
		       unsigned int relation)
{
	unsigned int i;
	int r, ret = 0;
	struct cpufreq_freqs freqs;
	struct opp *opp;
	unsigned long freq, volt = 0, volt_old = 0, tol = 0;

	if (!freq_table) {
		dev_err(mpu_dev, "%s: cpu%d: no freq table!\n", __func__,
				policy->cpu);
		return -EINVAL;
	}

	ret = cpufreq_frequency_table_target(policy, freq_table, target_freq,
			relation, &i);
	if (ret) {
		dev_dbg(mpu_dev, "%s: cpu%d: no freq match for %d(ret=%d)\n",
			__func__, policy->cpu, target_freq, ret);
		return ret;
	}
	freqs.new = freq_table[i].frequency;
	if (!freqs.new) {
		dev_err(mpu_dev, "%s: cpu%d: no match for freq %d\n", __func__,
			policy->cpu, target_freq);
		return -EINVAL;
	}

	freqs.old = omap_getspeed(policy->cpu);
	freqs.cpu = policy->cpu;

	if (freqs.old == freqs.new && policy->cur == freqs.new)
		return ret;

	/* notifiers */
	for_each_cpu(i, policy->cpus) {
		freqs.cpu = i;
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	}

	freq = freqs.new * 1000;
	ret = clk_round_rate(mpu_clk, freq);
	if (IS_ERR_VALUE(ret)) {
		dev_warn(mpu_dev,
			 "CPUfreq: Cannot find matching frequency for %lu\n",
			 freq);
		return ret;
	}
	freq = ret;

	if (mpu_reg) {
		rcu_read_lock();
		opp = opp_find_freq_ceil(mpu_dev, &freq);
		if (IS_ERR(opp)) {
			rcu_read_unlock();
			dev_err(mpu_dev, "%s: unable to find MPU OPP for %d\n",
				__func__, freqs.new);
			return -EINVAL;
		}
		volt = opp_get_voltage(opp);
		rcu_read_unlock();
		tol = volt * OPP_TOLERANCE / 100;
		volt_old = regulator_get_voltage(mpu_reg);
	}

	dev_dbg(mpu_dev, "cpufreq-omap: %u MHz, %ld mV --> %u MHz, %ld mV\n", 
		freqs.old / 1000, volt_old ? volt_old / 1000 : -1,
		freqs.new / 1000, volt ? volt / 1000 : -1);

	/* scaling up?  scale voltage before frequency */
	if (mpu_reg && (freqs.new > freqs.old)) {
		r = regulator_set_voltage(mpu_reg, volt - tol, volt + tol);
		if (r < 0) {
			dev_warn(mpu_dev, "%s: unable to scale voltage up.\n",
				 __func__);
			freqs.new = freqs.old;
			goto done;
		}
	}

	ret = clk_set_rate(mpu_clk, freqs.new * 1000);

	/* scaling down?  scale voltage after frequency */
	if (mpu_reg && (freqs.new < freqs.old)) {
		r = regulator_set_voltage(mpu_reg, volt - tol, volt + tol);
		if (r < 0) {
			dev_warn(mpu_dev, "%s: unable to scale voltage down.\n",
				 __func__);
			ret = clk_set_rate(mpu_clk, freqs.old * 1000);
			freqs.new = freqs.old;
			goto done;
		}
	}

	freqs.new = omap_getspeed(policy->cpu);

done:
	/* notifiers */
	for_each_cpu(i, policy->cpus) {
		freqs.cpu = i;
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	}

	return ret;
}

static inline void freq_table_free(void)
{
	if (atomic_dec_and_test(&freq_table_users))
		opp_free_cpufreq_table(mpu_dev, &freq_table);
}

static int __cpuinit omap_cpu_init(struct cpufreq_policy *policy)
{
	int result = 0;

	mpu_clk = clk_get(NULL, "cpufreq_ck");
	if (IS_ERR(mpu_clk))
		return PTR_ERR(mpu_clk);

	if (policy->cpu >= NR_CPUS) {
		result = -EINVAL;
		goto fail_ck;
	}

	policy->cur = policy->min = policy->max = omap_getspeed(policy->cpu);

	if (!freq_table)
		result = opp_init_cpufreq_table(mpu_dev, &freq_table);

	if (result) {
		dev_err(mpu_dev, "%s: cpu%d: failed creating freq table[%d]\n",
				__func__, policy->cpu, result);
		goto fail_ck;
	}

	atomic_inc_return(&freq_table_users);

	result = cpufreq_frequency_table_cpuinfo(policy, freq_table);
	if (result)
		goto fail_table;

	cpufreq_frequency_table_get_attr(freq_table, policy->cpu);

	policy->min = policy->cpuinfo.min_freq;
	policy->max = policy->cpuinfo.max_freq;
	policy->cur = omap_getspeed(policy->cpu);

	/*
	 * On OMAP SMP configuartion, both processors share the voltage
	 * and clock. So both CPUs needs to be scaled together and hence
	 * needs software co-ordination. Use cpufreq affected_cpus
	 * interface to handle this scenario. Additional is_smp() check
	 * is to keep SMP_ON_UP build working.
	 */
	if (is_smp())
		cpumask_setall(policy->cpus);

	/* FIXME: what's the actual transition time? */
	policy->cpuinfo.transition_latency = 300 * 1000;

	return 0;

fail_table:
	freq_table_free();
fail_ck:
	clk_put(mpu_clk);
	return result;
}

static int omap_cpu_exit(struct cpufreq_policy *policy)
{
	freq_table_free();
	clk_put(mpu_clk);
	return 0;
}

static struct freq_attr *omap_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver omap_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= omap_verify_speed,
	.target		= omap_target,
	.get		= omap_getspeed,
	.init		= omap_cpu_init,
	.exit		= omap_cpu_exit,
	.name		= "omap",
	.attr		= omap_cpufreq_attr,
};

static int __init omap_cpufreq_init(void)
{
	mpu_dev = get_cpu_device(0);
	if (!mpu_dev) {
		pr_warning("%s: unable to get the mpu device\n", __func__);
		return -EINVAL;
	}

	mpu_reg = regulator_get(mpu_dev, "vcc");
	if (IS_ERR(mpu_reg)) {
		pr_warning("%s: unable to get MPU regulator\n", __func__);
		mpu_reg = NULL;
	} else {
		/* 
		 * Ensure physical regulator is present.
		 * (e.g. could be dummy regulator.)
		 */
		if (regulator_get_voltage(mpu_reg) < 0) {
			pr_warn("%s: physical regulator not present for MPU\n",
				__func__);
			regulator_put(mpu_reg);
			mpu_reg = NULL;
		}
	}

	return cpufreq_register_driver(&omap_driver);
}

static void __exit omap_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&omap_driver);
}

MODULE_DESCRIPTION("cpufreq driver for OMAP SoCs");
MODULE_LICENSE("GPL");
module_init(omap_cpufreq_init);
module_exit(omap_cpufreq_exit);
