/*
 * drivers/cpufreq/cpufreq_governor.c
 *
 * CPUFREQ governors common code
 *
 * Copyright	(C) 2001 Russell King
 *		(C) 2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *		(C) 2003 Jun Nakajima <jun.nakajima@intel.com>
 *		(C) 2009 Alexander Clouter <alex@digriz.org.uk>
 *		(c) 2012 Viresh Kumar <viresh.kumar@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/export.h>
#include <linux/kernel_stat.h>
#include <linux/slab.h>

#include "cpufreq_governor.h"

DEFINE_MUTEX(dbs_data_mutex);
EXPORT_SYMBOL_GPL(dbs_data_mutex);

static struct attribute_group *get_sysfs_attr(struct dbs_governor *gov)
{
	return have_governor_per_policy() ?
		gov->attr_group_gov_pol : gov->attr_group_gov_sys;
}

void dbs_check_cpu(struct cpufreq_policy *policy)
{
	int cpu = policy->cpu;
	struct dbs_governor *gov = dbs_governor_of(policy);
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	unsigned int sampling_rate = dbs_data->sampling_rate;
	unsigned int ignore_nice = dbs_data->ignore_nice_load;
	unsigned int max_load = 0;
	unsigned int j;

	if (gov->governor == GOV_ONDEMAND) {
		struct od_cpu_dbs_info_s *od_dbs_info =
				gov->get_cpu_dbs_info_s(cpu);

		/*
		 * Sometimes, the ondemand governor uses an additional
		 * multiplier to give long delays. So apply this multiplier to
		 * the 'sampling_rate', so as to keep the wake-up-from-idle
		 * detection logic a bit conservative.
		 */
		sampling_rate *= od_dbs_info->rate_mult;

	}

	/* Get Absolute Load */
	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_info *j_cdbs;
		u64 cur_wall_time, cur_idle_time;
		unsigned int idle_time, wall_time;
		unsigned int load;
		int io_busy = 0;

		j_cdbs = gov->get_cpu_cdbs(j);

		/*
		 * For the purpose of ondemand, waiting for disk IO is
		 * an indication that you're performance critical, and
		 * not that the system is actually idle. So do not add
		 * the iowait time to the cpu idle time.
		 */
		if (gov->governor == GOV_ONDEMAND)
			io_busy = od_tuners->io_is_busy;
		cur_idle_time = get_cpu_idle_time(j, &cur_wall_time, io_busy);

		wall_time = (unsigned int)
			(cur_wall_time - j_cdbs->prev_cpu_wall);
		j_cdbs->prev_cpu_wall = cur_wall_time;

		if (cur_idle_time < j_cdbs->prev_cpu_idle)
			cur_idle_time = j_cdbs->prev_cpu_idle;

		idle_time = (unsigned int)
			(cur_idle_time - j_cdbs->prev_cpu_idle);
		j_cdbs->prev_cpu_idle = cur_idle_time;

		if (ignore_nice) {
			struct cpu_dbs_info *cdbs = gov->get_cpu_cdbs(cpu);
			u64 cur_nice;
			unsigned long cur_nice_jiffies;

			cur_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE] -
					 cdbs->prev_cpu_nice;
			/*
			 * Assumption: nice time between sampling periods will
			 * be less than 2^32 jiffies for 32 bit sys
			 */
			cur_nice_jiffies = (unsigned long)
					cputime64_to_jiffies64(cur_nice);

			cdbs->prev_cpu_nice =
				kcpustat_cpu(j).cpustat[CPUTIME_NICE];
			idle_time += jiffies_to_usecs(cur_nice_jiffies);
		}

		if (unlikely(!wall_time || wall_time < idle_time))
			continue;

		/*
		 * If the CPU had gone completely idle, and a task just woke up
		 * on this CPU now, it would be unfair to calculate 'load' the
		 * usual way for this elapsed time-window, because it will show
		 * near-zero load, irrespective of how CPU intensive that task
		 * actually is. This is undesirable for latency-sensitive bursty
		 * workloads.
		 *
		 * To avoid this, we reuse the 'load' from the previous
		 * time-window and give this task a chance to start with a
		 * reasonably high CPU frequency. (However, we shouldn't over-do
		 * this copy, lest we get stuck at a high load (high frequency)
		 * for too long, even when the current system load has actually
		 * dropped down. So we perform the copy only once, upon the
		 * first wake-up from idle.)
		 *
		 * Detecting this situation is easy: the governor's utilization
		 * update handler would not have run during CPU-idle periods.
		 * Hence, an unusually large 'wall_time' (as compared to the
		 * sampling rate) indicates this scenario.
		 *
		 * prev_load can be zero in two cases and we must recalculate it
		 * for both cases:
		 * - during long idle intervals
		 * - explicitly set to zero
		 */
		if (unlikely(wall_time > (2 * sampling_rate) &&
			     j_cdbs->prev_load)) {
			load = j_cdbs->prev_load;

			/*
			 * Perform a destructive copy, to ensure that we copy
			 * the previous load only once, upon the first wake-up
			 * from idle.
			 */
			j_cdbs->prev_load = 0;
		} else {
			load = 100 * (wall_time - idle_time) / wall_time;
			j_cdbs->prev_load = load;
		}

		if (load > max_load)
			max_load = load;
	}

	gov->gov_check_cpu(cpu, max_load);
}
EXPORT_SYMBOL_GPL(dbs_check_cpu);

