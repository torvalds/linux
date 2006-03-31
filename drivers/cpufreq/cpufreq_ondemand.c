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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <linux/cpufreq.h>
#include <linux/sysctl.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/sched.h>
#include <linux/kmod.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/kernel_stat.h>
#include <linux/percpu.h>
#include <linux/mutex.h>

/*
 * dbs is used in this file as a shortform for demandbased switching
 * It helps to keep variable names smaller, simpler
 */

#define DEF_FREQUENCY_UP_THRESHOLD		(80)
#define MIN_FREQUENCY_UP_THRESHOLD		(11)
#define MAX_FREQUENCY_UP_THRESHOLD		(100)

/*
 * The polling frequency of this governor depends on the capability of
 * the processor. Default polling frequency is 1000 times the transition
 * latency of the processor. The governor will work on any processor with
 * transition latency <= 10mS, using appropriate sampling
 * rate.
 * For CPUs with transition latency > 10mS (mostly drivers with CPUFREQ_ETERNAL)
 * this governor will not work.
 * All times here are in uS.
 */
static unsigned int def_sampling_rate;
#define MIN_SAMPLING_RATE_RATIO			(2)
/* for correct statistics, we need at least 10 ticks between each measure */
#define MIN_STAT_SAMPLING_RATE			(MIN_SAMPLING_RATE_RATIO * jiffies_to_usecs(10))
#define MIN_SAMPLING_RATE			(def_sampling_rate / MIN_SAMPLING_RATE_RATIO)
#define MAX_SAMPLING_RATE			(500 * def_sampling_rate)
#define DEF_SAMPLING_RATE_LATENCY_MULTIPLIER	(1000)
#define DEF_SAMPLING_DOWN_FACTOR		(1)
#define MAX_SAMPLING_DOWN_FACTOR		(10)
#define TRANSITION_LATENCY_LIMIT		(10 * 1000)

static void do_dbs_timer(void *data);

struct cpu_dbs_info_s {
	struct cpufreq_policy *cur_policy;
	unsigned int prev_cpu_idle_up;
	unsigned int prev_cpu_idle_down;
	unsigned int enable;
};
static DEFINE_PER_CPU(struct cpu_dbs_info_s, cpu_dbs_info);

static unsigned int dbs_enable;	/* number of CPUs using this policy */

static DEFINE_MUTEX (dbs_mutex);
static DECLARE_WORK	(dbs_work, do_dbs_timer, NULL);

struct dbs_tuners {
	unsigned int sampling_rate;
	unsigned int sampling_down_factor;
	unsigned int up_threshold;
	unsigned int ignore_nice;
};

static struct dbs_tuners dbs_tuners_ins = {
	.up_threshold = DEF_FREQUENCY_UP_THRESHOLD,
	.sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR,
	.ignore_nice = 0,
};

static inline unsigned int get_cpu_idle_time(unsigned int cpu)
{
	return	kstat_cpu(cpu).cpustat.idle +
		kstat_cpu(cpu).cpustat.iowait +
		( dbs_tuners_ins.ignore_nice ?
		  kstat_cpu(cpu).cpustat.nice :
		  0);
}

/************************** sysfs interface ************************/
static ssize_t show_sampling_rate_max(struct cpufreq_policy *policy, char *buf)
{
	return sprintf (buf, "%u\n", MAX_SAMPLING_RATE);
}

static ssize_t show_sampling_rate_min(struct cpufreq_policy *policy, char *buf)
{
	return sprintf (buf, "%u\n", MIN_SAMPLING_RATE);
}

