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

static struct attribute_group *get_sysfs_attr(struct dbs_data *dbs_data)
{
	if (have_governor_per_policy())
		return dbs_data->cdata->attr_group_gov_pol;
	else
		return dbs_data->cdata->attr_group_gov_sys;
}

void dbs_check_cpu(struct dbs_data *dbs_data, int cpu)
{
	struct cpu_dbs_info *cdbs = dbs_data->cdata->get_cpu_cdbs(cpu);
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	struct cs_dbs_tuners *cs_tuners = dbs_data->tuners;
	struct cpufreq_policy *policy = cdbs->shared->policy;
	unsigned int sampling_rate;
	unsigned int max_load = 0;
	unsigned int ignore_nice;
	unsigned int j;

	if (dbs_data->cdata->governor == GOV_ONDEMAND) {
		struct od_cpu_dbs_info_s *od_dbs_info =
				dbs_data->cdata->get_cpu_dbs_info_s(cpu);

		/*
		 * Sometimes, the ondemand governor uses an additional
		 * multiplier to give long delays. So apply this multiplier to
		 * the 'sampling_rate', so as to keep the wake-up-from-idle
		 * detection logic a bit conservative.
		 */
		sampling_rate = od_tuners->sampling_rate;
		sampling_rate *= od_dbs_info->rate_mult;

		ignore_nice = od_tuners->ignore_nice_load;
	} else {
		sampling_rate = cs_tuners->sampling_rate;
		ignore_nice = cs_tuners->ignore_nice_load;
	}

	/* Get Absolute Load */
	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_info *j_cdbs;
		u64 cur_wall_time, cur_idle_time;
		unsigned int idle_time, wall_time;
		unsigned int load;
		int io_busy = 0;

		j_cdbs = dbs_data->cdata->get_cpu_cdbs(j);

		/*
		 * For the purpose of ondemand, waiting for disk IO is
		 * an indication that you're performance critical, and
		 * not that the system is actually idle. So do not add
		 * the iowait time to the cpu idle time.
		 */
		if (dbs_data->cdata->governor == GOV_ONDEMAND)
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

	dbs_data->cdata->gov_check_cpu(cpu, max_load);
}
EXPORT_SYMBOL_GPL(dbs_check_cpu);

