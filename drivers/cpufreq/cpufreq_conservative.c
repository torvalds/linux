/*
 *  drivers/cpufreq/cpufreq_conservative.c
 *
 *  Copyright (C)  2001 Russell King
 *            (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *                      Jun Nakajima <jun.nakajima@intel.com>
 *            (C)  2004 Alexander Clouter <alex-kernel@digriz.org.uk>
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
#include <linux/cpu.h>
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
#define DEF_FREQUENCY_DOWN_THRESHOLD		(20)

/*
 * The polling frequency of this governor depends on the capability of
 * the processor. Default polling frequency is 1000 times the transition
 * latency of the processor. The governor will work on any processor with
 * transition latency <= 10mS, using appropriate sampling
 * rate.
 * For CPUs with transition latency > 10mS (mostly drivers
 * with CPUFREQ_ETERNAL), this governor will not work.
 * All times here are in uS.
 */
static unsigned int def_sampling_rate;
#define MIN_SAMPLING_RATE_RATIO			(2)
/* for correct statistics, we need at least 10 ticks between each measure */
#define MIN_STAT_SAMPLING_RATE			\
			(MIN_SAMPLING_RATE_RATIO * jiffies_to_usecs(10))
#define MIN_SAMPLING_RATE			\
			(def_sampling_rate / MIN_SAMPLING_RATE_RATIO)
#define MAX_SAMPLING_RATE			(500 * def_sampling_rate)
#define DEF_SAMPLING_RATE_LATENCY_MULTIPLIER	(1000)
#define DEF_SAMPLING_DOWN_FACTOR		(1)
#define MAX_SAMPLING_DOWN_FACTOR		(10)
#define TRANSITION_LATENCY_LIMIT		(10 * 1000 * 1000)

static void do_dbs_timer(struct work_struct *work);

struct cpu_dbs_info_s {
	struct cpufreq_policy *cur_policy;
	unsigned int prev_cpu_idle_up;
	unsigned int prev_cpu_idle_down;
	unsigned int enable;
	unsigned int down_skip;
	unsigned int requested_freq;
};
static DEFINE_PER_CPU(struct cpu_dbs_info_s, cpu_dbs_info);

static unsigned int dbs_enable;	/* number of CPUs using this policy */

/*
 * DEADLOCK ALERT! There is a ordering requirement between cpu_hotplug
 * lock and dbs_mutex. cpu_hotplug lock should always be held before
 * dbs_mutex. If any function that can potentially take cpu_hotplug lock
 * (like __cpufreq_driver_target()) is being called with dbs_mutex taken, then
 * cpu_hotplug lock should be taken before that. Note that cpu_hotplug lock
 * is recursive for the same process. -Venki
 */
static DEFINE_MUTEX (dbs_mutex);
static DECLARE_DELAYED_WORK(dbs_work, do_dbs_timer);

struct dbs_tuners {
	unsigned int sampling_rate;
	unsigned int sampling_down_factor;
	unsigned int up_threshold;
	unsigned int down_threshold;
	unsigned int ignore_nice;
	unsigned int freq_step;
};

static struct dbs_tuners dbs_tuners_ins = {
	.up_threshold = DEF_FREQUENCY_UP_THRESHOLD,
	.down_threshold = DEF_FREQUENCY_DOWN_THRESHOLD,
	.sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR,
	.ignore_nice = 0,
	.freq_step = 5,
};

static inline unsigned int get_cpu_idle_time(unsigned int cpu)
{
	unsigned int add_nice = 0, ret;

	if (dbs_tuners_ins.ignore_nice)
		add_nice = kstat_cpu(cpu).cpustat.nice;

	ret = kstat_cpu(cpu).cpustat.idle +
		kstat_cpu(cpu).cpustat.iowait +
		add_nice;

	return ret;
}

/* keep track of frequency transitions */
static int
dbs_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
		     void *data)
{
	struct cpufreq_freqs *freq = data;
	struct cpu_dbs_info_s *this_dbs_info = &per_cpu(cpu_dbs_info,
							freq->cpu);

	if (!this_dbs_info->enable)
		return 0;

	this_dbs_info->requested_freq = freq->new;

	return 0;
}

static struct notifier_block dbs_cpufreq_notifier_block = {
	.notifier_call = dbs_cpufreq_notifier
};

/************************** sysfs interface ************************/
static ssize_t show_sampling_rate_max(struct cpufreq_policy *policy, char *buf)
{
	return sprintf (buf, "%u\n", MAX_SAMPLING_RATE);
}

static ssize_t show_sampling_rate_min(struct cpufreq_policy *policy, char *buf)
{
	return sprintf (buf, "%u\n", MIN_SAMPLING_RATE);
}