#define define_one_ro(_name)		\
static struct freq_attr _name =		\
__ATTR(_name, 0444, show_##_name, NULL)

define_one_ro(sampling_rate_max);
define_one_ro(sampling_rate_min);

/* cpufreq_ondemand Governor Tunables */
#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct cpufreq_policy *unused, char *buf)				\
{									\
	return sprintf(buf, "%u\n", dbs_tuners_ins.object);		\
}
show_one(sampling_rate, sampling_rate);
show_one(sampling_down_factor, sampling_down_factor);
show_one(up_threshold, up_threshold);
show_one(ignore_nice_load, ignore_nice);

static ssize_t store_sampling_down_factor(struct cpufreq_policy *unused,
		const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf (buf, "%u", &input);
	if (ret != 1 )
		return -EINVAL;

	if (input > MAX_SAMPLING_DOWN_FACTOR || input < 1)
		return -EINVAL;

	mutex_lock(&dbs_mutex);
	dbs_tuners_ins.sampling_down_factor = input;
	mutex_unlock(&dbs_mutex);

	return count;
}

static ssize_t store_sampling_rate(struct cpufreq_policy *unused,
		const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf (buf, "%u", &input);

	mutex_lock(&dbs_mutex);
	if (ret != 1 || input > MAX_SAMPLING_RATE || input < MIN_SAMPLING_RATE) {
		mutex_unlock(&dbs_mutex);
		return -EINVAL;
	}

	dbs_tuners_ins.sampling_rate = input;
	mutex_unlock(&dbs_mutex);

	return count;
}

static ssize_t store_up_threshold(struct cpufreq_policy *unused,
		const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf (buf, "%u", &input);

	mutex_lock(&dbs_mutex);
	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
			input < MIN_FREQUENCY_UP_THRESHOLD) {
		mutex_unlock(&dbs_mutex);
		return -EINVAL;
	}

	dbs_tuners_ins.up_threshold = input;
	mutex_unlock(&dbs_mutex);

	return count;
}

static ssize_t store_ignore_nice_load(struct cpufreq_policy *policy,
		const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	unsigned int j;

	ret = sscanf (buf, "%u", &input);
	if ( ret != 1 )
		return -EINVAL;

	if ( input > 1 )
		input = 1;

	mutex_lock(&dbs_mutex);
	if ( input == dbs_tuners_ins.ignore_nice ) { /* nothing to do */
		mutex_unlock(&dbs_mutex);
		return count;
	}
	dbs_tuners_ins.ignore_nice = input;

	/* we need to re-evaluate prev_cpu_idle_up and prev_cpu_idle_down */
	for_each_online_cpu(j) {
		struct cpu_dbs_info_s *j_dbs_info;
		j_dbs_info = &per_cpu(cpu_dbs_info, j);
		j_dbs_info->prev_cpu_idle_up = get_cpu_idle_time(j);
		j_dbs_info->prev_cpu_idle_down = j_dbs_info->prev_cpu_idle_up;
	}
	mutex_unlock(&dbs_mutex);

	return count;
}

