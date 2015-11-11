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
#include <linux/pm_opp.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include <asm/smp_plat.h>
#include <asm/cpu.h>

/* OPP tolerance in percentage */
#define	OPP_TOLERANCE	4

static struct cpufreq_frequency_table *freq_table;
static atomic_t freq_table_users = ATOMIC_INIT(0);
static struct device *mpu_dev;
static struct regulator *mpu_reg;

static int omap_target(struct cpufreq_policy *policy, unsigned int index)
{
	int r, ret;
	struct dev_pm_opp *opp;
	unsigned long freq, volt = 0, volt_old = 0, tol = 0;
	unsigned int old_freq, new_freq;

	old_freq = policy->cur;
	new_freq = freq_table[index].frequency;

	freq = new_freq * 1000;
	ret = clk_round_rate(policy->clk, freq);
	if (IS_ERR_VALUE(ret)) {
		dev_warn(mpu_dev,
			 "CPUfreq: Cannot find matching frequency for %lu\n",
			 freq);
		return ret;
	}
	freq = ret;

	if (mpu_reg) {
		rcu_read_lock();
		opp = dev_pm_opp_find_freq_ceil(mpu_dev, &freq);
		if (IS_ERR(opp)) {
			rcu_read_unlock();
			dev_err(mpu_dev, "%s: unable to find MPU OPP for %d\n",
				__func__, new_freq);
			return -EINVAL;
		}
		volt = dev_pm_opp_get_voltage(opp);
		rcu_read_unlock();
		tol = volt * OPP_TOLERANCE / 100;
		volt_old = regulator_get_voltage(mpu_reg);
	}

	dev_dbg(mpu_dev, "cpufreq-omap: %u MHz, %ld mV --> %u MHz, %ld mV\n", 
		old_freq / 1000, volt_old ? volt_old / 1000 : -1,
		new_freq / 1000, volt ? volt / 1000 : -1);

	/* scaling up?  scale voltage before frequency */
	if (mpu_reg && (new_freq > old_freq)) {
		r = regulator_set_voltage(mpu_reg, volt - tol, volt + tol);
		if (r < 0) {
			dev_warn(mpu_dev, "%s: unable to scale voltage up.\n",
				 __func__);
			return r;
		}
	}

	ret = clk_set_rate(policy->clk, new_freq * 1000);

	/* scaling down?  scale voltage after frequency */
	if (mpu_reg && (new_freq < old_freq)) {
		r = regulator_set_voltage(mpu_reg, volt - tol, volt + tol);
		if (r < 0) {
			dev_warn(mpu_dev, "%s: unable to scale voltage down.\n",
				 __func__);
			clk_set_rate(policy->clk, old_freq * 1000);
			return r;
		}
	}

	return ret;
}

static inline void freq_table_free(void)
{
	if (atomic_dec_and_test(&freq_table_users))
		dev_pm_opp_free_cpufreq_table(mpu_dev, &freq_table);
}

static int omap_cpu_init(struct cpufreq_policy *policy)
{
	int result;

	policy->clk = clk_get(NULL, "cpufreq_ck");
	if (IS_ERR(policy->clk))
		return PTR_ERR(policy->clk);

	if (!freq_table) {
		result = dev_pm_opp_init_cpufreq_table(mpu_dev, &freq_table);
		if (result) {
			dev_err(mpu_dev,
				"%s: cpu%d: failed creating freq table[%d]\n",
				__func__, policy->cpu, result);
			goto fail;
		}
	}

	atomic_inc_return(&freq_table_users);

	/* FIXME: what's the actual transition time? */
	result = cpufreq_generic_init(policy, freq_table, 300 * 1000);
	if (!result)
		return 0;

	freq_table_free();
fail:
	clk_put(policy->clk);
	return result;
}

static int omap_cpu_exit(struct cpufreq_policy *policy)
{
	freq_table_free();
	clk_put(policy->clk);
	return 0;
}

static struct cpufreq_driver omap_driver = {
	.flags		= CPUFREQ_STICKY | CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= omap_target,
	.get		= cpufreq_generic_get,
	.init		= omap_cpu_init,
	.exit		= omap_cpu_exit,
	.name		= "omap",
	.attr		= cpufreq_generic_attr,
};

static int omap_cpufreq_probe(struct platform_device *pdev)
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

static int omap_cpufreq_remove(struct platform_device *pdev)
{
	return cpufreq_unregister_driver(&omap_driver);
}

static struct platform_driver omap_cpufreq_platdrv = {
	.driver = {
		.name	= "omap-cpufreq",
	},
	.probe		= omap_cpufreq_probe,
	.remove		= omap_cpufreq_remove,
};
module_platform_driver(omap_cpufreq_platdrv);

MODULE_DESCRIPTION("cpufreq driver for OMAP SoCs");
MODULE_LICENSE("GPL");
