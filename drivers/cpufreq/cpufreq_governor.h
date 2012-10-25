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

#ifndef _CPUFREQ_GOVERNER_H
#define _CPUFREQ_GOVERNER_H

#include <asm/cputime.h>
#include <linux/cpufreq.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>

/*
 * The polling frequency depends on the capability of the processor. Default
 * polling frequency is 1000 times the transition latency of the processor. The
 * governor will work on any processor with transition latency <= 10mS, using
 * appropriate sampling rate.
 *
 * For CPUs with transition latency > 10mS (mostly drivers with CPUFREQ_ETERNAL)
 * this governor will not work. All times here are in uS.
 */
#define MIN_SAMPLING_RATE_RATIO			(2)
#define LATENCY_MULTIPLIER			(1000)
#define MIN_LATENCY_MULTIPLIER			(100)
#define TRANSITION_LATENCY_LIMIT		(10 * 1000 * 1000)

/* Ondemand Sampling types */
enum {OD_NORMAL_SAMPLE, OD_SUB_SAMPLE};

/* Macro creating sysfs show routines */
#define show_one(_gov, file_name, object)				\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	return sprintf(buf, "%u\n", _gov##_tuners.object);		\
}

#define define_get_cpu_dbs_routines(_dbs_info)				\
static struct cpu_dbs_common_info *get_cpu_cdbs(int cpu)		\
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
 * on_*: On-demand governor
 * cs_*: Conservative governor
 */

/* Per cpu structures */
struct cpu_dbs_common_info {
	int cpu;
	cputime64_t prev_cpu_idle;
	cputime64_t prev_cpu_wall;
	cputime64_t prev_cpu_nice;
	struct cpufreq_policy *cur_policy;
	struct delayed_work work;
	/*
	 * percpu mutex that serializes governor limit change with gov_dbs_timer
	 * invocation. We do not want gov_dbs_timer to run when user is changing
	 * the governor or limits.
	 */
	struct mutex timer_mutex;
};

struct od_cpu_dbs_info_s {
	struct cpu_dbs_common_info cdbs;
	cputime64_t prev_cpu_iowait;
	struct cpufreq_frequency_table *freq_table;
	unsigned int freq_lo;
	unsigned int freq_lo_jiffies;
	unsigned int freq_hi_jiffies;
	unsigned int rate_mult;
	unsigned int sample_type:1;
};

struct cs_cpu_dbs_info_s {
	struct cpu_dbs_common_info cdbs;
	unsigned int down_skip;
	unsigned int requested_freq;
	unsigned int enable:1;
};

/* Governers sysfs tunables */
struct od_dbs_tuners {
	unsigned int ignore_nice;
	unsigned int sampling_rate;
	unsigned int sampling_down_factor;
	unsigned int up_threshold;
	unsigned int down_differential;
	unsigned int powersave_bias;
	unsigned int io_is_busy;
};

struct cs_dbs_tuners {
	unsigned int ignore_nice;
	unsigned int sampling_rate;
	unsigned int sampling_down_factor;
	unsigned int up_threshold;
	unsigned int down_threshold;
	unsigned int freq_step;
};

/* Per Governer data */
struct dbs_data {
	/* Common across governors */
	#define GOV_ONDEMAND		0
	#define GOV_CONSERVATIVE	1
	int governor;
	unsigned int min_sampling_rate;
	unsigned int enable; /* number of CPUs using this policy */
	struct attribute_group *attr_group;
	void *tuners;

	/* dbs_mutex protects dbs_enable in governor start/stop */
	struct mutex mutex;

	struct cpu_dbs_common_info *(*get_cpu_cdbs)(int cpu);
	void *(*get_cpu_dbs_info_s)(int cpu);
	void (*gov_dbs_timer)(struct work_struct *work);
	void (*gov_check_cpu)(int cpu, unsigned int load);

	/* Governor specific ops, see below */
	void *gov_ops;
};

/* Governor specific ops, will be passed to dbs_data->gov_ops */
struct od_ops {
	int (*io_busy)(void);
	void (*powersave_bias_init_cpu)(int cpu);
	unsigned int (*powersave_bias_target)(struct cpufreq_policy *policy,
			unsigned int freq_next, unsigned int relation);
	void (*freq_increase)(struct cpufreq_policy *p, unsigned int freq);
};

struct cs_ops {
	struct notifier_block *notifier_block;
};

static inline int delay_for_sampling_rate(unsigned int sampling_rate)
{
	int delay = usecs_to_jiffies(sampling_rate);

	/* We want all CPUs to do sampling nearly on same jiffy */
	if (num_online_cpus() > 1)
		delay -= jiffies % delay;

	return delay;
}

cputime64_t get_cpu_idle_time(unsigned int cpu, cputime64_t *wall);
void dbs_check_cpu(struct dbs_data *dbs_data, int cpu);
int cpufreq_governor_dbs(struct dbs_data *dbs_data,
		struct cpufreq_policy *policy, unsigned int event);
#endif /* _CPUFREQ_GOVERNER_H */