void gov_set_update_util(struct policy_dbs_info *policy_dbs,
			 unsigned int delay_us)
{
	struct cpufreq_policy *policy = policy_dbs->policy;
	struct dbs_governor *gov = dbs_governor_of(policy);
	int cpu;

	gov_update_sample_delay(policy_dbs, delay_us);
	policy_dbs->last_sample_time = 0;

	for_each_cpu(cpu, policy->cpus) {
		struct cpu_dbs_info *cdbs = gov->get_cpu_cdbs(cpu);

		cpufreq_set_update_util_data(cpu, &cdbs->update_util);
	}
}
EXPORT_SYMBOL_GPL(gov_set_update_util);

static inline void gov_clear_update_util(struct cpufreq_policy *policy)
{
	int i;

	for_each_cpu(i, policy->cpus)
		cpufreq_set_update_util_data(i, NULL);

	synchronize_rcu();
}

static void gov_cancel_work(struct policy_dbs_info *policy_dbs)
{
	/* Tell dbs_update_util_handler() to skip queuing up work items. */
	atomic_inc(&policy_dbs->work_count);
	/*
	 * If dbs_update_util_handler() is already running, it may not notice
	 * the incremented work_count, so wait for it to complete to prevent its
	 * work item from being queued up after the cancel_work_sync() below.
	 */
	gov_clear_update_util(policy_dbs->policy);
	irq_work_sync(&policy_dbs->irq_work);
	cancel_work_sync(&policy_dbs->work);
	atomic_set(&policy_dbs->work_count, 0);
}

static void dbs_work_handler(struct work_struct *work)
{
	struct policy_dbs_info *policy_dbs;
	struct cpufreq_policy *policy;
	struct dbs_governor *gov;
	unsigned int delay;

	policy_dbs = container_of(work, struct policy_dbs_info, work);
	policy = policy_dbs->policy;
	gov = dbs_governor_of(policy);

	/*
	 * Make sure cpufreq_governor_limits() isn't evaluating load or the
	 * ondemand governor isn't updating the sampling rate in parallel.
	 */
	mutex_lock(&policy_dbs->timer_mutex);
	delay = gov->gov_dbs_timer(policy);
	policy_dbs->sample_delay_ns = jiffies_to_nsecs(delay);
	mutex_unlock(&policy_dbs->timer_mutex);

	/*
	 * If the atomic operation below is reordered with respect to the
	 * sample delay modification, the utilization update handler may end
	 * up using a stale sample delay value.
	 */
	smp_mb__before_atomic();
	atomic_dec(&policy_dbs->work_count);
}

static void dbs_irq_work(struct irq_work *irq_work)
{
	struct policy_dbs_info *policy_dbs;

	policy_dbs = container_of(irq_work, struct policy_dbs_info, irq_work);
	schedule_work(&policy_dbs->work);
}

static inline void gov_queue_irq_work(struct policy_dbs_info *policy_dbs)
{
#ifdef CONFIG_SMP
	irq_work_queue_on(&policy_dbs->irq_work, smp_processor_id());
#else
	irq_work_queue(&policy_dbs->irq_work);
#endif
}

static void dbs_update_util_handler(struct update_util_data *data, u64 time,
				    unsigned long util, unsigned long max)
{
	struct cpu_dbs_info *cdbs = container_of(data, struct cpu_dbs_info, update_util);
	struct policy_dbs_info *policy_dbs = cdbs->policy_dbs;

	/*
	 * The work may not be allowed to be queued up right now.
	 * Possible reasons:
	 * - Work has already been queued up or is in progress.
	 * - The governor is being stopped.
	 * - It is too early (too little time from the previous sample).
	 */
	if (atomic_inc_return(&policy_dbs->work_count) == 1) {
		u64 delta_ns;

		delta_ns = time - policy_dbs->last_sample_time;
		if ((s64)delta_ns >= policy_dbs->sample_delay_ns) {
			policy_dbs->last_sample_time = time;
			gov_queue_irq_work(policy_dbs);
			return;
		}
	}
	atomic_dec(&policy_dbs->work_count);
}

