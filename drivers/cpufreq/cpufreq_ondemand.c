/*
 *  drivers/cpufreq/cpufreq_ondemand.c
 *
 *  Copyright (C)  2001 Russell King
 *            (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *                      Jun Nakajima <jun.nakajima@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpu.h>
#include <linux/percpu-defs.h>
#include <linux/slab.h>
#include <linux/tick.h>

#include "cpufreq_ondemand.h"

/* On-demand governor macros */
#define DEF_FREQUENCY_UP_THRESHOLD		(80)
#define DEF_SAMPLING_DOWN_FACTOR		(1)
#define MAX_SAMPLING_DOWN_FACTOR		(100000)
#define MICRO_FREQUENCY_UP_THRESHOLD		(95)
#define MICRO_FREQUENCY_MIN_SAMPLE_RATE		(10000)
#define MIN_FREQUENCY_UP_THRESHOLD		(11)
#define MAX_FREQUENCY_UP_THRESHOLD		(100)

static struct od_ops od_ops;

static unsigned int default_powersave_bias;

/*
 * Not all CPUs want IO time to be accounted as busy; this depends on how
 * efficient idling at a higher frequency/voltage is.
 * Pavel Machek says this is not so for various generations of AMD and old
 * Intel systems.
 * Mike Chan (android.com) claims this is also not true for ARM.
 * Because of this, whitelist specific known (series) of CPUs by default, and
 * leave all others up to the user.
 */
static int should_io_be_busy(void)
{
#if defined(CONFIG_X86)
	/*
	 * For Intel, Core 2 (model 15) and later have an efficient idle.
	 */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
			boot_cpu_data.x86 == 6 &&
			boot_cpu_data.x86_model >= 15)
		return 1;
#endif
	return 0;
}

/*
 * Find right freq to be set now with powersave_bias on.
 * Returns the freq_hi to be used right now and will set freq_hi_delay_us,
 * freq_lo, and freq_lo_delay_us in percpu area for averaging freqs.
 */
static unsigned int generic_powersave_bias_target(struct cpufreq_policy *policy,
		unsigned int freq_next, unsigned int relation)
{
	unsigned int freq_req, freq_reduc, freq_avg;
	unsigned int freq_hi, freq_lo;
	unsigned int index;
	unsigned int delay_hi_us;
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct od_policy_dbs_info *dbs_info = to_dbs_info(policy_dbs);
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	struct cpufreq_frequency_table *freq_table = policy->freq_table;

	if (!freq_table) {
		dbs_info->freq_lo = 0;
		dbs_info->freq_lo_delay_us = 0;
		return freq_next;
	}

	index = cpufreq_frequency_table_target(policy, freq_next, relation);
	freq_req = freq_table[index].frequency;
	freq_reduc = freq_req * od_tuners->powersave_bias / 1000;
	freq_avg = freq_req - freq_reduc;

	/* Find freq bounds for freq_avg in freq_table */
	index = cpufreq_table_find_index_h(policy, freq_avg);
	freq_lo = freq_table[index].frequency;
	index = cpufreq_table_find_index_l(policy, freq_avg);
	freq_hi = freq_table[index].frequency;

	/* Find out how long we have to be in hi and lo freqs */
	if (freq_hi == freq_lo) {
		dbs_info->freq_lo = 0;
		dbs_info->freq_lo_delay_us = 0;
		return freq_lo;
	}
	delay_hi_us = (freq_avg - freq_lo) * dbs_data->sampling_rate;
	delay_hi_us += (freq_hi - freq_lo) / 2;
	delay_hi_us /= freq_hi - freq_lo;
	dbs_info->freq_hi_delay_us = delay_hi_us;
	dbs_info->freq_lo = freq_lo;
	dbs_info->freq_lo_delay_us = dbs_data->sampling_rate - delay_hi_us;
	return freq_hi;
}

static void ondemand_powersave_bias_init(struct cpufreq_policy *policy)
{
	struct od_policy_dbs_info *dbs_info = to_dbs_info(policy->governor_data);

	dbs_info->freq_lo = 0;
}

static void dbs_freq_increase(struct cpufreq_policy *policy, unsigned int freq)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;

	if (od_tuners->powersave_bias)
		freq = od_ops.powersave_bias_target(policy, freq,
				CPUFREQ_RELATION_H);
	else if (policy->cur == policy->max)
		return;

	__cpufreq_driver_target(policy, freq, od_tuners->powersave_bias ?
			CPUFREQ_RELATION_L : CPUFREQ_RELATION_H);
}

/*
 * Every sampling_rate, we check, if current idle time is less than 20%
 * (default), then we try to increase frequency. Else, we adjust the frequency
 * proportional to load.
 */
