/*
 *  drivers/cpufreq/cpufreq_conservative.c
 *
 *  Copyright (C)  2001 Russell King
 *            (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *                      Jun Nakajima <jun.nakajima@intel.com>
 *            (C)  2009 Alexander Clouter <alex@digriz.org.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/percpu-defs.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include "cpufreq_governor.h"

/* Conservative governor macors */
#define DEF_FREQUENCY_UP_THRESHOLD		(80)
#define DEF_FREQUENCY_DOWN_THRESHOLD		(20)
#define DEF_SAMPLING_DOWN_FACTOR		(1)
#define MAX_SAMPLING_DOWN_FACTOR		(10)

static struct dbs_data cs_dbs_data;
static DEFINE_PER_CPU(struct cs_cpu_dbs_info_s, cs_cpu_dbs_info);

static struct cs_dbs_tuners cs_tuners = {
	.up_threshold = DEF_FREQUENCY_UP_THRESHOLD,
	.down_threshold = DEF_FREQUENCY_DOWN_THRESHOLD,
	.sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR,
	.ignore_nice = 0,
	.freq_step = 5,
};

/*
 * Every sampling_rate, we check, if current idle time is less than 20%
 * (default), then we try to increase frequency Every sampling_rate *
 * sampling_down_factor, we check, if current idle time is more than 80%, then
 * we try to decrease frequency
 *
 * Any frequency increase takes it to the maximum frequency. Frequency reduction
 * happens at minimum steps of 5% (default) of maximum frequency
 */
static void cs_check_cpu(int cpu, unsigned int load)
{
	struct cs_cpu_dbs_info_s *dbs_info = &per_cpu(cs_cpu_dbs_info, cpu);
	struct cpufreq_policy *policy = dbs_info->cdbs.cur_policy;
	unsigned int freq_target;

	/*
	 * break out if we 'cannot' reduce the speed as the user might
	 * want freq_step to be zero
	 */
	if (cs_tuners.freq_step == 0)
		return;

	/* Check for frequency increase */
	if (load > cs_tuners.up_threshold) {
		dbs_info->down_skip = 0;

		/* if we are already at full speed then break out early */
		if (dbs_info->requested_freq == policy->max)
			return;

		freq_target = (cs_tuners.freq_step * policy->max) / 100;

		/* max freq cannot be less than 100. But who knows.... */
		if (unlikely(freq_target == 0))
			freq_target = 5;

		dbs_info->requested_freq += freq_target;
		if (dbs_info->requested_freq > policy->max)
			dbs_info->requested_freq = policy->max;

		__cpufreq_driver_target(policy, dbs_info->requested_freq,
			CPUFREQ_RELATION_H);
		return;
	}

	/*
	 * The optimal frequency is the frequency that is the lowest that can
	 * support the current CPU usage without triggering the up policy. To be
	 * safe, we focus 10 points under the threshold.
	 */
	if (load < (cs_tuners.down_threshold - 10)) {
		freq_target = (cs_tuners.freq_step * policy->max) / 100;

		dbs_info->requested_freq -= freq_target;
		if (dbs_info->requested_freq < policy->min)
			dbs_info->requested_freq = policy->min;

		/*
		 * if we cannot reduce the frequency anymore, break out early
		 */
		if (policy->cur == policy->min)
			return;

		__cpufreq_driver_target(policy, dbs_info->requested_freq,
				CPUFREQ_RELATION_H);
		return;
	}
}

static void cs_dbs_timer(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct cs_cpu_dbs_info_s *dbs_info = container_of(work,
			struct cs_cpu_dbs_info_s, cdbs.work.work);
	unsigned int cpu = dbs_info->cdbs.cur_policy->cpu;
	struct cs_cpu_dbs_info_s *core_dbs_info = &per_cpu(cs_cpu_dbs_info,
			cpu);
	int delay = delay_for_sampling_rate(cs_tuners.sampling_rate);

	mutex_lock(&core_dbs_info->cdbs.timer_mutex);
	if (need_load_eval(&core_dbs_info->cdbs, cs_tuners.sampling_rate))
		dbs_check_cpu(&cs_dbs_data, cpu);

	schedule_delayed_work_on(smp_processor_id(), dw, delay);
	mutex_unlock(&core_dbs_info->cdbs.timer_mutex);
}

static int dbs_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
		void *data)
{
	struct cpufreq_freqs *freq = data;
	struct cs_cpu_dbs_info_s *dbs_info =
					&per_cpu(cs_cpu_dbs_info, freq->cpu);
	struct cpufreq_policy *policy;

	if (!dbs_info->enable)
		return 0;

	policy = dbs_info->cdbs.cur_policy;

	/*
	 * we only care if our internally tracked freq moves outside the 'valid'
	 * ranges of freqency available to us otherwise we do not change it
	*/
	if (dbs_info->requested_freq > policy->max
			|| dbs_info->requested_freq < policy->min)
		dbs_info->requested_freq = freq->new;

	return 0;
}

/************************** sysfs interface ************************/
static ssize_t show_sampling_rate_min(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", cs_dbs_data.min_sampling_rate);
}