static struct policy_dbs_info *alloc_policy_dbs_info(struct cpufreq_policy *policy,
						     struct dbs_governor *gov)
{
	struct policy_dbs_info *policy_dbs;
	int j;

	/* Allocate memory for the common information for policy->cpus */
	policy_dbs = kzalloc(sizeof(*policy_dbs), GFP_KERNEL);
	if (!policy_dbs)
		return NULL;

	mutex_init(&policy_dbs->timer_mutex);
	atomic_set(&policy_dbs->work_count, 0);
	init_irq_work(&policy_dbs->irq_work, dbs_irq_work);
	INIT_WORK(&policy_dbs->work, dbs_work_handler);

	/* Set policy_dbs for all CPUs, online+offline */
	for_each_cpu(j, policy->related_cpus) {
		struct cpu_dbs_info *j_cdbs = gov->get_cpu_cdbs(j);

		j_cdbs->policy_dbs = policy_dbs;
		j_cdbs->update_util.func = dbs_update_util_handler;
	}
	return policy_dbs;
}

static void free_policy_dbs_info(struct cpufreq_policy *policy,
				 struct dbs_governor *gov)
{
	struct cpu_dbs_info *cdbs = gov->get_cpu_cdbs(policy->cpu);
	struct policy_dbs_info *policy_dbs = cdbs->policy_dbs;
	int j;

	mutex_destroy(&policy_dbs->timer_mutex);

	for_each_cpu(j, policy->related_cpus) {
		struct cpu_dbs_info *j_cdbs = gov->get_cpu_cdbs(j);

		j_cdbs->policy_dbs = NULL;
		j_cdbs->update_util.func = NULL;
	}
	kfree(policy_dbs);
}

static int cpufreq_governor_init(struct cpufreq_policy *policy)
{
	struct dbs_governor *gov = dbs_governor_of(policy);
	struct dbs_data *dbs_data = gov->gdbs_data;
	struct policy_dbs_info *policy_dbs;
	unsigned int latency;
	int ret;

	/* State should be equivalent to EXIT */
	if (policy->governor_data)
		return -EBUSY;

	policy_dbs = alloc_policy_dbs_info(policy, gov);
	if (!policy_dbs)
		return -ENOMEM;

	if (dbs_data) {
		if (WARN_ON(have_governor_per_policy())) {
			ret = -EINVAL;
			goto free_policy_dbs_info;
		}
		dbs_data->usage_count++;
		policy_dbs->dbs_data = dbs_data;
		policy->governor_data = policy_dbs;
		return 0;
	}

	dbs_data = kzalloc(sizeof(*dbs_data), GFP_KERNEL);
	if (!dbs_data) {
		ret = -ENOMEM;
		goto free_policy_dbs_info;
	}

	dbs_data->usage_count = 1;

	ret = gov->init(dbs_data, !policy->governor->initialized);
	if (ret)
		goto free_policy_dbs_info;

	/* policy latency is in ns. Convert it to us first */
	latency = policy->cpuinfo.transition_latency / 1000;
	if (latency == 0)
		latency = 1;

	/* Bring kernel and HW constraints together */
	dbs_data->min_sampling_rate = max(dbs_data->min_sampling_rate,
					  MIN_LATENCY_MULTIPLIER * latency);
	dbs_data->sampling_rate = max(dbs_data->min_sampling_rate,
				      LATENCY_MULTIPLIER * latency);

	if (!have_governor_per_policy())
		gov->gdbs_data = dbs_data;

	policy_dbs->dbs_data = dbs_data;
	policy->governor_data = policy_dbs;

	ret = sysfs_create_group(get_governor_parent_kobj(policy),
				 get_sysfs_attr(gov));
	if (!ret)
		return 0;

	/* Failure, so roll back. */

	policy->governor_data = NULL;

	if (!have_governor_per_policy())
		gov->gdbs_data = NULL;
	gov->exit(dbs_data, !policy->governor->initialized);
	kfree(dbs_data);

free_policy_dbs_info:
	free_policy_dbs_info(policy, gov);
	return ret;
}

