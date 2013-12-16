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

#include <linux/slab.h>
#include "cpufreq_governor.h"

/* Conservative governor macros */
#define DEF_FREQUENCY_UP_THRESHOLD		(80)
#define DEF_FREQUENCY_DOWN_THRESHOLD		(20)
#define DEF_FREQUENCY_STEP			(5)
#define DEF_SAMPLING_DOWN_FACTOR		(1)
#define MAX_SAMPLING_DOWN_FACTOR		(10)

static DEFINE_PER_CPU(struct cs_cpu_dbs_info_s, cs_cpu_dbs_info);

static inline unsigned int get_freq_target(struct cs_dbs_tuners *cs_tuners,
					   struct cpufreq_policy *policy)
{
	unsigned int freq_target = (cs_tuners->freq_step * policy->max) / 100;

	/* max freq cannot be less than 100. But who knows... */
	if (unlikely(freq_target == 0))
		freq_target = DEF_FREQUENCY_STEP;

	return freq_target;
}

/*
 * Every sampling_rate, we check, if current idle time is less than 20%
 * (default), then we try to increase frequency. Every sampling_rate *
 * sampling_down_factor, we check, if current idle time is more than 80%
 * (default), then we try to decrease frequency
 *
 * Any frequency increase takes it to the maximum frequency. Frequency reduction
 * happens at minimum steps of 5% (default) of maximum frequency
 */
static void cs_check_cpu(int cpu, unsigned int load)
{
	struct cs_cpu_dbs_info_s *dbs_info = &per_cpu(cs_cpu_dbs_info, cpu);
	struct cpufreq_policy *policy = dbs_info->cdbs.cur_policy;
	struct dbs_data *dbs_data = policy->governor_data;
	struct cs_dbs_tuners *cs_tuners = dbs_data->tuners;

	/*
	 * break out if we 'cannot' reduce the speed as the user might
	 * want freq_step to be zero
	 */
	if (cs_tuners->freq_step == 0)
		return;

	/* Check for frequency increase */
	if (load > cs_tuners->up_threshold) {
		dbs_info->down_skip = 0;

		/* if we are already at full speed then break out early */
		if (dbs_info->requested_freq == policy->max)
			return;

		dbs_info->requested_freq += get_freq_target(cs_tuners, policy);

		__cpufreq_driver_target(policy, dbs_info->requested_freq,
			CPUFREQ_RELATION_H);
		return;
	}

	/* if sampling_down_factor is active break out early */
	if (++dbs_info->down_skip < cs_tuners->sampling_down_factor)
		return;
	dbs_info->down_skip = 0;

	/* Check for frequency decrease */
	if (load < cs_tuners->down_threshold) {
		unsigned int freq_target;
		/*
		 * if we cannot reduce the frequency anymore, break out early
		 */
		if (policy->cur == policy->min)
			return;

		freq_target = get_freq_target(cs_tuners, policy);
		if (dbs_info->requested_freq > freq_target)
			dbs_info->requested_freq -= freq_target;
		else
			dbs_info->requested_freq = policy->min;

		__cpufreq_driver_target(policy, dbs_info->requested_freq,
				CPUFREQ_RELATION_L);
		return;
	}
}

static void cs_dbs_timer(struct work_struct *work)
{
	struct cs_cpu_dbs_info_s *dbs_info = container_of(work,
			struct cs_cpu_dbs_info_s, cdbs.work.work);
	unsigned int cpu = dbs_info->cdbs.cur_policy->cpu;
	struct cs_cpu_dbs_info_s *core_dbs_info = &per_cpu(cs_cpu_dbs_info,
			cpu);
	struct dbs_data *dbs_data = dbs_info->cdbs.cur_policy->governor_data;
	struct cs_dbs_tuners *cs_tuners = dbs_data->tuners;
	int delay = delay_for_sampling_rate(cs_tuners->sampling_rate);
	bool modify_all = true;

	mutex_lock(&core_dbs_info->cdbs.timer_mutex);
	if (!need_load_eval(&core_dbs_info->cdbs, cs_tuners->sampling_rate))
		modify_all = false;
	else
		dbs_check_cpu(dbs_data, cpu);

	gov_queue_work(dbs_data, dbs_info->cdbs.cur_policy, delay, modify_all);
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
	 * ranges of frequency available to us otherwise we do not change it
	*/
	if (dbs_info->requested_freq > policy->max
			|| dbs_info->requested_freq < policy->min)
		dbs_info->requested_freq = freq->new;

	return 0;
}

/************************** sysfs interface ************************/
static struct common_dbs_data cs_dbs_cdata;

static ssize_t store_sampling_down_factor(struct dbs_data *dbs_data,
		const char *buf, size_t count)
{
	struct cs_dbs_tuners *cs_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_SAMPLING_DOWN_FACTOR || input < 1)
		return -EINVAL;

	cs_tuners->sampling_down_factor = input;
	return count;
}

static ssize_t store_sampling_rate(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	struct cs_dbs_tuners *cs_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	cs_tuners->sampling_rate = max(input, dbs_data->min_sampling_rate);
	return count;
}

