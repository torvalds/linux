/*
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - CPU frequency scaling support for EXYNOS series
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
#include <linux/regulator/consumer.h>
#include <linux/cpufreq.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/pm_qos.h>

#include <mach/cpufreq.h>

#include <plat/cpu.h>

#ifdef CONFIG_SMP
struct lpj_info {
	unsigned long   ref;
	unsigned int    freq;
};

static struct lpj_info global_lpj_ref;
#endif

/* Use boot_freq when entering sleep mode */
static unsigned int boot_freq;
static unsigned int max_freq;
static unsigned int max_thermal_freq;
static unsigned int curr_target_freq;

static struct exynos_dvfs_info *exynos_info;

static struct regulator *arm_regulator;
static struct cpufreq_freqs freqs;

static DEFINE_MUTEX(cpufreq_lock);

static bool exynos_cpufreq_disable;
static bool exynos_cpufreq_init_done;

int exynos_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy,
					      exynos_info->freq_table);
}

unsigned int exynos_getspeed(unsigned int cpu)
{
	return clk_get_rate(exynos_info->cpu_clk) / 1000;
}

static unsigned int exynos_get_safe_armvolt(unsigned int old_index, unsigned int new_index)
{
	unsigned int safe_arm_volt = 0;
	struct cpufreq_frequency_table *freq_table = exynos_info->freq_table;
	unsigned int *volt_table = exynos_info->volt_table;

	/*
	 * ARM clock source will be changed APLL to MPLL temporary
	 * To support this level, need to control regulator for
	 * reguired voltage level
	 */

	if (exynos_info->need_apll_change != NULL) {
		if (exynos_info->need_apll_change(old_index, new_index) &&
			(freq_table[new_index].frequency < exynos_info->mpll_freq_khz) &&
			(freq_table[old_index].frequency < exynos_info->mpll_freq_khz)) {
				safe_arm_volt = volt_table[exynos_info->pll_safe_idx];
			}

	}

	return safe_arm_volt;
}

static int exynos_frequency_table_target(struct cpufreq_policy *policy,
		struct cpufreq_frequency_table *table,
		unsigned int target_freq,
		unsigned int relation,
		unsigned int *index)
{
	unsigned int i;

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;

		if (target_freq == freq) {
			*index = i;
			break;
		}
	}

	if (table[i].frequency == CPUFREQ_TABLE_END)
		return -EINVAL;

	return 0;
}

static int exynos_cpufreq_get_index(unsigned int freq)
{
	int i;

	for (i = 0; i <= exynos_info->min_support_idx; i++)
		if (exynos_info->freq_table[i].frequency <= freq)
			return i;

	return -EINVAL;
}

static int exynos_cpufreq_scale(unsigned int target_freq, unsigned int curr_freq)
{
	unsigned int *volt_table = exynos_info->volt_table;
	int new_index, old_index;
	unsigned int arm_volt, safe_arm_volt = 0;
	unsigned int max_volt;
	int ret = 0;

	target_freq = max((unsigned int)pm_qos_request(PM_QOS_CPU_FREQ_MIN), target_freq);
	freqs.new = target_freq;

	if (curr_freq == freqs.new)
		goto out;

	old_index = exynos_cpufreq_get_index(curr_freq);
	if (old_index < 0) {
		ret = old_index;
		goto out;
	}

	new_index = exynos_cpufreq_get_index(freqs.new);
	if (new_index < 0) {
		ret = new_index;
		goto out;
	}

	/*
	 * ARM clock source will be changed APLL to MPLL temporary
	 * To support this level, need to control regulator for
	 * required voltage level
	 */
	safe_arm_volt = exynos_get_safe_armvolt(old_index, new_index);

	arm_volt = volt_table[new_index];
	max_volt = volt_table[0];

	/* When the new frequency is higher than current frequency */
	if ((freqs.new > freqs.old) && !safe_arm_volt) {
		/* Firstly, voltage up to increase frequency */
		ret = regulator_set_voltage(arm_regulator, arm_volt, max_volt);
		if (ret) {
			pr_err("%s: failed to set cpu voltage to %d\n",
				__func__, arm_volt);
			return ret;
		}
	}

	if (safe_arm_volt) {
		ret = regulator_set_voltage(arm_regulator, safe_arm_volt,
				max_volt);
		if (ret) {
			pr_err("%s: failed to set cpu voltage to %d\n",
				__func__, safe_arm_volt);
			return ret;
		}
	}

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	exynos_info->set_freq(old_index, new_index);

#ifdef CONFIG_SMP
	if (!global_lpj_ref.freq) {
		global_lpj_ref.ref = loops_per_jiffy;
		global_lpj_ref.freq = freqs.old;
	}
	loops_per_jiffy = cpufreq_scale(global_lpj_ref.ref, global_lpj_ref.freq,
			freqs.new);
#endif

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	/* When the new frequency is lower than current frequency */
	if ((freqs.new < freqs.old) ||
	   ((freqs.new > freqs.old) && safe_arm_volt)) {
		/* down the voltage after frequency change */
		ret = regulator_set_voltage(arm_regulator, arm_volt, max_volt);
		if (ret)
			pr_err("%s: failed to set cpu voltage to %d\n",
				__func__, arm_volt);
	}

out:
	return ret;
}


