/*
 * arch/arm/mach-tegra/cpu-tegra.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Based on arch/arm/plat-omap/cpu-omap.c, (C) 2005 Nokia Corporation
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/suspend.h>
#include <linux/debugfs.h>

#include <asm/smp_twd.h>
#include <asm/system.h>

#include <mach/hardware.h>
#include <mach/clk.h>

/* Frequency table index must be sequential starting at 0 and frequencies must be ascending*/
static struct cpufreq_frequency_table freq_table[] = {
	{ 0, 216000 },
	{ 1, 312000 },
	{ 2, 456000 },
	{ 3, 608000 },
	{ 4, 760000 },
	{ 5, 816000 },
	{ 6, 912000 },
	{ 7, 1000000 },
	{ 8, CPUFREQ_TABLE_END },
};

/* CPU frequency is gradually lowered when throttling is enabled */
#define THROTTLE_START_INDEX	2
#define THROTTLE_END_INDEX	6
#define THROTTLE_DELAY		msecs_to_jiffies(2000)
#define NO_DELAY		msecs_to_jiffies(0)

#define NUM_CPUS	2

static struct clk *cpu_clk;

static struct workqueue_struct *workqueue;

static unsigned long target_cpu_speed[NUM_CPUS];
static DEFINE_MUTEX(tegra_cpu_lock);
static bool is_suspended;

static DEFINE_MUTEX(throttling_lock);
static bool is_throttling;
static struct delayed_work throttle_work;


int tegra_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, freq_table);
}

unsigned int tegra_getspeed(unsigned int cpu)
{
	unsigned long rate;

	if (cpu >= NUM_CPUS)
		return 0;

	rate = clk_get_rate(cpu_clk) / 1000;
	return rate;
}

#ifdef CONFIG_HAVE_ARM_TWD
static void tegra_cpufreq_rescale_twd_other_cpu(void *data) {
	unsigned long new_rate = *(unsigned long *)data;
	twd_recalc_prescaler(new_rate);
}

static void tegra_cpufreq_rescale_twds(unsigned long new_rate)
{
	twd_recalc_prescaler(new_rate);
	smp_call_function(tegra_cpufreq_rescale_twd_other_cpu, &new_rate, 1);
}
#else
static inline void tegra_cpufreq_rescale_twds(unsigned long new_rate)
{
}
#endif

