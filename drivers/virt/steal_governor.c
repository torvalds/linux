// SPDX-License-Identifier: GPL-2.0-only
/*
 * Steal time governor driver periodically computes steal time.
 * Based on the thresholds it either reduce/increase the preferred
 * CPUs which can be used by the workload to avoid vCPU preemption
 * to an extent possible in paravirtualized environment.
 *
 * Available with CONFIG_STEAL_GOVERNOR
 *
 * Copyright (C) 2026 IBM
 * Author: Shrikanth Hegde <sshegde@linux.ibm.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cleanup.h>
#include <linux/cpuhplock.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/kconfig.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/sched/isolation.h>
#include <linux/topology.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#ifdef CONFIG_XEN
#include <xen/xen.h>
#endif

#if !IS_ENABLED(CONFIG_PREFERRED_CPU)
#error "Steal Governor requires CONFIG_PREFERRED_CPU"
#endif

struct steal_governor {
	ktime_t			time;
	u64			steal;
	unsigned long		delay;
	unsigned int		interval_ms;
	unsigned int		high_threshold;
	unsigned int		low_threshold;
	struct delayed_work	work;
};

static struct steal_governor sg_ctx = {
	.interval_ms	=	1000,	/* 1 second */
	.high_threshold =	500,	/* 5% */
	.low_threshold	=	200,	/* 2% */
};

static void restore_preferred_to_active(void)
{
	int cpu;

	guard(cpus_read_lock)();
	for_each_cpu(cpu, cpu_active_mask)
		set_cpu_preferred(cpu, true);
}

static int param_set_interval_ms(const char *val, const struct kernel_param *kp)
{
	unsigned int interval;
	int ret;

	ret = kstrtouint(val, 0, &interval);
	if (ret)
		return ret;

	if (interval < 100 || interval > 100000) {
		pr_err("interval_ms must be between 100 and 100000\n");
		return -EINVAL;
	}

	return param_set_uint(val, kp);
}

static const struct kernel_param_ops interval_ms_ops = {
	.set = param_set_interval_ms,
	.get = param_get_uint,
};

module_param_cb(interval_ms, &interval_ms_ops, &sg_ctx.interval_ms, 0444);
MODULE_PARM_DESC(interval_ms,
		 "Sampling frequency in milliseconds. default: 1000");

static int param_set_high_threshold(const char *val, const struct kernel_param *kp)
{
	unsigned int threshold;
	int ret;

	ret = kstrtouint(val, 0, &threshold);
	if (ret)
		return ret;

	if (threshold >= 100 * 100) {
		pr_err("high_threshold (%u) can't be more than 99.99%%\n", threshold);
		return -EINVAL;
	}

	return param_set_uint(val, kp);
}

static const struct kernel_param_ops high_threshold_ops = {
	.set = param_set_high_threshold,
	.get = param_get_uint,
};

module_param_cb(high_threshold, &high_threshold_ops, &sg_ctx.high_threshold, 0444);
MODULE_PARM_DESC(high_threshold,
		 "High steal threshold. default: 500 i.e 5%. Must be > low_threshold");

module_param_named(low_threshold, sg_ctx.low_threshold, uint, 0444);
MODULE_PARM_DESC(low_threshold,
		 "Low steal threshold. default: 200 i.e 2%. Must be < high_threshold");

/* Return collective steal time across system. */
static u64 get_system_steal_time(void)
{
	return kcpustat_field_total(CPUTIME_STEAL, cpu_possible_mask);
}

/* Return number of CPUs to consider for steal ratio. */
static unsigned int get_system_cpus(void)
{
	return num_active_cpus();
}

/*
 * Called when the steal governor detects high physical CPU contention.
 * It finds the last active core in the preferred mask and mark those
 * CPUs as non-preferred.
 *
 * Must ensure:
 * - at least one core is always kept as preferred
 * - preferred is always subset of active.
 */
static void decrease_preferred_cpus(void)
{
	const struct cpumask *first_hk_core;
	int target_cpu = nr_cpu_ids;
	int cpu;

	guard(cpus_read_lock)();
	cpu = cpumask_first_and(housekeeping_cpumask(HK_TYPE_KERNEL_NOISE),
				cpu_preferred_mask);
	if (cpu >= nr_cpu_ids)
		return;

	/* Always leave first housekeeping core as preferred. */
	first_hk_core = topology_sibling_cpumask(cpu);
	cpu = cpumask_last(cpu_preferred_mask);
	if (cpu >= nr_cpu_ids)
		return;

	/* Find the last CPU which doesn't belong to that first hk_core. */
	if (!cpumask_test_cpu(cpu, first_hk_core)) {
		target_cpu = cpu;
	} else {
		for_each_cpu_andnot(cpu, cpu_preferred_mask, first_hk_core)
			target_cpu = cpu;
	}

	/* Only the first housekeeping core remains */
	if (target_cpu >= nr_cpu_ids)
		return;

	for_each_cpu_and(cpu, topology_sibling_cpumask(target_cpu),
			 cpu_preferred_mask)
		set_cpu_preferred(cpu, false);
}