#define define_one_ro(_name)				\
static struct freq_attr _name =				\
__ATTR(_name, 0444, show_##_name, NULL)

define_one_ro(sampling_rate_max);
define_one_ro(sampling_rate_min);

/* cpufreq_conservative Governor Tunables */
#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct cpufreq_policy *unused, char *buf)				\
{									\
	return sprintf(buf, "%u\n", dbs_tuners_ins.object);		\
}
show_one(sampling_rate, sampling_rate);
show_one(sampling_down_factor, sampling_down_factor);
show_one(up_threshold, up_threshold);
show_one(down_threshold, down_threshold);
show_one(ignore_nice_load, ignore_nice);
show_one(freq_step, freq_step);

static ssize_t store_sampling_down_factor(struct cpufreq_policy *unused,
		const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf (buf, "%u", &input);
	if (ret != 1 || input > MAX_SAMPLING_DOWN_FACTOR || input < 1)
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
	if (ret != 1 || input > 100 || input <= dbs_tuners_ins.down_threshold) {
		mutex_unlock(&dbs_mutex);
		return -EINVAL;
	}

	dbs_tuners_ins.up_threshold = input;
	mutex_unlock(&dbs_mutex);

	return count;
}

static ssize_t store_down_threshold(struct cpufreq_policy *unused,
		const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf (buf, "%u", &input);

	mutex_lock(&dbs_mutex);
	if (ret != 1 || input > 100 || input >= dbs_tuners_ins.up_threshold) {
		mutex_unlock(&dbs_mutex);
		return -EINVAL;
	}

	dbs_tuners_ins.down_threshold = input;
	mutex_unlock(&dbs_mutex);

	return count;
}

static ssize_t store_ignore_nice_load(struct cpufreq_policy *policy,
		const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	unsigned int j;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	mutex_lock(&dbs_mutex);
	if (input == dbs_tuners_ins.ignore_nice) { /* nothing to do */
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

static ssize_t store_freq_step(struct cpufreq_policy *policy,
		const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	if (input > 100)
		input = 100;

	/* no need to test here if freq_step is zero as the user might actually
	 * want this, they would be crazy though :) */
	mutex_lock(&dbs_mutex);
	dbs_tuners_ins.freq_step = input;
	mutex_unlock(&dbs_mutex);

	return count;
}

#define define_one_rw(_name) \
static struct freq_attr _name = \
__ATTR(_name, 0644, show_##_name, store_##_name)

define_one_rw(sampling_rate);
define_one_rw(sampling_down_factor);
define_one_rw(up_threshold);
define_one_rw(down_threshold);
define_one_rw(ignore_nice_load);
define_one_rw(freq_step);

static struct attribute * dbs_attributes[] = {
	&sampling_rate_max.attr,
	&sampling_rate_min.attr,
	&sampling_rate.attr,
	&sampling_down_factor.attr,
	&up_threshold.attr,
	&down_threshold.attr,
	&ignore_nice_load.attr,
	&freq_step.attr,
	NULL
};

static struct attribute_group dbs_attr_group = {
	.attrs = dbs_attributes,
	.name = "conservative",
};

/************************** sysfs end ************************/

static void dbs_check_cpu(int cpu)
{
	unsigned int idle_ticks, up_idle_ticks, down_idle_ticks;
	unsigned int tmp_idle_ticks, total_idle_ticks;
	unsigned int freq_target;
	unsigned int freq_down_sampling_rate;
	struct cpu_dbs_info_s *this_dbs_info = &per_cpu(cpu_dbs_info, cpu);
	struct cpufreq_policy *policy;

	if (!this_dbs_info->enable)
		return;

	policy = this_dbs_info->cur_policy;

	/*
	 * The default safe range is 20% to 80%
	 * Every sampling_rate, we check
	 *	- If current idle time is less than 20%, then we try to
	 *	  increase frequency
	 * Every sampling_rate*sampling_down_factor, we check
	 *	- If current idle time is more than 80%, then we try to
	 *	  decrease frequency
	 *
	 * Any frequency increase takes it to the maximum frequency.
	 * Frequency reduction happens at minimum steps of
	 * 5% (default) of max_frequency
	 */

	/* Check for frequency increase */
	idle_ticks = UINT_MAX;

	/* Check for frequency increase */
	total_idle_ticks = get_cpu_idle_time(cpu);
	tmp_idle_ticks = total_idle_ticks -
		this_dbs_info->prev_cpu_idle_up;
	this_dbs_info->prev_cpu_idle_up = total_idle_ticks;

	if (tmp_idle_ticks < idle_ticks)
		idle_ticks = tmp_idle_ticks;

	/* Scale idle ticks by 100 and compare with up and down ticks */
	idle_ticks *= 100;
	up_idle_ticks = (100 - dbs_tuners_ins.up_threshold) *
			usecs_to_jiffies(dbs_tuners_ins.sampling_rate);

	if (idle_ticks < up_idle_ticks) {
		this_dbs_info->down_skip = 0;
		this_dbs_info->prev_cpu_idle_down =
			this_dbs_info->prev_cpu_idle_up;

		/* if we are already at full speed then break out early */
		if (this_dbs_info->requested_freq == policy->max)
			return;

		freq_target = (dbs_tuners_ins.freq_step * policy->max) / 100;

		/* max freq cannot be less than 100. But who knows.... */
		if (unlikely(freq_target == 0))
			freq_target = 5;

		this_dbs_info->requested_freq += freq_target;
		if (this_dbs_info->requested_freq > policy->max)
			this_dbs_info->requested_freq = policy->max;

		__cpufreq_driver_target(policy, this_dbs_info->requested_freq,
			CPUFREQ_RELATION_H);
		return;
	}

	/* Check for frequency decrease */
	this_dbs_info->down_skip++;
	if (this_dbs_info->down_skip < dbs_tuners_ins.sampling_down_factor)
		return;

	/* Check for frequency decrease */
	total_idle_ticks = this_dbs_info->prev_cpu_idle_up;
	tmp_idle_ticks = total_idle_ticks -
		this_dbs_info->prev_cpu_idle_down;
	this_dbs_info->prev_cpu_idle_down = total_idle_ticks;

	if (tmp_idle_ticks < idle_ticks)
		idle_ticks = tmp_idle_ticks;

	/* Scale idle ticks by 100 and compare with up and down ticks */
	idle_ticks *= 100;
	this_dbs_info->down_skip = 0;

	freq_down_sampling_rate = dbs_tuners_ins.sampling_rate *
		dbs_tuners_ins.sampling_down_factor;
	down_idle_ticks = (100 - dbs_tuners_ins.down_threshold) *
		usecs_to_jiffies(freq_down_sampling_rate);

	if (idle_ticks > down_idle_ticks) {
		/*
		 * if we are already at the lowest speed then break out early
		 * or if we 'cannot' reduce the speed as the user might want
		 * freq_target to be zero
		 */
		if (this_dbs_info->requested_freq == policy->min
				|| dbs_tuners_ins.freq_step == 0)
			return;

		freq_target = (dbs_tuners_ins.freq_step * policy->max) / 100;

		/* max freq cannot be less than 100. But who knows.... */
		if (unlikely(freq_target == 0))
			freq_target = 5;

		this_dbs_info->requested_freq -= freq_target;
		if (this_dbs_info->requested_freq < policy->min)
			this_dbs_info->requested_freq = policy->min;

		__cpufreq_driver_target(policy, this_dbs_info->requested_freq,
				CPUFREQ_RELATION_H);
		return;
	}
}

static void do_dbs_timer(struct work_struct *work)
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
	init_timer_deferrable(&dbs_work.timer);
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
	int rc;

	this_dbs_info = &per_cpu(cpu_dbs_info, cpu);

	switch (event) {
	case CPUFREQ_GOV_START:
		if ((!cpu_online(cpu)) || (!policy->cur))
			return -EINVAL;

		if (this_dbs_info->enable) /* Already enabled */
			break;

		mutex_lock(&dbs_mutex);

		rc = sysfs_create_group(&policy->kobj, &dbs_attr_group);
		if (rc) {
			mutex_unlock(&dbs_mutex);
			return rc;
		}

		for_each_cpu_mask_nr(j, policy->cpus) {
			struct cpu_dbs_info_s *j_dbs_info;
			j_dbs_info = &per_cpu(cpu_dbs_info, j);
			j_dbs_info->cur_policy = policy;

			j_dbs_info->prev_cpu_idle_up = get_cpu_idle_time(cpu);
			j_dbs_info->prev_cpu_idle_down
				= j_dbs_info->prev_cpu_idle_up;
		}
		this_dbs_info->enable = 1;
		this_dbs_info->down_skip = 0;
		this_dbs_info->requested_freq = policy->cur;

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

			def_sampling_rate = 10 * latency *
					DEF_SAMPLING_RATE_LATENCY_MULTIPLIER;

			if (def_sampling_rate < MIN_STAT_SAMPLING_RATE)
				def_sampling_rate = MIN_STAT_SAMPLING_RATE;

			dbs_tuners_ins.sampling_rate = def_sampling_rate;

			dbs_timer_init();
			cpufreq_register_notifier(
					&dbs_cpufreq_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);
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
		if (dbs_enable == 0) {
			dbs_timer_exit();
			cpufreq_unregister_notifier(
					&dbs_cpufreq_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);
		}

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

struct cpufreq_governor cpufreq_gov_conservative = {
	.name			= "conservative",
	.governor		= cpufreq_governor_dbs,
	.max_transition_latency	= TRANSITION_LATENCY_LIMIT,
	.owner			= THIS_MODULE,
};
EXPORT_SYMBOL(cpufreq_gov_conservative);

static int __init cpufreq_gov_dbs_init(void)
{
	return cpufreq_register_governor(&cpufreq_gov_conservative);
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	/* Make sure that the scheduled work is indeed not running */
	flush_scheduled_work();

	cpufreq_unregister_governor(&cpufreq_gov_conservative);
}


MODULE_AUTHOR ("Alexander Clouter <alex-kernel@digriz.org.uk>");
MODULE_DESCRIPTION ("'cpufreq_conservative' - A dynamic cpufreq governor for "
		"Low Latency Frequency Transition capable processors "
		"optimised for use in a battery environment");
MODULE_LICENSE ("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_CONSERVATIVE
fs_initcall(cpufreq_gov_dbs_init);
#else
module_init(cpufreq_gov_dbs_init);
#endif
module_exit(cpufreq_gov_dbs_exit);
