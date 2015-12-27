/*
 * drivers/cpufreq/cpufreq_governor.h
 *
 * Header file for CPUFreq governors common code
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

#ifndef _CPUFREQ_GOVERNOR_H
#define _CPUFREQ_GOVERNOR_H

#include <linux/cpufreq.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/mutex.h>

/*
 * The polling frequency depends on the capability of the processor. Default
 * polling frequency is 1000 times the transition latency of the processor. The
 * governor will work on any processor with transition latency <= 10ms, using
 * appropriate sampling rate.
 *
 * For CPUs with transition latency > 10ms (mostly drivers with CPUFREQ_ETERNAL)
 * this governor will not work. All times here are in us (micro seconds).
 */
#define MIN_SAMPLING_RATE_RATIO			(2)
#define LATENCY_MULTIPLIER			(1000)
#define MIN_LATENCY_MULTIPLIER			(20)
#define TRANSITION_LATENCY_LIMIT		(10 * 1000 * 1000)

/* Ondemand Sampling types */
enum {OD_NORMAL_SAMPLE, OD_SUB_SAMPLE};

/*
 * Macro for creating governors sysfs routines
 *
 * - gov_sys: One governor instance per whole system
 * - gov_pol: One governor instance per policy
 */

/* Create attributes */
#define gov_sys_attr_ro(_name)						\
static struct global_attr _name##_gov_sys =				\
__ATTR(_name, 0444, show_##_name##_gov_sys, NULL)

#define gov_sys_attr_rw(_name)						\
static struct global_attr _name##_gov_sys =				\
__ATTR(_name, 0644, show_##_name##_gov_sys, store_##_name##_gov_sys)

#define gov_pol_attr_ro(_name)						\
static struct freq_attr _name##_gov_pol =				\
__ATTR(_name, 0444, show_##_name##_gov_pol, NULL)

#define gov_pol_attr_rw(_name)						\
static struct freq_attr _name##_gov_pol =				\
__ATTR(_name, 0644, show_##_name##_gov_pol, store_##_name##_gov_pol)

#define gov_sys_pol_attr_rw(_name)					\
	gov_sys_attr_rw(_name);						\
	gov_pol_attr_rw(_name)

#define gov_sys_pol_attr_ro(_name)					\
	gov_sys_attr_ro(_name);						\
	gov_pol_attr_ro(_name)

/* Create show/store routines */
#define show_one(_gov, file_name)					\
static ssize_t show_##file_name##_gov_sys				\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	struct _gov##_dbs_tuners *tuners = _gov##_dbs_cdata.gdbs_data->tuners; \
	return sprintf(buf, "%u\n", tuners->file_name);			\
}									\
									\
static ssize_t show_##file_name##_gov_pol				\
(struct cpufreq_policy *policy, char *buf)				\
{									\
	struct dbs_data *dbs_data = policy->governor_data;		\
	struct _gov##_dbs_tuners *tuners = dbs_data->tuners;		\
	return sprintf(buf, "%u\n", tuners->file_name);			\
}

#define store_one(_gov, file_name)					\
static ssize_t store_##file_name##_gov_sys				\
(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count) \
{									\
	struct dbs_data *dbs_data = _gov##_dbs_cdata.gdbs_data;		\
	return store_##file_name(dbs_data, buf, count);			\
}									\
									\
static ssize_t store_##file_name##_gov_pol				\
(struct cpufreq_policy *policy, const char *buf, size_t count)		\
{									\
	struct dbs_data *dbs_data = policy->governor_data;		\
	return store_##file_name(dbs_data, buf, count);			\
}

#define show_store_one(_gov, file_name)					\
show_one(_gov, file_name);						\
store_one(_gov, file_name)

/* create helper routines */
#define define_get_cpu_dbs_routines(_dbs_info)				\
static struct cpu_dbs_info *get_cpu_cdbs(int cpu)			\
{									\
	return &per_cpu(_dbs_info, cpu).cdbs;				\
}									\
									\
static void *get_cpu_dbs_info_s(int cpu)				\
{									\
	return &per_cpu(_dbs_info, cpu);				\
}

/*
 * Abbreviations:
 * dbs: used as a shortform for demand based switching It helps to keep variable
 *	names smaller, simpler
 * cdbs: common dbs
 * od_*: On-demand governor
 * cs_*: Conservative governor
 */

/* Common to all CPUs of a policy */
struct cpu_common_dbs_info {
	struct cpufreq_policy *policy;
	/*
	 * percpu mutex that serializes governor limit change with dbs_timer
	 * invocation. We do not want dbs_timer to run when user is changing
	 * the governor or limits.
	 */
	struct mutex timer_mutex;
	ktime_t time_stamp;
};

/* Per cpu structures */
struct cpu_dbs_info {
	u64 prev_cpu_idle;
	u64 prev_cpu_wall;
	u64 prev_cpu_nice;
	/*
	 * Used to keep track of load in the previous interval. However, when
	 * explicitly set to zero, it is used as a flag to ensure that we copy
	 * the previous load to the current interval only once, upon the first
	 * wake-up from idle.
	 */
	unsigned int prev_load;
	struct delayed_work dwork;
	struct cpu_common_dbs_info *shared;
};

struct od_cpu_dbs_info_s {
	struct cpu_dbs_info cdbs;
	struct cpufreq_frequency_table *freq_table;
	unsigned int freq_lo;
	unsigned int freq_lo_jiffies;
	unsigned int freq_hi_jiffies;
	unsigned int rate_mult;
	unsigned int sample_type:1;
};

struct cs_cpu_dbs_info_s {
	struct cpu_dbs_info cdbs;
	unsigned int down_skip;
	unsigned int requested_freq;
};

/* Per policy Governors sysfs tunables */
struct od_dbs_tuners {
	unsigned int ignore_nice_load;
	unsigned int sampling_rate;
	unsigned int sampling_down_factor;
	unsigned int up_threshold;
	unsigned int powersave_bias;
	unsigned int io_is_busy;
};

struct cs_dbs_tuners {
	unsigned int ignore_nice_load;
	unsigned int sampling_rate;
	unsigned int sampling_down_factor;
	unsigned int up_threshold;
	unsigned int down_threshold;
	unsigned int freq_step;
};

/* Common Governor data across policies */
struct dbs_data;
struct common_dbs_data {
	/* Common across governors */
	#define GOV_ONDEMAND		0
	#define GOV_CONSERVATIVE	1
	int governor;
	struct attribute_group *attr_group_gov_sys; /* one governor - system */
	struct attribute_group *attr_group_gov_pol; /* one governor - policy */

	/*
	 * Common data for platforms that don't set
	 * CPUFREQ_HAVE_GOVERNOR_PER_POLICY
	 */
	struct dbs_data *gdbs_data;

	struct cpu_dbs_info *(*get_cpu_cdbs)(int cpu);
	void *(*get_cpu_dbs_info_s)(int cpu);
	unsigned int (*gov_dbs_timer)(struct cpu_dbs_info *cdbs,
				      struct dbs_data *dbs_data,
				      bool modify_all);
	void (*gov_check_cpu)(int cpu, unsigned int load);
	int (*init)(struct dbs_data *dbs_data, bool notify);
	void (*exit)(struct dbs_data *dbs_data, bool notify);

	/* Governor specific ops, see below */
	void *gov_ops;

	/*
	 * Protects governor's data (struct dbs_data and struct common_dbs_data)
	 */
	struct mutex mutex;
};

/* Governor Per policy data */
struct dbs_data {
	struct common_dbs_data *cdata;
	unsigned int min_sampling_rate;
	int usage_count;
	void *tuners;
};

/* Governor specific ops, will be passed to dbs_data->gov_ops */
struct od_ops {
	void (*powersave_bias_init_cpu)(int cpu);
	unsigned int (*powersave_bias_target)(struct cpufreq_policy *policy,
			unsigned int freq_next, unsigned int relation);
	void (*freq_increase)(struct cpufreq_policy *policy, unsigned int freq);
};

static inline int delay_for_sampling_rate(unsigned int sampling_rate)
{
	int delay = usecs_to_jiffies(sampling_rate);

	/* We want all CPUs to do sampling nearly on same jiffy */
	if (num_online_cpus() > 1)
		delay -= jiffies % delay;

	return delay;
}

#define declare_show_sampling_rate_min(_gov)				\
static ssize_t show_sampling_rate_min_gov_sys				\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	struct dbs_data *dbs_data = _gov##_dbs_cdata.gdbs_data;		\
	return sprintf(buf, "%u\n", dbs_data->min_sampling_rate);	\
}									\
									\
static ssize_t show_sampling_rate_min_gov_pol				\
(struct cpufreq_policy *policy, char *buf)				\
{									\
	struct dbs_data *dbs_data = policy->governor_data;		\
	return sprintf(buf, "%u\n", dbs_data->min_sampling_rate);	\
}

extern struct mutex cpufreq_governor_lock;

void dbs_check_cpu(struct dbs_data *dbs_data, int cpu);
int cpufreq_governor_dbs(struct cpufreq_policy *policy,
		struct common_dbs_data *cdata, unsigned int event);
void gov_queue_work(struct dbs_data *dbs_data, struct cpufreq_policy *policy,
		unsigned int delay, bool all_cpus);
void od_register_powersave_bias_handler(unsigned int (*f)
		(struct cpufreq_policy *, unsigned int, unsigned int),
		unsigned int powersave_bias);
void od_unregister_powersave_bias_handler(void);
#endif /* _CPUFREQ_GOVERNOR_H */