/*
 * Called when the steal governor detects no/low physical CPU contention.
 * It finds the first active core outside of preferred mask and mark
 * those CPUs as preferred.
 *
 * Must ensure preferred is subset of active.
 */
static void increase_preferred_cpus(void)
{
	int first_cpu, cpu;

	guard(cpus_read_lock)();
	first_cpu = cpumask_first_andnot(cpu_active_mask, cpu_preferred_mask);

	/* All CPUs are preferred. Nothing to increase further */
	if (first_cpu >= nr_cpu_ids)
		return;

	for_each_cpu_and(cpu, topology_sibling_cpumask(first_cpu),
			 cpu_active_mask)
		set_cpu_preferred(cpu, true);
}

static bool preferred_cpus_valid(void)
{
	if (cpumask_empty(cpu_preferred_mask)) {
		pr_err("empty preferred mask. stopping\n");
		return false;
	}

	if (!cpumask_subset(cpu_preferred_mask, cpu_active_mask)) {
		pr_err("preferred: %*pbl is not subset of active: %*pbl, stopping\n",
		       cpumask_pr_args(cpu_preferred_mask),
		       cpumask_pr_args(cpu_active_mask));
		return false;
	}

	return true;
}

static void steal_governor_loop(struct work_struct *work)
{
	u64 curr_steal, delta_steal, delta_ns, steal_ratio;
	ktime_t now;

	now = ktime_get();
	delta_ns = ktime_to_ns(ktime_sub(now, sg_ctx.time));

	if (unlikely(delta_ns < NSEC_PER_MSEC)) {
		pr_err_ratelimited("work scheduled too soon delta_ns: %llu\n", delta_ns);
		goto requeue_work;
	}

	curr_steal = get_system_steal_time();
	delta_steal = curr_steal > sg_ctx.steal ? curr_steal - sg_ctx.steal : 0;
	sg_ctx.steal = curr_steal;
	sg_ctx.time = now;

	/*
	 * steal_ratio = (delta_steal * 100*100)/(delta_ns * num_cpus())
	 * To avoid possible overflow, divide the denominator early.
	 * Note minimum interval is 100ms.
	 */
	delta_ns = max_t(u64, div_u64(delta_ns * get_system_cpus(), 10000), 1);
	steal_ratio = div64_u64(delta_steal, delta_ns);

	if (steal_ratio > sg_ctx.high_threshold)
		decrease_preferred_cpus();
	else if (steal_ratio <= sg_ctx.low_threshold)
		increase_preferred_cpus();
	/*
	 * else: steal ratio is within bounds. Still do design checks so that
	 * module restores to active if CPU hotplug breaks those assumptions.
	 */
	if (!preferred_cpus_valid()) {
		restore_preferred_to_active();
		return;
	}

requeue_work:
	schedule_delayed_work(&sg_ctx.work, sg_ctx.delay);
}

static int __init steal_governor_init(void)
{
#ifdef CONFIG_XEN
	if (xen_initial_domain()) {
		pr_err("Cannot load in Xen Dom0 (Host OS). Driver is for guests only.\n");
		return -ENODEV;
	}
#endif

	if (sg_ctx.low_threshold >= sg_ctx.high_threshold) {
		pr_err("low_threshold (%u) must be less than high_threshold (%u)\n",
		       sg_ctx.low_threshold, sg_ctx.high_threshold);
		return -EINVAL;
	}

	sg_ctx.delay = msecs_to_jiffies(sg_ctx.interval_ms);
	INIT_DELAYED_WORK(&sg_ctx.work, steal_governor_loop);
	sg_ctx.steal = get_system_steal_time();
	sg_ctx.time = ktime_get();
	schedule_delayed_work(&sg_ctx.work, sg_ctx.delay);
	pr_info("enabled. interval: %ums, high_threshold: %u, low_threshold: %u\n",
		sg_ctx.interval_ms, sg_ctx.high_threshold, sg_ctx.low_threshold);

	return 0;
}

static void __exit steal_governor_exit(void)
{
	disable_delayed_work_sync(&sg_ctx.work);
	restore_preferred_to_active();
	pr_info("disabled\n");
}

module_init(steal_governor_init);
module_exit(steal_governor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("Virtualization Steal Time Governor");