static int exynos_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	struct cpufreq_frequency_table *freq_table = exynos_info->freq_table;
	unsigned int index;
	unsigned int new_freq;
	int ret = 0;

	mutex_lock(&cpufreq_lock);

	if (exynos_cpufreq_disable)
		goto out;

	freqs.old = policy->cur;

	curr_target_freq = target_freq;

	/*
	 * If the new frequency is more than the thermal max allowed
	 * frequency, use max_thermal_freq as a new frequency.
	 */
	if (target_freq > max_thermal_freq)
		target_freq = max_thermal_freq;

	if (cpufreq_frequency_table_target(policy, freq_table,
					   target_freq, relation, &index)) {
		ret = -EINVAL;
		goto out;
	}

	new_freq = freq_table[index].frequency;

	exynos_cpufreq_scale(new_freq, freqs.old);

out:
	mutex_unlock(&cpufreq_lock);

	return ret;
}

static unsigned int exynos_thermal_lower_speed(void)
{
	unsigned int max = 0;
	unsigned int curr;
	int i;
	struct cpufreq_frequency_table *freq_table = exynos_info->freq_table;

	curr = max_thermal_freq;

	for (i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		if (freq_table[i].frequency != CPUFREQ_ENTRY_INVALID &&
				freq_table[i].frequency < curr) {
			max = freq_table[i].frequency;
			break;
		}
	}

	if (!max)
		return curr;

	return max;
}

void exynos_thermal_throttle(void)
{
	unsigned int cur;

	if (!exynos_cpufreq_init_done) {
		pr_warn_once("%s: Thermal throttle prior to CPUFREQ ready\n",
				__func__);
		return;
	}

	mutex_lock(&cpufreq_lock);

	max_thermal_freq = exynos_thermal_lower_speed();

	pr_debug("%s: temperature too high, cpu throttle at max %u\n",
			__func__, max_thermal_freq);

	if (!exynos_cpufreq_disable) {
		cur = exynos_getspeed(0);
		if (cur > max_thermal_freq) {
			freqs.old = cur;
			exynos_cpufreq_scale(max_thermal_freq, freqs.old);
		}
	}

	mutex_unlock(&cpufreq_lock);
}

void exynos_thermal_unthrottle(void)
{

	if (!exynos_cpufreq_init_done)
		return;

	mutex_lock(&cpufreq_lock);

	if (max_thermal_freq == max_freq) {
		pr_warn("%s: not throttling\n", __func__);
		goto out;
	}

	max_thermal_freq = max_freq;

	pr_debug("%s: temperature reduced, ending cpu throttling\n", __func__);

	if (!exynos_cpufreq_disable) {
		freqs.old = exynos_getspeed(0);
		exynos_cpufreq_scale(curr_target_freq, freqs.old);
	}

out:
	mutex_unlock(&cpufreq_lock);
}

#ifdef CONFIG_PM
static int exynos_cpufreq_suspend(struct cpufreq_policy *policy)
{
	return 0;
}

static int exynos_cpufreq_resume(struct cpufreq_policy *policy)
{
	return 0;
}
#endif

/**
 * exynos_cpufreq_pm_notifier - block CPUFREQ's activities in suspend-resume
 *			context
 * @notifier
 * @pm_event
 * @v
 *
 * While cpufreq_disable == true, target() ignores every frequency but
 * boot_freq. The boot_freq value is the initial frequency,
 * which is set by the bootloader. In order to eliminate possible
 * inconsistency in clock values, we save and restore frequencies during
 * suspend and resume and block CPUFREQ activities. Note that the standard
 * suspend/resume cannot be used as they are too deep (syscore_ops) for
 * regulator actions.
 */
