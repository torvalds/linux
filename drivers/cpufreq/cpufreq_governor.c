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

#include <asm/cputime.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/export.h>
#include <linux/kernel_stat.h>
#include <linux/mutex.h>
#include <linux/tick.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "cpufreq_governor.h"

static inline u64 get_cpu_idle_time_jiffy(unsigned int cpu, u64 *wall)
{
	u64 idle_time;
	u64 cur_wall_time;
	u64 busy_time;

	cur_wall_time = jiffies64_to_cputime64(get_jiffies_64());

	busy_time = kcpustat_cpu(cpu).cpustat[CPUTIME_USER];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SYSTEM];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_IRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SOFTIRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_STEAL];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_NICE];

	idle_time = cur_wall_time - busy_time;
	if (wall)
		*wall = cputime_to_usecs(cur_wall_time);

	return cputime_to_usecs(idle_time);
}

u64 get_cpu_idle_time(unsigned int cpu, u64 *wall)
{
	u64 idle_time = get_cpu_idle_time_us(cpu, NULL);

	if (idle_time == -1ULL)
		return get_cpu_idle_time_jiffy(cpu, wall);
	else
		idle_time += get_cpu_iowait_time_us(cpu, wall);

	return idle_time;
}
EXPORT_SYMBOL_GPL(get_cpu_idle_time);

void dbs_check_cpu(struct dbs_data *dbs_data, int cpu)
{
	struct cpu_dbs_common_info *cdbs = dbs_data->get_cpu_cdbs(cpu);
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	struct cs_dbs_tuners *cs_tuners = dbs_data->tuners;
	struct cpufreq_policy *policy;
	unsigned int max_load = 0;
	unsigned int ignore_nice;
	unsigned int j;

	if (dbs_data->governor == GOV_ONDEMAND)
		ignore_nice = od_tuners->ignore_nice;
	else
		ignore_nice = cs_tuners->ignore_nice;

	policy = cdbs->cur_policy;

	/* Get Absolute Load (in terms of freq for ondemand gov) */
	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_common_info *j_cdbs;
		u64 cur_wall_time, cur_idle_time, cur_iowait_time;
		unsigned int idle_time, wall_time, iowait_time;
		unsigned int load;

		j_cdbs = dbs_data->get_cpu_cdbs(j);

		cur_idle_time = get_cpu_idle_time(j, &cur_wall_time);

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

		if (dbs_data->governor == GOV_ONDEMAND) {
			struct od_cpu_dbs_info_s *od_j_dbs_info =
				dbs_data->get_cpu_dbs_info_s(cpu);

			cur_iowait_time = get_cpu_iowait_time_us(j,
					&cur_wall_time);
			if (cur_iowait_time == -1ULL)
				cur_iowait_time = 0;

			iowait_time = (unsigned int) (cur_iowait_time -
					od_j_dbs_info->prev_cpu_iowait);
			od_j_dbs_info->prev_cpu_iowait = cur_iowait_time;

			/*
			 * For the purpose of ondemand, waiting for disk IO is
			 * an indication that you're performance critical, and
			 * not that the system is actually idle. So subtract the
			 * iowait time from the cpu idle time.
			 */
			if (od_tuners->io_is_busy && idle_time >= iowait_time)
				idle_time -= iowait_time;
		}

		if (unlikely(!wall_time || wall_time < idle_time))
			continue;

		load = 100 * (wall_time - idle_time) / wall_time;

		if (dbs_data->governor == GOV_ONDEMAND) {
			int freq_avg = __cpufreq_driver_getavg(policy, j);
			if (freq_avg <= 0)
				freq_avg = policy->cur;

			load *= freq_avg;
		}

		if (load > max_load)
			max_load = load;
	}

	dbs_data->gov_check_cpu(cpu, max_load);
}
EXPORT_SYMBOL_GPL(dbs_check_cpu);

bool dbs_sw_coordinated_cpus(struct cpu_dbs_common_info *cdbs)
{
	struct cpufreq_policy *policy = cdbs->cur_policy;

	return cpumask_weight(policy->cpus) > 1;
}
EXPORT_SYMBOL_GPL(dbs_sw_coordinated_cpus);

static inline void dbs_timer_init(struct dbs_data *dbs_data,
				  struct cpu_dbs_common_info *cdbs,
				  unsigned int sampling_rate,
				  int cpu)
{
	int delay = delay_for_sampling_rate(sampling_rate);
	struct cpu_dbs_common_info *cdbs_local = dbs_data->get_cpu_cdbs(cpu);

	schedule_delayed_work_on(cpu, &cdbs_local->work, delay);
}

static inline void dbs_timer_exit(struct cpu_dbs_common_info *cdbs)
{
	cancel_delayed_work_sync(&cdbs->work);
}

int cpufreq_governor_dbs(struct dbs_data *dbs_data,
		struct cpufreq_policy *policy, unsigned int event)
{
	struct od_cpu_dbs_info_s *od_dbs_info = NULL;
	struct cs_cpu_dbs_info_s *cs_dbs_info = NULL;
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	struct cs_dbs_tuners *cs_tuners = dbs_data->tuners;
	struct cpu_dbs_common_info *cpu_cdbs;
	unsigned int *sampling_rate, latency, ignore_nice, j, cpu = policy->cpu;
	int rc;

	cpu_cdbs = dbs_data->get_cpu_cdbs(cpu);