static void od_update(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct od_policy_dbs_info *dbs_info = to_dbs_info(policy_dbs);
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	unsigned int load = dbs_update(policy);

	dbs_info->freq_lo = 0;

	/* Check for frequency increase */
	if (load > dbs_data->up_threshold) {
		/* If switching to max speed, apply sampling_down_factor */
		if (policy->cur < policy->max)
			policy_dbs->rate_mult = dbs_data->sampling_down_factor;
		dbs_freq_increase(policy, policy->max);
	} else {
		/* Calculate the next frequency proportional to load */
		unsigned int freq_next, min_f, max_f;

		min_f = policy->cpuinfo.min_freq;
		max_f = policy->cpuinfo.max_freq;
		freq_next = min_f + load * (max_f - min_f) / 100;

		/* No longer fully busy, reset rate_mult */
		policy_dbs->rate_mult = 1;

		if (od_tuners->powersave_bias)
			freq_next = od_ops.powersave_bias_target(policy,
								 freq_next,
								 CPUFREQ_RELATION_L);

		__cpufreq_driver_target(policy, freq_next, CPUFREQ_RELATION_C);
	}
}

static unsigned int od_dbs_timer(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	struct od_policy_dbs_info *dbs_info = to_dbs_info(policy_dbs);
	int sample_type = dbs_info->sample_type;

	/* Common NORMAL_SAMPLE setup */
	dbs_info->sample_type = OD_NORMAL_SAMPLE;
	/*
	 * OD_SUB_SAMPLE doesn't make sense if sample_delay_ns is 0, so ignore
	 * it then.
	 */
	if (sample_type == OD_SUB_SAMPLE && policy_dbs->sample_delay_ns > 0) {
		__cpufreq_driver_target(policy, dbs_info->freq_lo,
					CPUFREQ_RELATION_H);
		return dbs_info->freq_lo_delay_us;
	}

	od_update(policy);

	if (dbs_info->freq_lo) {
		/* Setup timer for SUB_SAMPLE */
		dbs_info->sample_type = OD_SUB_SAMPLE;
		return dbs_info->freq_hi_delay_us;
	}

	return dbs_data->sampling_rate * policy_dbs->rate_mult;
}

/************************** sysfs interface ************************/
static struct dbs_governor od_dbs_gov;

static ssize_t store_io_is_busy(struct gov_attr_set *attr_set, const char *buf,
				size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	dbs_data->io_is_busy = !!input;

	/* we need to re-evaluate prev_cpu_idle */
	gov_update_cpu_data(dbs_data);

	return count;
}

static ssize_t store_up_threshold(struct gov_attr_set *attr_set,
				  const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
			input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}

	dbs_data->up_threshold = input;
	return count;
}

static ssize_t store_sampling_down_factor(struct gov_attr_set *attr_set,
					  const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct policy_dbs_info *policy_dbs;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_SAMPLING_DOWN_FACTOR || input < 1)
		return -EINVAL;

	dbs_data->sampling_down_factor = input;

	/* Reset down sampling multiplier in case it was active */
	list_for_each_entry(policy_dbs, &attr_set->policy_list, list) {
		/*
		 * Doing this without locking might lead to using different
		 * rate_mult values in od_update() and od_dbs_timer().
		 */
		mutex_lock(&policy_dbs->timer_mutex);
		policy_dbs->rate_mult = 1;
		mutex_unlock(&policy_dbs->timer_mutex);
	}

	return count;
}

static ssize_t store_ignore_nice_load(struct gov_attr_set *attr_set,
				      const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	if (input == dbs_data->ignore_nice_load) { /* nothing to do */
		return count;
	}
	dbs_data->ignore_nice_load = input;

	/* we need to re-evaluate prev_cpu_idle */
	gov_update_cpu_data(dbs_data);

	return count;
}

static ssize_t store_powersave_bias(struct gov_attr_set *attr_set,
				    const char *buf, size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	struct policy_dbs_info *policy_dbs;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	if (input > 1000)
		input = 1000;

	od_tuners->powersave_bias = input;

	list_for_each_entry(policy_dbs, &attr_set->policy_list, list)
		ondemand_powersave_bias_init(policy_dbs->policy);

	return count;
}

gov_show_one_common(sampling_rate);
gov_show_one_common(up_threshold);
gov_show_one_common(sampling_down_factor);
gov_show_one_common(ignore_nice_load);
gov_show_one_common(min_sampling_rate);
gov_show_one_common(io_is_busy);
gov_show_one(od, powersave_bias);

gov_attr_rw(sampling_rate);
gov_attr_rw(io_is_busy);
gov_attr_rw(up_threshold);
gov_attr_rw(sampling_down_factor);
gov_attr_rw(ignore_nice_load);
gov_attr_rw(powersave_bias);
gov_attr_ro(min_sampling_rate);

static struct attribute *od_attributes[] = {
	&min_sampling_rate.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&sampling_down_factor.attr,
	&ignore_nice_load.attr,
	&powersave_bias.attr,
	&io_is_busy.attr,
	NULL
};

/************************** sysfs end ************************/

static struct policy_dbs_info *od_alloc(void)
{
	struct od_policy_dbs_info *dbs_info;