static int exynos_cpufreq_pm_notifier(struct notifier_block *notifier,
				       unsigned long pm_event, void *v)
{
	int ret;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		mutex_lock(&cpufreq_lock);
		exynos_cpufreq_disable = true;
		mutex_unlock(&cpufreq_lock);

		freqs.old = exynos_getspeed(0);
		ret = exynos_cpufreq_scale(boot_freq, freqs.old);

		if (ret < 0)
			return NOTIFY_BAD;

		pr_debug("PM_SUSPEND_PREPARE for CPUFREQ\n");
		break;
	case PM_POST_SUSPEND:
		pr_debug("PM_POST_SUSPEND for CPUFREQ\n");

		mutex_lock(&cpufreq_lock);
		exynos_cpufreq_disable = false;
		mutex_unlock(&cpufreq_lock);

		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block exynos_cpufreq_nb = {
	.notifier_call = exynos_cpufreq_pm_notifier,
};

static int exynos_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	policy->cur = policy->min = policy->max = exynos_getspeed(policy->cpu);

	cpufreq_frequency_table_get_attr(exynos_info->freq_table, policy->cpu);

	/* set the transition latency value */
	policy->cpuinfo.transition_latency = 100000;

	/*
	 * EXYNOS4 multi-core processors has 2 cores
	 * that the frequency cannot be set independently.
	 * Each cpu is bound to the same speed.
	 * So the affected cpu is all of the cpus.
	 */
	if (num_online_cpus() == 1) {
		cpumask_copy(policy->related_cpus, cpu_possible_mask);
		cpumask_copy(policy->cpus, cpu_online_mask);
	} else {
		cpumask_setall(policy->cpus);
	}

	return cpufreq_frequency_table_cpuinfo(policy, exynos_info->freq_table);
}

static struct cpufreq_driver exynos_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= exynos_verify_speed,
	.target		= exynos_target,
	.get		= exynos_getspeed,
	.init		= exynos_cpufreq_cpu_init,
	.name		= "exynos_cpufreq",
#ifdef CONFIG_PM
	.suspend	= exynos_cpufreq_suspend,
	.resume		= exynos_cpufreq_resume,
#endif
};

static int exynos_cpufreq_reboot_notifier_call(struct notifier_block *this,
				   unsigned long code, void *_cmd)
{
	int ret;

	mutex_lock(&cpufreq_lock);
	exynos_cpufreq_disable = true;
	mutex_unlock(&cpufreq_lock);

	freqs.old = exynos_getspeed(0);
	ret = exynos_cpufreq_scale(boot_freq, freqs.old);

	if (ret < 0)
		return NOTIFY_BAD;

	return NOTIFY_DONE;
}

static struct notifier_block exynos_cpufreq_reboot_notifier = {
	.notifier_call = exynos_cpufreq_reboot_notifier_call,
};

static int exynos_qos_handler(struct notifier_block *b, unsigned long val, void *v)
{
	int ret;

	if (freqs.old >= val)
		return NOTIFY_OK;

	mutex_lock(&cpufreq_lock);
	ret = exynos_cpufreq_scale(val, freqs.old);
	mutex_unlock(&cpufreq_lock);

	if (ret < 0)
		return NOTIFY_BAD;

	return NOTIFY_OK;
}

static struct notifier_block exynos_qos_notifier = {
	.notifier_call = exynos_qos_handler,
};

static int __init exynos_cpufreq_init(void)
{
	int ret = -EINVAL;

	exynos_info = kzalloc(sizeof(struct exynos_dvfs_info), GFP_KERNEL);
	if (!exynos_info)
		return -ENOMEM;

	if (soc_is_exynos4210())
		ret = exynos4210_cpufreq_init(exynos_info);
	else if (soc_is_exynos4212() || soc_is_exynos4412())
		ret = exynos4x12_cpufreq_init(exynos_info);
	else if (soc_is_exynos5250())
		ret = exynos5250_cpufreq_init(exynos_info);
	else
		pr_err("%s: CPU type not found\n", __func__);

	if (ret)
		goto err_vdd_arm;

	if (exynos_info->set_freq == NULL) {
		pr_err("%s: No set_freq function (ERR)\n", __func__);
		goto err_vdd_arm;
	}

	arm_regulator = regulator_get(NULL, "vdd_arm");
	if (IS_ERR(arm_regulator)) {
		pr_err("%s: failed to get resource vdd_arm\n", __func__);
		goto err_vdd_arm;
	}

	boot_freq = exynos_getspeed(0);
	max_freq = exynos_info->freq_table[exynos_info->max_support_idx].frequency;
	max_thermal_freq = max_freq;

	exynos_cpufreq_disable = false;

	register_pm_notifier(&exynos_cpufreq_nb);
	register_reboot_notifier(&exynos_cpufreq_reboot_notifier);
	pm_qos_add_notifier(PM_QOS_CPU_FREQ_MIN, &exynos_qos_notifier);

	if (cpufreq_register_driver(&exynos_driver)) {
		pr_err("%s: failed to register cpufreq driver\n", __func__);
		goto err_cpufreq;
	}

	exynos_cpufreq_init_done = true;

	return 0;
err_cpufreq:
	unregister_pm_notifier(&exynos_cpufreq_nb);

	if (!IS_ERR(arm_regulator))
		regulator_put(arm_regulator);
err_vdd_arm:
	kfree(exynos_info);
	pr_debug("%s: failed initialization\n", __func__);
	return -EINVAL;
}
late_initcall(exynos_cpufreq_init);
