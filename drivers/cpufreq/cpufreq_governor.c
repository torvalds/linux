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

static struct attribute_group *get_sysfs_attr(struct dbs_data *dbs_data)
{
	if (have_governor_per_policy())
		return dbs_data->cdata->attr_group_gov_pol;
	else
		return dbs_data->cdata->attr_group_gov_sys;
}

void dbs_check_cpu(struct dbs_data *dbs_data, int cpu)
{
	struct cpu_dbs_common_info *cdbs = dbs_data->cdata->get_cpu_cdbs(cpu);
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	struct cs_dbs_tuners *cs_tuners = dbs_data->tuners;
	struct cpufreq_policy *policy;
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

	policy = cdbs->cur_policy;

	/* Get Absolute Load */
	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_common_info *j_cdbs;
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
		 * Detecting this situation is easy: the governor's deferrable
		 * timer would not have fired during CPU-idle periods. Hence
		 * an unusually large 'wall_time' (as compared to the sampling
		 * rate) indicates this scenario.
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

static inline void __gov_queue_work(int cpu, struct dbs_data *dbs_data,
		unsigned int delay)
{
	struct cpu_dbs_common_info *cdbs = dbs_data->cdata->get_cpu_cdbs(cpu);

	mod_delayed_work_on(cpu, system_wq, &cdbs->work, delay);
}

void gov_queue_work(struct dbs_data *dbs_data, struct cpufreq_policy *policy,
		unsigned int delay, bool all_cpus)
{
	int i;

	mutex_lock(&cpufreq_governor_lock);
	if (!policy->governor_enabled)
		goto out_unlock;

	if (!all_cpus) {
		/*
		 * Use raw_smp_processor_id() to avoid preemptible warnings.
		 * We know that this is only called with all_cpus == false from
		 * works that have been queued with *_work_on() functions and
		 * those works are canceled during CPU_DOWN_PREPARE so they
		 * can't possibly run on any other CPU.
		 */
		__gov_queue_work(raw_smp_processor_id(), dbs_data, delay);
	} else {
		for_each_cpu(i, policy->cpus)
			__gov_queue_work(i, dbs_data, delay);
	}

out_unlock:
	mutex_unlock(&cpufreq_governor_lock);
}
EXPORT_SYMBOL_GPL(gov_queue_work);

static inline void gov_cancel_work(struct dbs_data *dbs_data,
		struct cpufreq_policy *policy)
{
	struct cpu_dbs_common_info *cdbs;
	int i;

	for_each_cpu(i, policy->cpus) {
		cdbs = dbs_data->cdata->get_cpu_cdbs(i);
		cancel_delayed_work_sync(&cdbs->work);
	}
}

/* Will return if we need to evaluate cpu load again or not */
bool need_load_eval(struct cpu_dbs_common_info *cdbs,
		unsigned int sampling_rate)
{
	if (policy_is_shared(cdbs->cur_policy)) {
		ktime_t time_now = ktime_get();
		s64 delta_us = ktime_us_delta(time_now, cdbs->time_stamp);

		/* Do nothing if we recently have sampled */
		if (delta_us < (s64)(sampling_rate / 2))
			return false;
		else
			cdbs->time_stamp = time_now;
	}

	return true;
}
EXPORT_SYMBOL_GPL(need_load_eval);

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

static int cpufreq_governor_init(struct cpufreq_policy *policy,
				 struct dbs_data *dbs_data,
				 struct common_dbs_data *cdata)
{
	unsigned int latency;
	int ret;

	if (dbs_data) {
		if (WARN_ON(have_governor_per_policy()))
			return -EINVAL;
		dbs_data->usage_count++;
		policy->governor_data = dbs_data;
		return 0;
	}

	dbs_data = kzalloc(sizeof(*dbs_data), GFP_KERNEL);
	if (!dbs_data)
		return -ENOMEM;

	dbs_data->cdata = cdata;
	dbs_data->usage_count = 1;

	ret = cdata->init(dbs_data, !policy->governor->initialized);
	if (ret)
		goto free_dbs_data;

	/* policy latency is in ns. Convert it to us first */
	latency = policy->cpuinfo.transition_latency / 1000;
	if (latency == 0)
		latency = 1;

	/* Bring kernel and HW constraints together */
	dbs_data->min_sampling_rate = max(dbs_data->min_sampling_rate,
					  MIN_LATENCY_MULTIPLIER * latency);
	set_sampling_rate(dbs_data, max(dbs_data->min_sampling_rate,
					latency * LATENCY_MULTIPLIER));

	if (!have_governor_per_policy()) {
		if (WARN_ON(cpufreq_get_global_kobject())) {
			ret = -EINVAL;
			goto cdata_exit;
		}
		cdata->gdbs_data = dbs_data;
	}

	ret = sysfs_create_group(get_governor_parent_kobj(policy),
				 get_sysfs_attr(dbs_data));
	if (ret)
		goto put_kobj;

	policy->governor_data = dbs_data;

	return 0;

put_kobj:
	if (!have_governor_per_policy()) {
		cdata->gdbs_data = NULL;
		cpufreq_put_global_kobject();
	}
cdata_exit:
	cdata->exit(dbs_data, !policy->governor->initialized);
free_dbs_data:
	kfree(dbs_data);
	return ret;
}

static void cpufreq_governor_exit(struct cpufreq_policy *policy,
				  struct dbs_data *dbs_data)
{
	struct common_dbs_data *cdata = dbs_data->cdata;

	policy->governor_data = NULL;
	if (!--dbs_data->usage_count) {
		sysfs_remove_group(get_governor_parent_kobj(policy),
				   get_sysfs_attr(dbs_data));

		if (!have_governor_per_policy()) {
			cdata->gdbs_data = NULL;
			cpufreq_put_global_kobject();
		}

		cdata->exit(dbs_data, policy->governor->initialized == 1);
		kfree(dbs_data);
	}
}

static int cpufreq_governor_start(struct cpufreq_policy *policy,
				  struct dbs_data *dbs_data)
{
	struct common_dbs_data *cdata = dbs_data->cdata;
	unsigned int sampling_rate, ignore_nice, j, cpu = policy->cpu;
	struct cpu_dbs_common_info *cpu_cdbs = cdata->get_cpu_cdbs(cpu);
	int io_busy = 0;

	if (!policy->cur)
		return -EINVAL;

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
		struct cpu_dbs_common_info *j_cdbs = cdata->get_cpu_cdbs(j);
		unsigned int prev_load;

		j_cdbs->cpu = j;
		j_cdbs->cur_policy = policy;
		j_cdbs->prev_cpu_idle =
			get_cpu_idle_time(j, &j_cdbs->prev_cpu_wall, io_busy);

		prev_load = (unsigned int)(j_cdbs->prev_cpu_wall -
					    j_cdbs->prev_cpu_idle);
		j_cdbs->prev_load = 100 * prev_load /
				    (unsigned int)j_cdbs->prev_cpu_wall;

		if (ignore_nice)
			j_cdbs->prev_cpu_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE];

		mutex_init(&j_cdbs->timer_mutex);
		INIT_DEFERRABLE_WORK(&j_cdbs->work, cdata->gov_dbs_timer);
	}

	if (cdata->governor == GOV_CONSERVATIVE) {
		struct cs_cpu_dbs_info_s *cs_dbs_info =
			cdata->get_cpu_dbs_info_s(cpu);

		cs_dbs_info->down_skip = 0;
		cs_dbs_info->enable = 1;
		cs_dbs_info->requested_freq = policy->cur;
	} else {
		struct od_ops *od_ops = cdata->gov_ops;
		struct od_cpu_dbs_info_s *od_dbs_info = cdata->get_cpu_dbs_info_s(cpu);

		od_dbs_info->rate_mult = 1;
		od_dbs_info->sample_type = OD_NORMAL_SAMPLE;
		od_ops->powersave_bias_init_cpu(cpu);
	}

	/* Initiate timer time stamp */
	cpu_cdbs->time_stamp = ktime_get();

	gov_queue_work(dbs_data, policy, delay_for_sampling_rate(sampling_rate),
		       true);
	return 0;
}