static int tegra_update_cpu_speed(unsigned long rate)
{
	int ret = 0;
	struct cpufreq_freqs freqs;

	freqs.old = tegra_getspeed(0);
	freqs.new = rate;

	if (freqs.old == freqs.new)
		return ret;

	for_each_online_cpu(freqs.cpu)
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	if (freqs.new > freqs.old)
		tegra_cpufreq_rescale_twds(freqs.new * 1000);

#ifdef CONFIG_CPU_FREQ_DEBUG
	printk(KERN_DEBUG "cpufreq-tegra: transition: %u --> %u\n",
	       freqs.old, freqs.new);
#endif

	ret = clk_set_rate(cpu_clk, freqs.new * 1000);
	if (ret) {
		pr_err("cpu-tegra: Failed to set cpu frequency to %d kHz\n",
			freqs.new);
		return ret;
	}

	if (freqs.new < freqs.old)
		tegra_cpufreq_rescale_twds(freqs.new * 1000);

	for_each_online_cpu(freqs.cpu)
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

static unsigned long tegra_cpu_highest_speed(void) {
	unsigned long rate = 0;
	int i;

	for_each_online_cpu(i)
		rate = max(rate, target_cpu_speed[i]);
	return rate;
}

static int tegra_target(struct cpufreq_policy *policy,
		       unsigned int target_freq,
		       unsigned int relation)
{
	int idx;
	unsigned int freq;
	unsigned int highest_speed;
	unsigned int limit_when_throttling;
	int ret = 0;

	mutex_lock(&tegra_cpu_lock);

	if (is_suspended) {
		ret = -EBUSY;
		goto out;
	}

	cpufreq_frequency_table_target(policy, freq_table, target_freq,
		relation, &idx);

	freq = freq_table[idx].frequency;

	target_cpu_speed[policy->cpu] = freq;

	highest_speed = tegra_cpu_highest_speed();

	/* Do not go above this frequency when throttling */
	limit_when_throttling = freq_table[THROTTLE_START_INDEX].frequency;

	if (is_throttling && highest_speed > limit_when_throttling) {
		if (tegra_getspeed(0) < limit_when_throttling) {
			ret = tegra_update_cpu_speed(limit_when_throttling);
			goto out;
		} else {
			ret = -EBUSY;
			goto out;
		}
	}

	ret = tegra_update_cpu_speed(highest_speed);
out:
	mutex_unlock(&tegra_cpu_lock);
	return ret;
}

static bool tegra_throttling_needed(unsigned long *rate)
{
	unsigned int current_freq = tegra_getspeed(0);
	int i;

	for (i = THROTTLE_END_INDEX; i >= THROTTLE_START_INDEX; i--) {
		if (freq_table[i].frequency < current_freq) {
			*rate = freq_table[i].frequency;
			return true;
		}
	}

	return false;
}

static void tegra_throttle_work_func(struct work_struct *work)
{
	unsigned long rate;

	mutex_lock(&tegra_cpu_lock);

	if (tegra_throttling_needed(&rate) && tegra_update_cpu_speed(rate) == 0) {
		queue_delayed_work(workqueue, &throttle_work, THROTTLE_DELAY);
	}

	mutex_unlock(&tegra_cpu_lock);
}

/**
 * tegra_throttling_enable
 * This functions may sleep
 */
void tegra_throttling_enable(void)
{
	mutex_lock(&throttling_lock);

	if (!is_throttling) {
		is_throttling = true;
		queue_delayed_work(workqueue, &throttle_work, NO_DELAY);
	}

	mutex_unlock(&throttling_lock);
}
EXPORT_SYMBOL_GPL(tegra_throttling_enable);

/**
 * tegra_throttling_disable
 * This functions may sleep
 */
void tegra_throttling_disable(void)
{
	mutex_lock(&throttling_lock);

	if (is_throttling) {
		cancel_delayed_work_sync(&throttle_work);
		is_throttling = false;
	}

	mutex_unlock(&throttling_lock);
}
EXPORT_SYMBOL_GPL(tegra_throttling_disable);

#ifdef CONFIG_DEBUG_FS
static int throttle_debug_set(void *data, u64 val)
{
	if (val) {
		tegra_throttling_enable();
	} else {
		tegra_throttling_disable();
	}

	return 0;
}
static int throttle_debug_get(void *data, u64 *val)
{
	*val = (u64) is_throttling;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(throttle_fops, throttle_debug_get, throttle_debug_set, "%llu\n");

static struct dentry *cpu_tegra_debugfs_root;

static int __init tegra_cpu_debug_init(void)
{
	cpu_tegra_debugfs_root = debugfs_create_dir("cpu-tegra", 0);

	if (!cpu_tegra_debugfs_root)
		return -ENOMEM;

	if (!debugfs_create_file("throttle", 0644, cpu_tegra_debugfs_root, NULL, &throttle_fops))
		goto err_out;

	return 0;

err_out:
	debugfs_remove_recursive(cpu_tegra_debugfs_root);
	return -ENOMEM;

}

static void __exit tegra_cpu_debug_exit(void)
{
	debugfs_remove_recursive(cpu_tegra_debugfs_root);
}

late_initcall(tegra_cpu_debug_init);
module_exit(tegra_cpu_debug_exit);
#endif

static int tegra_pm_notify(struct notifier_block *nb, unsigned long event,
	void *dummy)
{
	mutex_lock(&tegra_cpu_lock);
	if (event == PM_SUSPEND_PREPARE) {
		is_suspended = true;
		pr_info("Tegra cpufreq suspend: setting frequency to %d kHz\n",
			freq_table[0].frequency);
		tegra_update_cpu_speed(freq_table[0].frequency);
	} else if (event == PM_POST_SUSPEND) {
		is_suspended = false;
	}
	mutex_unlock(&tegra_cpu_lock);

	return NOTIFY_OK;
}

static struct notifier_block tegra_cpu_pm_notifier = {
	.notifier_call = tegra_pm_notify,
};

static int tegra_cpu_init(struct cpufreq_policy *policy)
{
	if (policy->cpu >= NUM_CPUS)
		return -EINVAL;

	cpu_clk = clk_get_sys(NULL, "cpu");
	if (IS_ERR(cpu_clk))
		return PTR_ERR(cpu_clk);

	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	cpufreq_frequency_table_get_attr(freq_table, policy->cpu);
	policy->cur = tegra_getspeed(policy->cpu);
	target_cpu_speed[policy->cpu] = policy->cur;

	/* FIXME: what's the actual transition time? */
	policy->cpuinfo.transition_latency = 300 * 1000;

	policy->shared_type = CPUFREQ_SHARED_TYPE_ALL;
	cpumask_copy(policy->related_cpus, cpu_possible_mask);

	if (policy->cpu == 0) {
		INIT_DELAYED_WORK(&throttle_work, tegra_throttle_work_func);
		register_pm_notifier(&tegra_cpu_pm_notifier);
	}

	return 0;
}

static int tegra_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	clk_put(cpu_clk);
	return 0;
}

static struct freq_attr *tegra_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver tegra_cpufreq_driver = {
	.verify		= tegra_verify_speed,
	.target		= tegra_target,
	.get		= tegra_getspeed,
	.init		= tegra_cpu_init,
	.exit		= tegra_cpu_exit,
	.name		= "tegra",
	.attr		= tegra_cpufreq_attr,
};

static int __init tegra_cpufreq_init(void)
{
	workqueue = create_singlethread_workqueue("cpu-tegra");
	if (!workqueue)
		return -ENOMEM;
	return cpufreq_register_driver(&tegra_cpufreq_driver);
}

static void __exit tegra_cpufreq_exit(void)
{
	destroy_workqueue(workqueue);
        cpufreq_unregister_driver(&tegra_cpufreq_driver);
}


MODULE_AUTHOR("Colin Cross <ccross@android.com>");
MODULE_DESCRIPTION("cpufreq driver for Nvidia Tegra2");
MODULE_LICENSE("GPL");
module_init(tegra_cpufreq_init);
module_exit(tegra_cpufreq_exit);