void gov_set_update_util(struct cpu_common_dbs_info *shared,
			 unsigned int delay_us)
{
	struct cpufreq_policy *policy = shared->policy;
	struct dbs_data *dbs_data = policy->governor_data;
	int cpu;

	gov_update_sample_delay(shared, delay_us);
	shared->last_sample_time = 0;

	for_each_cpu(cpu, policy->cpus) {
		struct cpu_dbs_info *cdbs = dbs_data->cdata->get_cpu_cdbs(cpu);

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

static void gov_cancel_work(struct cpu_common_dbs_info *shared)
{
	/* Tell dbs_update_util_handler() to skip queuing up work items. */
	atomic_inc(&shared->skip_work);
	/*
	 * If dbs_update_util_handler() is already running, it may not notice
	 * the incremented skip_work, so wait for it to complete to prevent its
	 * work item from being queued up after the cancel_work_sync() below.
	 */
	gov_clear_update_util(shared->policy);
	irq_work_sync(&shared->irq_work);
	cancel_work_sync(&shared->work);
	atomic_set(&shared->skip_work, 0);
}

static void dbs_work_handler(struct work_struct *work)
{
	struct cpu_common_dbs_info *shared = container_of(work, struct
					cpu_common_dbs_info, work);
	struct cpufreq_policy *policy;
	struct dbs_data *dbs_data;
	unsigned int delay;

	policy = shared->policy;
	dbs_data = policy->governor_data;

	/*
	 * Make sure cpufreq_governor_limits() isn't evaluating load or the
	 * ondemand governor isn't updating the sampling rate in parallel.
	 */
	mutex_lock(&shared->timer_mutex);
	delay = dbs_data->cdata->gov_dbs_timer(policy);
	shared->sample_delay_ns = jiffies_to_nsecs(delay);
	mutex_unlock(&shared->timer_mutex);

	/*
	 * If the atomic operation below is reordered with respect to the
	 * sample delay modification, the utilization update handler may end
	 * up using a stale sample delay value.
	 */
	smp_mb__before_atomic();
	atomic_dec(&shared->skip_work);
}

static void dbs_irq_work(struct irq_work *irq_work)
{
	struct cpu_common_dbs_info *shared;

	shared = container_of(irq_work, struct cpu_common_dbs_info, irq_work);
	schedule_work(&shared->work);
}

static inline void gov_queue_irq_work(struct cpu_common_dbs_info *shared)
{
#ifdef CONFIG_SMP
	irq_work_queue_on(&shared->irq_work, smp_processor_id());
#else
	irq_work_queue(&shared->irq_work);
#endif
}

static void dbs_update_util_handler(struct update_util_data *data, u64 time,
				    unsigned long util, unsigned long max)
{
	struct cpu_dbs_info *cdbs = container_of(data, struct cpu_dbs_info, update_util);
	struct cpu_common_dbs_info *shared = cdbs->shared;

	/*
	 * The work may not be allowed to be queued up right now.
	 * Possible reasons:
	 * - Work has already been queued up or is in progress.
	 * - The governor is being stopped.
	 * - It is too early (too little time from the previous sample).
	 */
	if (atomic_inc_return(&shared->skip_work) == 1) {
		u64 delta_ns;

		delta_ns = time - shared->last_sample_time;
		if ((s64)delta_ns >= shared->sample_delay_ns) {
			shared->last_sample_time = time;
			gov_queue_irq_work(shared);
			return;
		}
	}
	atomic_dec(&shared->skip_work);
}

static void set_sampling_rate(struct dbs_data *dbs_data,
		unsigned int sampling_rate)
{
	if (dbs_data->cdata->governor == GOV_CONSERVATIVE) {
		struct cs_dbs_tuners *cs_tuners = dbs_data->tuners;
		cs_tuners->sampling_rate = sampling_rate;
	} else {
		struct od_dbs_tuners *od_tuners = dbs_data->tuners;
		od_tuners->sampling_rate = sampling_rate;
	}
}

static int alloc_common_dbs_info(struct cpufreq_policy *policy,
				 struct common_dbs_data *cdata)
{
	struct cpu_common_dbs_info *shared;
	int j;

	/* Allocate memory for the common information for policy->cpus */
	shared = kzalloc(sizeof(*shared), GFP_KERNEL);
	if (!shared)
		return -ENOMEM;

	/* Set shared for all CPUs, online+offline */
	for_each_cpu(j, policy->related_cpus)
		cdata->get_cpu_cdbs(j)->shared = shared;

	mutex_init(&shared->timer_mutex);
	atomic_set(&shared->skip_work, 0);
	init_irq_work(&shared->irq_work, dbs_irq_work);
	INIT_WORK(&shared->work, dbs_work_handler);
	return 0;
}

static void free_common_dbs_info(struct cpufreq_policy *policy,
				 struct common_dbs_data *cdata)
{
	struct cpu_dbs_info *cdbs = cdata->get_cpu_cdbs(policy->cpu);
	struct cpu_common_dbs_info *shared = cdbs->shared;
	int j;

	mutex_destroy(&shared->timer_mutex);

	for_each_cpu(j, policy->cpus)
		cdata->get_cpu_cdbs(j)->shared = NULL;

	kfree(shared);
}

static int cpufreq_governor_init(struct cpufreq_policy *policy,
				 struct common_dbs_data *cdata)
{
	struct dbs_data *dbs_data = cdata->gdbs_data;
	unsigned int latency;
	int ret;

	/* State should be equivalent to EXIT */
	if (policy->governor_data)
		return -EBUSY;

	if (dbs_data) {
		if (WARN_ON(have_governor_per_policy()))
			return -EINVAL;

		ret = alloc_common_dbs_info(policy, cdata);
		if (ret)
			return ret;

		dbs_data->usage_count++;
		policy->governor_data = dbs_data;
		return 0;
	}

	dbs_data = kzalloc(sizeof(*dbs_data), GFP_KERNEL);
	if (!dbs_data)
		return -ENOMEM;

	ret = alloc_common_dbs_info(policy, cdata);
	if (ret)
		goto free_dbs_data;

	dbs_data->cdata = cdata;
	dbs_data->usage_count = 1;

	ret = cdata->init(dbs_data, !policy->governor->initialized);
	if (ret)
		goto free_common_dbs_info;

	/* policy latency is in ns. Convert it to us first */
	latency = policy->cpuinfo.transition_latency / 1000;
	if (latency == 0)
		latency = 1;

	/* Bring kernel and HW constraints together */
	dbs_data->min_sampling_rate = max(dbs_data->min_sampling_rate,
					  MIN_LATENCY_MULTIPLIER * latency);
	set_sampling_rate(dbs_data, max(dbs_data->min_sampling_rate,
					latency * LATENCY_MULTIPLIER));

	if (!have_governor_per_policy())
		cdata->gdbs_data = dbs_data;

	policy->governor_data = dbs_data;

	ret = sysfs_create_group(get_governor_parent_kobj(policy),
				 get_sysfs_attr(dbs_data));
	if (ret)
		goto reset_gdbs_data;

	return 0;

reset_gdbs_data:
	policy->governor_data = NULL;

	if (!have_governor_per_policy())
		cdata->gdbs_data = NULL;
	cdata->exit(dbs_data, !policy->governor->initialized);
free_common_dbs_info:
	free_common_dbs_info(policy, cdata);
free_dbs_data:
	kfree(dbs_data);
	return ret;
}

static int cpufreq_governor_exit(struct cpufreq_policy *policy)
{
	struct dbs_data *dbs_data = policy->governor_data;
	struct common_dbs_data *cdata = dbs_data->cdata;
	struct cpu_dbs_info *cdbs = cdata->get_cpu_cdbs(policy->cpu);

	/* State should be equivalent to INIT */
	if (!cdbs->shared || cdbs->shared->policy)
		return -EBUSY;

	if (!--dbs_data->usage_count) {
		sysfs_remove_group(get_governor_parent_kobj(policy),
				   get_sysfs_attr(dbs_data));

		policy->governor_data = NULL;

		if (!have_governor_per_policy())
			cdata->gdbs_data = NULL;

		cdata->exit(dbs_data, policy->governor->initialized == 1);
		kfree(dbs_data);
	} else {
		policy->governor_data = NULL;
	}

	free_common_dbs_info(policy, cdata);
	return 0;
}

static int cpufreq_governor_start(struct cpufreq_policy *policy)
{
	struct dbs_data *dbs_data = policy->governor_data;
	struct common_dbs_data *cdata = dbs_data->cdata;
	unsigned int sampling_rate, ignore_nice, j, cpu = policy->cpu;
	struct cpu_dbs_info *cdbs = cdata->get_cpu_cdbs(cpu);
	struct cpu_common_dbs_info *shared = cdbs->shared;
	int io_busy = 0;

	if (!policy->cur)
		return -EINVAL;

	/* State should be equivalent to INIT */
	if (!shared || shared->policy)
		return -EBUSY;

	if (cdata->governor == GOV_CONSERVATIVE) {
		struct cs_dbs_tuners *cs_tuners = dbs_data->tuners;

		sampling_rate = cs_tuners->sampling_rate;
		ignore_nice = cs_tuners->ignore_nice_load;
	} else {
		struct od_dbs_tuners *od_tuners = dbs_data->tuners;

		sampling_rate = od_tuners->sampling_rate;
		ignore_nice = od_tuners->ignore_nice_load;
		io_busy = od_tuners->io_is_busy;
	}

	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_info *j_cdbs = cdata->get_cpu_cdbs(j);
		unsigned int prev_load;

		j_cdbs->prev_cpu_idle =
			get_cpu_idle_time(j, &j_cdbs->prev_cpu_wall, io_busy);

		prev_load = (unsigned int)(j_cdbs->prev_cpu_wall -
					    j_cdbs->prev_cpu_idle);
		j_cdbs->prev_load = 100 * prev_load /
				    (unsigned int)j_cdbs->prev_cpu_wall;

		if (ignore_nice)
			j_cdbs->prev_cpu_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE];

		j_cdbs->update_util.func = dbs_update_util_handler;
	}
	shared->policy = policy;

	if (cdata->governor == GOV_CONSERVATIVE) {
		struct cs_cpu_dbs_info_s *cs_dbs_info =
			cdata->get_cpu_dbs_info_s(cpu);

		cs_dbs_info->down_skip = 0;
		cs_dbs_info->requested_freq = policy->cur;
	} else {
		struct od_ops *od_ops = cdata->gov_ops;
		struct od_cpu_dbs_info_s *od_dbs_info = cdata->get_cpu_dbs_info_s(cpu);

		od_dbs_info->rate_mult = 1;
		od_dbs_info->sample_type = OD_NORMAL_SAMPLE;
		od_ops->powersave_bias_init_cpu(cpu);
	}

	gov_set_update_util(shared, sampling_rate);
	return 0;
}