static void cpufreq_governor_stop(struct cpufreq_policy *policy,
				  struct dbs_data *dbs_data)
{
	struct common_dbs_data *cdata = dbs_data->cdata;
	unsigned int cpu = policy->cpu;
	struct cpu_dbs_common_info *cpu_cdbs = cdata->get_cpu_cdbs(cpu);

	if (cdata->governor == GOV_CONSERVATIVE) {
		struct cs_cpu_dbs_info_s *cs_dbs_info =
			cdata->get_cpu_dbs_info_s(cpu);

		cs_dbs_info->enable = 0;
	}

	gov_cancel_work(dbs_data, policy);

	mutex_destroy(&cpu_cdbs->timer_mutex);
	cpu_cdbs->cur_policy = NULL;
}

static void cpufreq_governor_limits(struct cpufreq_policy *policy,
				    struct dbs_data *dbs_data)
{
	struct common_dbs_data *cdata = dbs_data->cdata;
	unsigned int cpu = policy->cpu;
	struct cpu_dbs_common_info *cpu_cdbs = cdata->get_cpu_cdbs(cpu);

	if (!cpu_cdbs->cur_policy)
		return;

	mutex_lock(&cpu_cdbs->timer_mutex);
	if (policy->max < cpu_cdbs->cur_policy->cur)
		__cpufreq_driver_target(cpu_cdbs->cur_policy, policy->max,
					CPUFREQ_RELATION_H);
	else if (policy->min > cpu_cdbs->cur_policy->cur)
		__cpufreq_driver_target(cpu_cdbs->cur_policy, policy->min,
					CPUFREQ_RELATION_L);
	dbs_check_cpu(dbs_data, cpu);
	mutex_unlock(&cpu_cdbs->timer_mutex);
}

int cpufreq_governor_dbs(struct cpufreq_policy *policy,
			 struct common_dbs_data *cdata, unsigned int event)
{
	struct dbs_data *dbs_data;
	int ret = 0;

	/* Lock governor to block concurrent initialization of governor */
	mutex_lock(&cdata->mutex);

	if (have_governor_per_policy())
		dbs_data = policy->governor_data;
	else
		dbs_data = cdata->gdbs_data;

	if (WARN_ON(!dbs_data && (event != CPUFREQ_GOV_POLICY_INIT))) {
		ret = -EINVAL;
		goto unlock;
	}

	switch (event) {
	case CPUFREQ_GOV_POLICY_INIT:
		ret = cpufreq_governor_init(policy, dbs_data, cdata);
		break;
	case CPUFREQ_GOV_POLICY_EXIT:
		cpufreq_governor_exit(policy, dbs_data);
		break;
	case CPUFREQ_GOV_START:
		ret = cpufreq_governor_start(policy, dbs_data);
		break;
	case CPUFREQ_GOV_STOP:
		cpufreq_governor_stop(policy, dbs_data);
		break;
	case CPUFREQ_GOV_LIMITS:
		cpufreq_governor_limits(policy, dbs_data);
		break;
	}

unlock:
	mutex_unlock(&cdata->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_governor_dbs);