	if (dbs_data->governor == GOV_CONSERVATIVE) {
		cs_dbs_info = dbs_data->get_cpu_dbs_info_s(cpu);
		sampling_rate = &cs_tuners->sampling_rate;
		ignore_nice = cs_tuners->ignore_nice;
	} else {
		od_dbs_info = dbs_data->get_cpu_dbs_info_s(cpu);
		sampling_rate = &od_tuners->sampling_rate;
		ignore_nice = od_tuners->ignore_nice;
	}

	switch (event) {
	case CPUFREQ_GOV_START:
		if ((!cpu_online(cpu)) || (!policy->cur))
			return -EINVAL;

		mutex_lock(&dbs_data->mutex);

		dbs_data->enable++;
		cpu_cdbs->cpu = cpu;
		for_each_cpu(j, policy->cpus) {
			struct cpu_dbs_common_info *j_cdbs;
			j_cdbs = dbs_data->get_cpu_cdbs(j);

			j_cdbs->cur_policy = policy;
			j_cdbs->prev_cpu_idle = get_cpu_idle_time(j,
					&j_cdbs->prev_cpu_wall);
			if (ignore_nice)
				j_cdbs->prev_cpu_nice =
					kcpustat_cpu(j).cpustat[CPUTIME_NICE];

			mutex_init(&j_cdbs->timer_mutex);
			INIT_DEFERRABLE_WORK(&j_cdbs->work,
					     dbs_data->gov_dbs_timer);
		}

		/*
		 * Start the timerschedule work, when this governor is used for
		 * first time
		 */
		if (dbs_data->enable != 1)
			goto second_time;

		rc = sysfs_create_group(cpufreq_global_kobject,
				dbs_data->attr_group);
		if (rc) {
			mutex_unlock(&dbs_data->mutex);
			return rc;
		}

		/* policy latency is in nS. Convert it to uS first */
		latency = policy->cpuinfo.transition_latency / 1000;
		if (latency == 0)
			latency = 1;

		/*
		 * conservative does not implement micro like ondemand
		 * governor, thus we are bound to jiffes/HZ
		 */
		if (dbs_data->governor == GOV_CONSERVATIVE) {
			struct cs_ops *ops = dbs_data->gov_ops;

			cpufreq_register_notifier(ops->notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);

			dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
				jiffies_to_usecs(10);
		} else {
			struct od_ops *ops = dbs_data->gov_ops;

			od_tuners->io_is_busy = ops->io_busy();
		}

		/* Bring kernel and HW constraints together */
		dbs_data->min_sampling_rate = max(dbs_data->min_sampling_rate,
				MIN_LATENCY_MULTIPLIER * latency);
		*sampling_rate = max(dbs_data->min_sampling_rate, latency *
				LATENCY_MULTIPLIER);

second_time:
		if (dbs_data->governor == GOV_CONSERVATIVE) {
			cs_dbs_info->down_skip = 0;
			cs_dbs_info->enable = 1;
			cs_dbs_info->requested_freq = policy->cur;
		} else {
			struct od_ops *ops = dbs_data->gov_ops;
			od_dbs_info->rate_mult = 1;
			od_dbs_info->sample_type = OD_NORMAL_SAMPLE;
			ops->powersave_bias_init_cpu(cpu);
		}
		mutex_unlock(&dbs_data->mutex);

		if (dbs_sw_coordinated_cpus(cpu_cdbs)) {
			/* Initiate timer time stamp */
			cpu_cdbs->time_stamp = ktime_get();

			for_each_cpu(j, policy->cpus) {
				struct cpu_dbs_common_info *j_cdbs;

				j_cdbs = dbs_data->get_cpu_cdbs(j);
				dbs_timer_init(dbs_data, j_cdbs,
					       *sampling_rate, j);
			}
		} else {
			dbs_timer_init(dbs_data, cpu_cdbs, *sampling_rate, cpu);
		}
		break;

	case CPUFREQ_GOV_STOP:
		if (dbs_data->governor == GOV_CONSERVATIVE)
			cs_dbs_info->enable = 0;

		if (dbs_sw_coordinated_cpus(cpu_cdbs)) {
			for_each_cpu(j, policy->cpus) {
				struct cpu_dbs_common_info *j_cdbs;

				j_cdbs = dbs_data->get_cpu_cdbs(j);
				dbs_timer_exit(j_cdbs);
			}
		} else {
			dbs_timer_exit(cpu_cdbs);
		}

		mutex_lock(&dbs_data->mutex);
		mutex_destroy(&cpu_cdbs->timer_mutex);
		dbs_data->enable--;
		if (!dbs_data->enable) {
			struct cs_ops *ops = dbs_data->gov_ops;

			sysfs_remove_group(cpufreq_global_kobject,
					dbs_data->attr_group);
			if (dbs_data->governor == GOV_CONSERVATIVE)
				cpufreq_unregister_notifier(ops->notifier_block,
						CPUFREQ_TRANSITION_NOTIFIER);
		}
		mutex_unlock(&dbs_data->mutex);

		break;

	case CPUFREQ_GOV_LIMITS:
		mutex_lock(&cpu_cdbs->timer_mutex);
		if (policy->max < cpu_cdbs->cur_policy->cur)
			__cpufreq_driver_target(cpu_cdbs->cur_policy,
					policy->max, CPUFREQ_RELATION_H);
		else if (policy->min > cpu_cdbs->cur_policy->cur)
			__cpufreq_driver_target(cpu_cdbs->cur_policy,
					policy->min, CPUFREQ_RELATION_L);
		dbs_check_cpu(dbs_data, cpu);
		mutex_unlock(&cpu_cdbs->timer_mutex);
		break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_governor_dbs);