static ssize_t store_sampling_down_factor(struct kobject *a,
					  struct attribute *b,
					  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_SAMPLING_DOWN_FACTOR || input < 1)
		return -EINVAL;

	cs_tuners.sampling_down_factor = input;
	return count;
}

static ssize_t store_sampling_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	cs_tuners.sampling_rate = max(input, cs_dbs_data.min_sampling_rate);
	return count;
}

static ssize_t store_up_threshold(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > 100 || input <= cs_tuners.down_threshold)
		return -EINVAL;

	cs_tuners.up_threshold = input;
	return count;
}

static ssize_t store_down_threshold(struct kobject *a, struct attribute *b,
				    const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	/* cannot be lower than 11 otherwise freq will not fall */
	if (ret != 1 || input < 11 || input > 100 ||
			input >= cs_tuners.up_threshold)
		return -EINVAL;

	cs_tuners.down_threshold = input;
	return count;
}

static ssize_t store_ignore_nice_load(struct kobject *a, struct attribute *b,
				      const char *buf, size_t count)
{
	unsigned int input, j;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	if (input == cs_tuners.ignore_nice) /* nothing to do */
		return count;

	cs_tuners.ignore_nice = input;

	/* we need to re-evaluate prev_cpu_idle */
	for_each_online_cpu(j) {
		struct cs_cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(cs_cpu_dbs_info, j);
		dbs_info->cdbs.prev_cpu_idle = get_cpu_idle_time(j,
						&dbs_info->cdbs.prev_cpu_wall);
		if (cs_tuners.ignore_nice)
			dbs_info->cdbs.prev_cpu_nice =
				kcpustat_cpu(j).cpustat[CPUTIME_NICE];
	}
	return count;
}

static ssize_t store_freq_step(struct kobject *a, struct attribute *b,
			       const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	if (input > 100)
		input = 100;

	/*
	 * no need to test here if freq_step is zero as the user might actually
	 * want this, they would be crazy though :)
	 */
	cs_tuners.freq_step = input;
	return count;
}

show_one(cs, sampling_rate, sampling_rate);
show_one(cs, sampling_down_factor, sampling_down_factor);
show_one(cs, up_threshold, up_threshold);
show_one(cs, down_threshold, down_threshold);
show_one(cs, ignore_nice_load, ignore_nice);
show_one(cs, freq_step, freq_step);

define_one_global_rw(sampling_rate);
define_one_global_rw(sampling_down_factor);
define_one_global_rw(up_threshold);
define_one_global_rw(down_threshold);
define_one_global_rw(ignore_nice_load);
define_one_global_rw(freq_step);
define_one_global_ro(sampling_rate_min);

static struct attribute *dbs_attributes[] = {
	&sampling_rate_min.attr,
	&sampling_rate.attr,
	&sampling_down_factor.attr,
	&up_threshold.attr,
	&down_threshold.attr,
	&ignore_nice_load.attr,
	&freq_step.attr,
	NULL
};

static struct attribute_group cs_attr_group = {
	.attrs = dbs_attributes,
	.name = "conservative",
};

/************************** sysfs end ************************/

define_get_cpu_dbs_routines(cs_cpu_dbs_info);

static struct notifier_block cs_cpufreq_notifier_block = {
	.notifier_call = dbs_cpufreq_notifier,
};

static struct cs_ops cs_ops = {
	.notifier_block = &cs_cpufreq_notifier_block,
};

static struct dbs_data cs_dbs_data = {
	.governor = GOV_CONSERVATIVE,
	.attr_group = &cs_attr_group,
	.tuners = &cs_tuners,
	.get_cpu_cdbs = get_cpu_cdbs,
	.get_cpu_dbs_info_s = get_cpu_dbs_info_s,
	.gov_dbs_timer = cs_dbs_timer,
	.gov_check_cpu = cs_check_cpu,
	.gov_ops = &cs_ops,
};

static int cs_cpufreq_governor_dbs(struct cpufreq_policy *policy,
				   unsigned int event)
{
	return cpufreq_governor_dbs(&cs_dbs_data, policy, event);
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_CONSERVATIVE
static
#endif
struct cpufreq_governor cpufreq_gov_conservative = {
	.name			= "conservative",
	.governor		= cs_cpufreq_governor_dbs,
	.max_transition_latency	= TRANSITION_LATENCY_LIMIT,
	.owner			= THIS_MODULE,
};

static int __init cpufreq_gov_dbs_init(void)
{
	mutex_init(&cs_dbs_data.mutex);
	return cpufreq_register_governor(&cpufreq_gov_conservative);
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_conservative);
}

MODULE_AUTHOR("Alexander Clouter <alex@digriz.org.uk>");
MODULE_DESCRIPTION("'cpufreq_conservative' - A dynamic cpufreq governor for "
		"Low Latency Frequency Transition capable processors "
		"optimised for use in a battery environment");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_CONSERVATIVE
fs_initcall(cpufreq_gov_dbs_init);
#else
module_init(cpufreq_gov_dbs_init);
#endif
module_exit(cpufreq_gov_dbs_exit);
