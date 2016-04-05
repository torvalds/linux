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

#include <linux/atomic.h>
#include <linux/irq_work.h>
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
 * Abbreviations:
 * dbs: used as a shortform for demand based switching It helps to keep variable
 *	names smaller, simpler
 * cdbs: common dbs
 * od_*: On-demand governor
 * cs_*: Conservative governor
 */

/* Governor demand based switching data (per-policy or global). */
struct dbs_data {
	int usage_count;
	void *tuners;
	unsigned int min_sampling_rate;
	unsigned int ignore_nice_load;
	unsigned int sampling_rate;
	unsigned int sampling_down_factor;
	unsigned int up_threshold;
	unsigned int io_is_busy;

	struct kobject kobj;
	struct list_head policy_dbs_list;
	/*
	 * Protect concurrent updates to governor tunables from sysfs,
	 * policy_dbs_list and usage_count.
	 */
	struct mutex mutex;
};

/* Governor's specific attributes */
struct dbs_data;
struct governor_attr {
	struct attribute attr;
	ssize_t (*show)(struct dbs_data *dbs_data, char *buf);
	ssize_t (*store)(struct dbs_data *dbs_data, const char *buf,
			 size_t count);
};

#define gov_show_one(_gov, file_name)					\
static ssize_t show_##file_name						\
(struct dbs_data *dbs_data, char *buf)					\
{									\
	struct _gov##_dbs_tuners *tuners = dbs_data->tuners;		\
	return sprintf(buf, "%u\n", tuners->file_name);			\
}

#define gov_show_one_common(file_name)					\
static ssize_t show_##file_name						\
(struct dbs_data *dbs_data, char *buf)					\
{									\
	return sprintf(buf, "%u\n", dbs_data->file_name);		\
}

#define gov_attr_ro(_name)						\
static struct governor_attr _name =					\
__ATTR(_name, 0444, show_##_name, NULL)

#define gov_attr_rw(_name)						\
static struct governor_attr _name =					\
__ATTR(_name, 0644, show_##_name, store_##_name)

/* Common to all CPUs of a policy */
struct policy_dbs_info {
	struct cpufreq_policy *policy;
	/*
	 * Per policy mutex that serializes load evaluation from limit-change
	 * and work-handler.
	 */
	struct mutex timer_mutex;

	u64 last_sample_time;
	s64 sample_delay_ns;
	atomic_t work_count;
	struct irq_work irq_work;
	struct work_struct work;
	/* dbs_data may be shared between multiple policy objects */
	struct dbs_data *dbs_data;
	struct list_head list;
	/* Multiplier for increasing sample delay temporarily. */
	unsigned int rate_mult;
	/* Status indicators */
	bool is_shared;		/* This object is used by multiple CPUs */
	bool work_in_progress;	/* Work is being queued up or in progress */
};

static inline void gov_update_sample_delay(struct policy_dbs_info *policy_dbs,
					   unsigned int delay_us)
{
	policy_dbs->sample_delay_ns = delay_us * NSEC_PER_USEC;
}

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
	struct update_util_data update_util;
	struct policy_dbs_info *policy_dbs;
};

/* Common Governor data across policies */
struct dbs_governor {
	struct cpufreq_governor gov;
	struct kobj_type kobj_type;

	/*
	 * Common data for platforms that don't set
	 * CPUFREQ_HAVE_GOVERNOR_PER_POLICY
	 */
	struct dbs_data *gdbs_data;

	unsigned int (*gov_dbs_timer)(struct cpufreq_policy *policy);
	struct policy_dbs_info *(*alloc)(void);
	void (*free)(struct policy_dbs_info *policy_dbs);
	int (*init)(struct dbs_data *dbs_data, bool notify);
	void (*exit)(struct dbs_data *dbs_data, bool notify);
	void (*start)(struct cpufreq_policy *policy);
};

static inline struct dbs_governor *dbs_governor_of(struct cpufreq_policy *policy)
{
	return container_of(policy->governor, struct dbs_governor, gov);
}

/* Governor specific operations */
struct od_ops {
	unsigned int (*powersave_bias_target)(struct cpufreq_policy *policy,
			unsigned int freq_next, unsigned int relation);
};

unsigned int dbs_update(struct cpufreq_policy *policy);
int cpufreq_governor_dbs(struct cpufreq_policy *policy, unsigned int event);
void od_register_powersave_bias_handler(unsigned int (*f)
		(struct cpufreq_policy *, unsigned int, unsigned int),
		unsigned int powersave_bias);
void od_unregister_powersave_bias_handler(void);
ssize_t store_sampling_rate(struct dbs_data *dbs_data, const char *buf,
			    size_t count);
void gov_update_cpu_data(struct dbs_data *dbs_data);
#endif /* _CPUFREQ_GOVERNOR_H */