static int cpufreq_governor_stop(struct cpufreq_policy *policy)
{
	struct dbs_data *dbs_data = policy->governor_data;
	struct cpu_dbs_info *cdbs = dbs_data->cdata->get_cpu_cdbs(policy->cpu);
	struct cpu_common_dbs_info *shared = cdbs->shared;

	/* State should be equivalent to START */
	if (!shared || !shared->policy)
		return -EBUSY;

	gov_cancel_work(shared);
	shared->policy = NULL;

	return 0;
}

static int cpufreq_governor_limits(struct cpufreq_policy *policy)
{
	struct dbs_data *dbs_data = policy->governor_data;
	struct common_dbs_data *cdata = dbs_data->cdata;
	unsigned int cpu = policy->cpu;
	struct cpu_dbs_info *cdbs = cdata->get_cpu_cdbs(cpu);

	/* State should be equivalent to START */
	if (!cdbs->shared || !cdbs->shared->policy)
		return -EBUSY;

	mutex_lock(&cdbs->shared->timer_mutex);
	if (policy->max < cdbs->shared->policy->cur)
		__cpufreq_driver_target(cdbs->shared->policy, policy->max,
					CPUFREQ_RELATION_H);
	else if (policy->min > cdbs->shared->policy->cur)
		__cpufreq_driver_target(cdbs->shared->policy, policy->min,
					CPUFREQ_RELATION_L);
	dbs_check_cpu(dbs_data, cpu);
	mutex_unlock(&cdbs->shared->timer_mutex);

	return 0;
}

int cpufreq_governor_dbs(struct cpufreq_policy *policy,
			 struct common_dbs_data *cdata, unsigned int event)
{
	int ret = -EINVAL;

	/* Lock governor to block concurrent initialization of governor */
	mutex_lock(&dbs_data_mutex);

	if (event == CPUFREQ_GOV_POLICY_INIT) {
		ret = cpufreq_governor_init(policy, cdata);
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