	dbs_info = kzalloc(sizeof(*dbs_info), GFP_KERNEL);
	return dbs_info ? &dbs_info->policy_dbs : NULL;
}

static void od_free(struct policy_dbs_info *policy_dbs)
{
	kfree(to_dbs_info(policy_dbs));
}

static int od_init(struct dbs_data *dbs_data)
{
	struct od_dbs_tuners *tuners;
	u64 idle_time;
	int cpu;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners)
		return -ENOMEM;

	cpu = get_cpu();
	idle_time = get_cpu_idle_time_us(cpu, NULL);
	put_cpu();
	if (idle_time != -1ULL) {
		/* Idle micro accounting is supported. Use finer thresholds */
		dbs_data->up_threshold = MICRO_FREQUENCY_UP_THRESHOLD;
		/*
		 * In nohz/micro accounting case we set the minimum frequency
		 * not depending on HZ, but fixed (very low). The deferred
		 * timer might skip some samples if idle/sleeping as needed.
		*/
		dbs_data->min_sampling_rate = MICRO_FREQUENCY_MIN_SAMPLE_RATE;
	} else {
		dbs_data->up_threshold = DEF_FREQUENCY_UP_THRESHOLD;

		/* For correct statistics, we need 10 ticks for each measure */
		dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
			jiffies_to_usecs(10);
	}

	dbs_data->sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR;
	dbs_data->ignore_nice_load = 0;
	tuners->powersave_bias = default_powersave_bias;
	dbs_data->io_is_busy = should_io_be_busy();

	dbs_data->tuners = tuners;
	return 0;
}

static void od_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}

static void od_start(struct cpufreq_policy *policy)
{
	struct od_policy_dbs_info *dbs_info = to_dbs_info(policy->governor_data);

	dbs_info->sample_type = OD_NORMAL_SAMPLE;
	ondemand_powersave_bias_init(policy);
}

static struct od_ops od_ops = {
	.powersave_bias_target = generic_powersave_bias_target,
};

static struct dbs_governor od_dbs_gov = {
	.gov = CPUFREQ_DBS_GOVERNOR_INITIALIZER("ondemand"),
	.kobj_type = { .default_attrs = od_attributes },
	.gov_dbs_timer = od_dbs_timer,
	.alloc = od_alloc,
	.free = od_free,
	.init = od_init,
	.exit = od_exit,
	.start = od_start,
};

#define CPU_FREQ_GOV_ONDEMAND	(&od_dbs_gov.gov)

static void od_set_powersave_bias(unsigned int powersave_bias)
{
	unsigned int cpu;
	cpumask_t done;

	default_powersave_bias = powersave_bias;
	cpumask_clear(&done);

	get_online_cpus();
	for_each_online_cpu(cpu) {
		struct cpufreq_policy *policy;
		struct policy_dbs_info *policy_dbs;
		struct dbs_data *dbs_data;
		struct od_dbs_tuners *od_tuners;

		if (cpumask_test_cpu(cpu, &done))
			continue;

		policy = cpufreq_cpu_get_raw(cpu);
		if (!policy || policy->governor != CPU_FREQ_GOV_ONDEMAND)
			continue;

		policy_dbs = policy->governor_data;
		if (!policy_dbs)
			continue;

		cpumask_or(&done, &done, policy->cpus);

		dbs_data = policy_dbs->dbs_data;
		od_tuners = dbs_data->tuners;
		od_tuners->powersave_bias = default_powersave_bias;
	}
	put_online_cpus();
}

void od_register_powersave_bias_handler(unsigned int (*f)
		(struct cpufreq_policy *, unsigned int, unsigned int),
		unsigned int powersave_bias)
{
	od_ops.powersave_bias_target = f;
	od_set_powersave_bias(powersave_bias);
}
EXPORT_SYMBOL_GPL(od_register_powersave_bias_handler);

void od_unregister_powersave_bias_handler(void)
{
	od_ops.powersave_bias_target = generic_powersave_bias_target;
	od_set_powersave_bias(0);
}
EXPORT_SYMBOL_GPL(od_unregister_powersave_bias_handler);

static int __init cpufreq_gov_dbs_init(void)
{
	return cpufreq_register_governor(CPU_FREQ_GOV_ONDEMAND);
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	cpufreq_unregister_governor(CPU_FREQ_GOV_ONDEMAND);
}

MODULE_AUTHOR("Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>");
MODULE_AUTHOR("Alexey Starikovskiy <alexey.y.starikovskiy@intel.com>");
MODULE_DESCRIPTION("'cpufreq_ondemand' - A dynamic cpufreq governor for "
	"Low Latency Frequency Transition capable processors");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_ONDEMAND
struct cpufreq_governor *cpufreq_default_governor(void)
{
	return CPU_FREQ_GOV_ONDEMAND;
}

fs_initcall(cpufreq_gov_dbs_init);
#else
module_init(cpufreq_gov_dbs_init);
#endif
module_exit(cpufreq_gov_dbs_exit);