static ssize_t store_up_threshold(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	struct cs_dbs_tuners *cs_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > 100 || input <= cs_tuners->down_threshold)
		return -EINVAL;

	cs_tuners->up_threshold = input;
	return count;
}

static ssize_t store_down_threshold(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	struct cs_dbs_tuners *cs_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	/* cannot be lower than 11 otherwise freq will not fall */
	if (ret != 1 || input < 11 || input > 100 ||
			input >= cs_tuners->up_threshold)
		return -EINVAL;

	cs_tuners->down_threshold = input;
	return count;
}

static ssize_t store_ignore_nice_load(struct dbs_data *dbs_data,
		const char *buf, size_t count)
{
	struct cs_dbs_tuners *cs_tuners = dbs_data->tuners;
	unsigned int input, j;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	if (input == cs_tuners->ignore_nice_load) /* nothing to do */
		return count;

	cs_tuners->ignore_nice_load = input;

	/* we need to re-evaluate prev_cpu_idle */
	for_each_online_cpu(j) {
		struct cs_cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(cs_cpu_dbs_info, j);
		dbs_info->cdbs.prev_cpu_idle = get_cpu_idle_time(j,
					&dbs_info->cdbs.prev_cpu_wall, 0);
		if (cs_tuners->ignore_nice_load)
			dbs_info->cdbs.prev_cpu_nice =
				kcpustat_cpu(j).cpustat[CPUTIME_NICE];
	}
	return count;
}

static ssize_t store_freq_step(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	struct cs_dbs_tuners *cs_tuners = dbs_data->tuners;
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
	cs_tuners->freq_step = input;
	return count;
}

show_store_one(cs, sampling_rate);
show_store_one(cs, sampling_down_factor);
show_store_one(cs, up_threshold);
show_store_one(cs, down_threshold);
show_store_one(cs, ignore_nice_load);
show_store_one(cs, freq_step);
declare_show_sampling_rate_min(cs);

gov_sys_pol_attr_rw(sampling_rate);
gov_sys_pol_attr_rw(sampling_down_factor);
gov_sys_pol_attr_rw(up_threshold);
gov_sys_pol_attr_rw(down_threshold);
gov_sys_pol_attr_rw(ignore_nice_load);
gov_sys_pol_attr_rw(freq_step);
gov_sys_pol_attr_ro(sampling_rate_min);

static struct attribute *dbs_attributes_gov_sys[] = {
	&sampling_rate_min_gov_sys.attr,
	&sampling_rate_gov_sys.attr,
	&sampling_down_factor_gov_sys.attr,
	&up_threshold_gov_sys.attr,
	&down_threshold_gov_sys.attr,
	&ignore_nice_load_gov_sys.attr,
	&freq_step_gov_sys.attr,
	NULL
};

static struct attribute_group cs_attr_group_gov_sys = {
	.attrs = dbs_attributes_gov_sys,
	.name = "conservative",
};

static struct attribute *dbs_attributes_gov_pol[] = {
	&sampling_rate_min_gov_pol.attr,
	&sampling_rate_gov_pol.attr,
	&sampling_down_factor_gov_pol.attr,
	&up_threshold_gov_pol.attr,
	&down_threshold_gov_pol.attr,
	&ignore_nice_load_gov_pol.attr,
	&freq_step_gov_pol.attr,
	NULL
};

static struct attribute_group cs_attr_group_gov_pol = {
	.attrs = dbs_attributes_gov_pol,
	.name = "conservative",
};

/************************** sysfs end ************************/

static int cs_init(struct dbs_data *dbs_data)
{
	struct cs_dbs_tuners *tuners;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners) {
		pr_err("%s: kzalloc failed\n", __func__);
		return -ENOMEM;
	}

	tuners->up_threshold = DEF_FREQUENCY_UP_THRESHOLD;
	tuners->down_threshold = DEF_FREQUENCY_DOWN_THRESHOLD;
	tuners->sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR;
	tuners->ignore_nice_load = 0;
	tuners->freq_step = DEF_FREQUENCY_STEP;

	dbs_data->tuners = tuners;
	dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
		jiffies_to_usecs(10);
	mutex_init(&dbs_data->mutex);
	return 0;
}

static void cs_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}

define_get_cpu_dbs_routines(cs_cpu_dbs_info);

static struct notifier_block cs_cpufreq_notifier_block = {
	.notifier_call = dbs_cpufreq_notifier,
};

static struct cs_ops cs_ops = {
	.notifier_block = &cs_cpufreq_notifier_block,
};

static struct common_dbs_data cs_dbs_cdata = {
	.governor = GOV_CONSERVATIVE,
	.attr_group_gov_sys = &cs_attr_group_gov_sys,
	.attr_group_gov_pol = &cs_attr_group_gov_pol,
	.get_cpu_cdbs = get_cpu_cdbs,
	.get_cpu_dbs_info_s = get_cpu_dbs_info_s,
	.gov_dbs_timer = cs_dbs_timer,
	.gov_check_cpu = cs_check_cpu,
	.gov_ops = &cs_ops,
	.init = cs_init,
	.exit = cs_exit,
};

static int cs_cpufreq_governor_dbs(struct cpufreq_policy *policy,
				   unsigned int event)
{
	return cpufreq_governor_dbs(policy, &cs_dbs_cdata, event);
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