static int cpufreq_governor_exit(struct cpufreq_policy *policy)
{
	struct dbs_governor *gov = dbs_governor_of(policy);
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;

	/* State should be equivalent to INIT */
	if (policy_dbs->policy)
		return -EBUSY;

	if (!--dbs_data->usage_count) {
		sysfs_remove_group(get_governor_parent_kobj(policy),
				   get_sysfs_attr(gov));

		policy->governor_data = NULL;

		if (!have_governor_per_policy())
			gov->gdbs_data = NULL;

		gov->exit(dbs_data, policy->governor->initialized == 1);
		kfree(dbs_data);
	} else {
		policy->governor_data = NULL;
	}

	free_policy_dbs_info(policy, gov);
	return 0;
}

static int cpufreq_governor_start(struct cpufreq_policy *policy)
{
	struct dbs_governor *gov = dbs_governor_of(policy);
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	unsigned int sampling_rate, ignore_nice, j, cpu = policy->cpu;
	int io_busy = 0;

	if (!policy->cur)
		return -EINVAL;

	/* State should be equivalent to INIT */
	if (policy_dbs->policy)
		return -EBUSY;

	sampling_rate = dbs_data->sampling_rate;
	ignore_nice = dbs_data->ignore_nice_load;

	if (gov->governor == GOV_ONDEMAND) {
		struct od_dbs_tuners *od_tuners = dbs_data->tuners;

		io_busy = od_tuners->io_is_busy;
	}

	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_info *j_cdbs = gov->get_cpu_cdbs(j);
		unsigned int prev_load;

		j_cdbs->prev_cpu_idle =
			get_cpu_idle_time(j, &j_cdbs->prev_cpu_wall, io_busy);

		prev_load = (unsigned int)(j_cdbs->prev_cpu_wall -
					    j_cdbs->prev_cpu_idle);
		j_cdbs->prev_load = 100 * prev_load /
				    (unsigned int)j_cdbs->prev_cpu_wall;

		if (ignore_nice)
			j_cdbs->prev_cpu_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE];
	}
	policy_dbs->policy = policy;

	if (gov->governor == GOV_CONSERVATIVE) {
		struct cs_cpu_dbs_info_s *cs_dbs_info =
			gov->get_cpu_dbs_info_s(cpu);

		cs_dbs_info->down_skip = 0;
		cs_dbs_info->requested_freq = policy->cur;
	} else {
		struct od_ops *od_ops = gov->gov_ops;
		struct od_cpu_dbs_info_s *od_dbs_info = gov->get_cpu_dbs_info_s(cpu);

		od_dbs_info->rate_mult = 1;
		od_dbs_info->sample_type = OD_NORMAL_SAMPLE;
		od_ops->powersave_bias_init_cpu(cpu);
	}

	gov_set_update_util(policy_dbs, sampling_rate);
	return 0;
}

static int cpufreq_governor_stop(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;

	/* State should be equivalent to START */
	if (!policy_dbs->policy)
		return -EBUSY;

	gov_cancel_work(policy_dbs);
	policy_dbs->policy = NULL;

	return 0;
}

static int cpufreq_governor_limits(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;

	/* State should be equivalent to START */
	if (!policy_dbs->policy)
		return -EBUSY;

	mutex_lock(&policy_dbs->timer_mutex);
	if (policy->max < policy->cur)
		__cpufreq_driver_target(policy, policy->max, CPUFREQ_RELATION_H);
	else if (policy->min > policy->cur)
		__cpufreq_driver_target(policy, policy->min, CPUFREQ_RELATION_L);
	dbs_check_cpu(policy);
	mutex_unlock(&policy_dbs->timer_mutex);

	return 0;
}

int cpufreq_governor_dbs(struct cpufreq_policy *policy, unsigned int event)
{
	int ret = -EINVAL;

	/* Lock governor to block concurrent initialization of governor */
	mutex_lock(&dbs_data_mutex);

	if (event == CPUFREQ_GOV_POLICY_INIT) {
		ret = cpufreq_governor_init(policy);
	} else if (policy->governor_data) {
		switch (event) {
		case CPUFREQ_GOV_POLICY_EXIT:
			ret = cpufreq_governor_exit(policy);
			break;
		case CPUFREQ_GOV_START:
			ret = cpufreq_governor_start(policy);
			break;
		case CPUFREQ_GOV_STOP:
			ret = cpufreq_governor_stop(policy);
			break;
		case CPUFREQ_GOV_LIMITS:
			ret = cpufreq_governor_limits(policy);
			break;
		}
	}

	mutex_unlock(&dbs_data_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_governor_dbs);