#define define_one_rw(_name) \
static struct freq_attr _name = \
__ATTR(_name, 0644, show_##_name, store_##_name)

define_one_rw(sampling_rate);
define_one_rw(sampling_down_factor);
define_one_rw(up_threshold);
define_one_rw(ignore_nice_load);

static struct attribute * dbs_attributes[] = {
	&sampling_rate_max.attr,
	&sampling_rate_min.attr,
	&sampling_rate.attr,
	&sampling_down_factor.attr,
	&up_threshold.attr,
	&ignore_nice_load.attr,
	NULL
};

static struct attribute_group dbs_attr_group = {
	.attrs = dbs_attributes,
	.name = "ondemand",
};

/************************** sysfs end ************************/

static void dbs_check_cpu(int cpu)
{
	unsigned int idle_ticks, up_idle_ticks, total_ticks;
	unsigned int freq_next;
	unsigned int freq_down_sampling_rate;
	static int down_skip[NR_CPUS];
	struct cpu_dbs_info_s *this_dbs_info;

	struct cpufreq_policy *policy;
	unsigned int j;

	this_dbs_info = &per_cpu(cpu_dbs_info, cpu);
	if (!this_dbs_info->enable)
		return;

	policy = this_dbs_info->cur_policy;
	/*
	 * Every sampling_rate, we check, if current idle time is less
	 * than 20% (default), then we try to increase frequency
	 * Every sampling_rate*sampling_down_factor, we look for a the lowest
	 * frequency which can sustain the load while keeping idle time over
	 * 30%. If such a frequency exist, we try to decrease to this frequency.
	 *
	 * Any frequency increase takes it to the maximum frequency.
	 * Frequency reduction happens at minimum steps of
	 * 5% (default) of current frequency
	 */

	/* Check for frequency increase */
	idle_ticks = UINT_MAX;
	for_each_cpu_mask(j, policy->cpus) {
		unsigned int tmp_idle_ticks, total_idle_ticks;
		struct cpu_dbs_info_s *j_dbs_info;

		j_dbs_info = &per_cpu(cpu_dbs_info, j);
		total_idle_ticks = get_cpu_idle_time(j);
		tmp_idle_ticks = total_idle_ticks -
			j_dbs_info->prev_cpu_idle_up;
		j_dbs_info->prev_cpu_idle_up = total_idle_ticks;

		if (tmp_idle_ticks < idle_ticks)
			idle_ticks = tmp_idle_ticks;
	}

	/* Scale idle ticks by 100 and compare with up and down ticks */
	idle_ticks *= 100;
	up_idle_ticks = (100 - dbs_tuners_ins.up_threshold) *
			usecs_to_jiffies(dbs_tuners_ins.sampling_rate);

	if (idle_ticks < up_idle_ticks) {
		down_skip[cpu] = 0;
		for_each_cpu_mask(j, policy->cpus) {
			struct cpu_dbs_info_s *j_dbs_info;

			j_dbs_info = &per_cpu(cpu_dbs_info, j);
			j_dbs_info->prev_cpu_idle_down =
					j_dbs_info->prev_cpu_idle_up;
		}
		/* if we are already at full speed then break out early */
		if (policy->cur == policy->max)
			return;

		__cpufreq_driver_target(policy, policy->max,
			CPUFREQ_RELATION_H);
		return;
	}

	/* Check for frequency decrease */
	down_skip[cpu]++;
	if (down_skip[cpu] < dbs_tuners_ins.sampling_down_factor)
		return;

	idle_ticks = UINT_MAX;
	for_each_cpu_mask(j, policy->cpus) {
		unsigned int tmp_idle_ticks, total_idle_ticks;
		struct cpu_dbs_info_s *j_dbs_info;

		j_dbs_info = &per_cpu(cpu_dbs_info, j);
		/* Check for frequency decrease */
		total_idle_ticks = j_dbs_info->prev_cpu_idle_up;
		tmp_idle_ticks = total_idle_ticks -
			j_dbs_info->prev_cpu_idle_down;
		j_dbs_info->prev_cpu_idle_down = total_idle_ticks;

		if (tmp_idle_ticks < idle_ticks)
			idle_ticks = tmp_idle_ticks;
	}

	down_skip[cpu] = 0;
	/* if we cannot reduce the frequency anymore, break out early */
	if (policy->cur == policy->min)
		return;

	/* Compute how many ticks there are between two measurements */
	freq_down_sampling_rate = dbs_tuners_ins.sampling_rate *
		dbs_tuners_ins.sampling_down_factor;
	total_ticks = usecs_to_jiffies(freq_down_sampling_rate);

	/*
	 * The optimal frequency is the frequency that is the lowest that
	 * can support the current CPU usage without triggering the up
	 * policy. To be safe, we focus 10 points under the threshold.
	 */
	freq_next = ((total_ticks - idle_ticks) * 100) / total_ticks;
	freq_next = (freq_next * policy->cur) /
			(dbs_tuners_ins.up_threshold - 10);

	if (freq_next < policy->min)
		freq_next = policy->min;

	if (freq_next <= ((policy->cur * 95) / 100))
		__cpufreq_driver_target(policy, freq_next, CPUFREQ_RELATION_L);
}

static void do_dbs_timer(void *data)
{
	int i;
	mutex_lock(&dbs_mutex);
	for_each_online_cpu(i)
		dbs_check_cpu(i);
	schedule_delayed_work(&dbs_work,
			usecs_to_jiffies(dbs_tuners_ins.sampling_rate));
	mutex_unlock(&dbs_mutex);
}

static inline void dbs_timer_init(void)
{
	INIT_WORK(&dbs_work, do_dbs_timer, NULL);
	schedule_delayed_work(&dbs_work,
			usecs_to_jiffies(dbs_tuners_ins.sampling_rate));
	return;
}

static inline void dbs_timer_exit(void)
{
	cancel_delayed_work(&dbs_work);
	return;
}

static int cpufreq_governor_dbs(struct cpufreq_policy *policy,
				   unsigned int event)
{
	unsigned int cpu = policy->cpu;
	struct cpu_dbs_info_s *this_dbs_info;
	unsigned int j;

	this_dbs_info = &per_cpu(cpu_dbs_info, cpu);

	switch (event) {
	case CPUFREQ_GOV_START:
		if ((!cpu_online(cpu)) ||
		    (!policy->cur))
			return -EINVAL;

		if (policy->cpuinfo.transition_latency >
				(TRANSITION_LATENCY_LIMIT * 1000)) {
			printk(KERN_WARNING "ondemand governor failed to load "
			       "due to too long transition latency\n");
			return -EINVAL;
		}
		if (this_dbs_info->enable) /* Already enabled */
			break;

		mutex_lock(&dbs_mutex);
		for_each_cpu_mask(j, policy->cpus) {
			struct cpu_dbs_info_s *j_dbs_info;
			j_dbs_info = &per_cpu(cpu_dbs_info, j);
			j_dbs_info->cur_policy = policy;

			j_dbs_info->prev_cpu_idle_up = get_cpu_idle_time(j);
			j_dbs_info->prev_cpu_idle_down
				= j_dbs_info->prev_cpu_idle_up;
		}
		this_dbs_info->enable = 1;
		sysfs_create_group(&policy->kobj, &dbs_attr_group);
		dbs_enable++;
		/*
		 * Start the timerschedule work, when this governor
		 * is used for first time
		 */
		if (dbs_enable == 1) {
			unsigned int latency;
			/* policy latency is in nS. Convert it to uS first */
			latency = policy->cpuinfo.transition_latency / 1000;
			if (latency == 0)
				latency = 1;

			def_sampling_rate = latency *
					DEF_SAMPLING_RATE_LATENCY_MULTIPLIER;

			if (def_sampling_rate < MIN_STAT_SAMPLING_RATE)
				def_sampling_rate = MIN_STAT_SAMPLING_RATE;

			dbs_tuners_ins.sampling_rate = def_sampling_rate;
			dbs_timer_init();
		}

		mutex_unlock(&dbs_mutex);
		break;

	case CPUFREQ_GOV_STOP:
		mutex_lock(&dbs_mutex);
		this_dbs_info->enable = 0;
		sysfs_remove_group(&policy->kobj, &dbs_attr_group);
		dbs_enable--;
		/*
		 * Stop the timerschedule work, when this governor
		 * is used for first time
		 */
		if (dbs_enable == 0)
			dbs_timer_exit();

		mutex_unlock(&dbs_mutex);

		break;

	case CPUFREQ_GOV_LIMITS:
		mutex_lock(&dbs_mutex);
		if (policy->max < this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(
					this_dbs_info->cur_policy,
					policy->max, CPUFREQ_RELATION_H);
		else if (policy->min > this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(
					this_dbs_info->cur_policy,
					policy->min, CPUFREQ_RELATION_L);
		mutex_unlock(&dbs_mutex);
		break;
	}
	return 0;
}

static struct cpufreq_governor cpufreq_gov_dbs = {
	.name		= "ondemand",
	.governor	= cpufreq_governor_dbs,
	.owner		= THIS_MODULE,
};

static int __init cpufreq_gov_dbs_init(void)
{
	return cpufreq_register_governor(&cpufreq_gov_dbs);
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	/* Make sure that the scheduled work is indeed not running */
	flush_scheduled_work();

	cpufreq_unregister_governor(&cpufreq_gov_dbs);
}


MODULE_AUTHOR ("Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>");
MODULE_DESCRIPTION ("'cpufreq_ondemand' - A dynamic cpufreq governor for "
		"Low Latency Frequency Transition capable processors");
MODULE_LICENSE ("GPL");

module_init(cpufreq_gov_dbs_init);
module_exit(cpufreq_gov_dbs_exit);
