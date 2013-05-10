/*
 * drivers/cpufreq/cpufreq_smartass2.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Erasmux
 *
 * Based on the interactive governor By Mike Chan (mike@android.com)
 * which was adaptated to 2.6.29 kernel by Nadlabak (pavel@doshaska.net)
 *
 * SMP support based on mod by faux123
 *
 * For a general overview of smartassV2 see the relavent part in
 * Documentation/cpu-freq/governors.txt
 *
 */

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/sched.h>
#include <linux/tick.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/moduleparam.h>
#include <asm/cputime.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

/******************** Tunable parameters: ********************/

/*
 * The "ideal" frequency to use when awake. The governor will ramp up faster
 * towards the ideal frequency and slower after it has passed it. Similarly,
 * lowering the frequency towards the ideal frequency is faster than below it.
 */
#define DEFAULT_AWAKE_IDEAL_FREQ 696000
static unsigned int awake_ideal_freq;

/*
 * The "ideal" frequency to use when suspended.
 * When set to 0, the governor will not track the suspended state (meaning
 * that practically when sleep_ideal_freq==0 the awake_ideal_freq is used
 * also when suspended).
 */
#define DEFAULT_SLEEP_IDEAL_FREQ 108000
static unsigned int sleep_ideal_freq;

/*
 * Freqeuncy delta when ramping up above the ideal freqeuncy.
 * Zero disables and causes to always jump straight to max frequency.
 * When below the ideal freqeuncy we always ramp up to the ideal freq.
 */
#define DEFAULT_RAMP_UP_STEP 256000
static unsigned int ramp_up_step;

/*
 * Freqeuncy delta when ramping down below the ideal freqeuncy.
 * Zero disables and will calculate ramp down according to load heuristic.
 * When above the ideal freqeuncy we always ramp down to the ideal freq.
 */
#define DEFAULT_RAMP_DOWN_STEP 168000
static unsigned int ramp_down_step;

/*
 * CPU freq will be increased if measured load > max_cpu_load;
 */
#define DEFAULT_MAX_CPU_LOAD 50
static unsigned long max_cpu_load;

/*
 * CPU freq will be decreased if measured load < min_cpu_load;
 */
#define DEFAULT_MIN_CPU_LOAD 25
static unsigned long min_cpu_load;

/*
 * The minimum amount of time to spend at a frequency before we can ramp up.
 * Notice we ignore this when we are below the ideal frequency.
 */
#define DEFAULT_UP_RATE_US 48000;
static unsigned long up_rate_us;

/*
 * The minimum amount of time to spend at a frequency before we can ramp down.
 * Notice we ignore this when we are above the ideal frequency.
 */
#define DEFAULT_DOWN_RATE_US 99000;
static unsigned long down_rate_us;

/*
 * The frequency to set when waking up from sleep.
 * When sleep_ideal_freq=0 this will have no effect.
 */
#define DEFAULT_SLEEP_WAKEUP_FREQ 99999999
static unsigned int sleep_wakeup_freq;

/*
 * Sampling rate, I highly recommend to leave it at 2.
 */
#define DEFAULT_SAMPLE_RATE_JIFFIES 2
static unsigned int sample_rate_jiffies;


/*************** End of tunables ***************/


static void (*pm_idle_old)(void);
static atomic_t active_count = ATOMIC_INIT(0);

struct smartass_info_s {
	struct cpufreq_policy *cur_policy;
	struct cpufreq_frequency_table *freq_table;
	struct timer_list timer;
	u64 time_in_idle;
	u64 idle_exit_time;
	u64 freq_change_time;
	u64 freq_change_time_in_idle;
	int cur_cpu_load;
	int old_freq;
	int ramp_dir;
	unsigned int enable;
	int ideal_speed;
};
static DEFINE_PER_CPU(struct smartass_info_s, smartass_info);

/* Workqueues handle frequency scaling */
static struct workqueue_struct *up_wq;
static struct workqueue_struct *down_wq;
static struct work_struct freq_scale_work;

static cpumask_t work_cpumask;
static spinlock_t cpumask_lock;

static unsigned int suspended;

#define dprintk(flag,msg...) do { \
	if (debug_mask & flag) printk(KERN_DEBUG msg); \
	} while (0)

enum {
	SMARTASS_DEBUG_JUMPS=1,
	SMARTASS_DEBUG_LOAD=2,
	SMARTASS_DEBUG_ALG=4
};

/*
 * Combination of the above debug flags.
 */
static unsigned long debug_mask;

static int cpufreq_governor_smartass(struct cpufreq_policy *policy,
		unsigned int event);

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_SMARTASS2
static
#endif
struct cpufreq_governor cpufreq_gov_smartass2 = {
	.name = "smartassV2",
	.governor = cpufreq_governor_smartass,
	.max_transition_latency = 9000000,
	.owner = THIS_MODULE,
};

inline static void smartass_update_min_max(struct smartass_info_s *this_smartass, struct cpufreq_policy *policy, int suspend) {
	if (suspend) {
		this_smartass->ideal_speed = /* sleep_ideal_freq; but make sure it obeys the policy min/max */
			policy->max > sleep_ideal_freq ?
			(sleep_ideal_freq > policy->min ? sleep_ideal_freq : policy->min) : policy->max;
	} else {
		this_smartass->ideal_speed = /* awake_ideal_freq; but make sure it obeys the policy min/max */
			policy->min < awake_ideal_freq ?
			(awake_ideal_freq < policy->max ? awake_ideal_freq : policy->max) : policy->min;
	}
}

inline static void smartass_update_min_max_allcpus(void) {
	unsigned int i;
	for_each_online_cpu(i) {
		struct smartass_info_s *this_smartass = &per_cpu(smartass_info, i);
		if (this_smartass->enable)
			smartass_update_min_max(this_smartass,this_smartass->cur_policy,suspended);
	}
}

inline static unsigned int validate_freq(struct cpufreq_policy *policy, int freq) {
	if (freq > (int)policy->max)
		return policy->max;
	if (freq < (int)policy->min)
		return policy->min;
	return freq;
}

inline static void reset_timer(unsigned long cpu, struct smartass_info_s *this_smartass) {
	this_smartass->time_in_idle = get_cpu_idle_time_us(cpu, &this_smartass->idle_exit_time);
	mod_timer(&this_smartass->timer, jiffies + sample_rate_jiffies);
}

inline static void work_cpumask_set(unsigned long cpu) {
	unsigned long flags;
	spin_lock_irqsave(&cpumask_lock, flags);
	cpumask_set_cpu(cpu, &work_cpumask);
	spin_unlock_irqrestore(&cpumask_lock, flags);
}

inline static int work_cpumask_test_and_clear(unsigned long cpu) {
	unsigned long flags;
	int res = 0;
	spin_lock_irqsave(&cpumask_lock, flags);
	res = cpumask_test_and_clear_cpu(cpu, &work_cpumask);
	spin_unlock_irqrestore(&cpumask_lock, flags);
	return res;
}

inline static int target_freq(struct cpufreq_policy *policy, struct smartass_info_s *this_smartass,
			      int new_freq, int old_freq, int prefered_relation) {
	int index, target;
	struct cpufreq_frequency_table *table = this_smartass->freq_table;

	if (new_freq == old_freq)
		return 0;
	new_freq = validate_freq(policy,new_freq);
	if (new_freq == old_freq)
		return 0;

	if (table &&
	    !cpufreq_frequency_table_target(policy,table,new_freq,prefered_relation,&index))
	{
		target = table[index].frequency;
		if (target == old_freq) {
			/* if for example we are ramping up to *at most* current + ramp_up_step
			 * but there is no such frequency higher than the current, try also
			 * to ramp up to *at least* current + ramp_up_step. */
			if (new_freq > old_freq && prefered_relation==CPUFREQ_RELATION_H
			    && !cpufreq_frequency_table_target(policy,table,new_freq,
							       CPUFREQ_RELATION_L,&index))
				target = table[index].frequency;
			/* simlarly for ramping down: */
			else if (new_freq < old_freq && prefered_relation==CPUFREQ_RELATION_L
				&& !cpufreq_frequency_table_target(policy,table,new_freq,
								   CPUFREQ_RELATION_H,&index))
				target = table[index].frequency;
		}

		if (target == old_freq) {
			/* We should not get here:
			 * If we got here we tried to change to a validated new_freq which is different
			 * from old_freq, so there is no reason for us to remain at same frequency. */
			printk(KERN_WARNING "Smartass: frequency change failed: %d to %d => %d\n",
			       old_freq,new_freq,target);
			return 0;
		}
	}
	else target = new_freq;

	__cpufreq_driver_target(policy, target, prefered_relation);

	dprintk(SMARTASS_DEBUG_JUMPS,"SmartassQ: jumping from %d to %d => %d (%d)\n",
		old_freq,new_freq,target,policy->cur);

	return target;
}

static void cpufreq_smartass_timer(unsigned long cpu)
{
	u64 delta_idle;
	u64 delta_time;
	int cpu_load;
	int old_freq;
	u64 update_time = 0;
	u64 now_idle;
	int queued_work = 0;
	struct smartass_info_s *this_smartass = &per_cpu(smartass_info, cpu);
	struct cpufreq_policy *policy = this_smartass->cur_policy;

	now_idle = get_cpu_idle_time_us(cpu, &update_time);
	old_freq = policy->cur;

	if (this_smartass->idle_exit_time == 0 || update_time == this_smartass->idle_exit_time)
		return;

	delta_idle = cputime64_sub(now_idle, this_smartass->time_in_idle);
	delta_time = cputime64_sub(update_time, this_smartass->idle_exit_time);

	/* If timer ran less than 1ms after short-term sample started, retry. */
	if (delta_time < 1000) {
		if (!timer_pending(&this_smartass->timer))
			reset_timer(cpu,this_smartass);
		return;
	}

	if (delta_idle > delta_time)
		cpu_load = 0;
	else
		cpu_load = 100 * (unsigned int)(delta_time - delta_idle) / (unsigned int)delta_time;

	dprintk(SMARTASS_DEBUG_LOAD,"smartassT @ %d: load %d (delta_time %llu)\n",
		old_freq,cpu_load,delta_time);

	this_smartass->cur_cpu_load = cpu_load;
	this_smartass->old_freq = old_freq;

	/* Scale up if load is above max or if there where no idle cycles since coming out of idle,
	 * additionally, if we are at or above the ideal_speed, verify we have been at this frequency
	 * for at least up_rate_us: */
	if (cpu_load > max_cpu_load || delta_idle == 0)
	{
		if (old_freq < policy->max &&
			 (old_freq < this_smartass->ideal_speed || delta_idle == 0 ||
			  cputime64_sub(update_time, this_smartass->freq_change_time) >= up_rate_us))
		{
			dprintk(SMARTASS_DEBUG_ALG,"smartassT @ %d ramp up: load %d (delta_idle %llu)\n",
				old_freq,cpu_load,delta_idle);
			this_smartass->ramp_dir = 1;
			work_cpumask_set(cpu);
			queue_work(up_wq, &freq_scale_work);
			queued_work = 1;
		}
		else this_smartass->ramp_dir = 0;
	}
	/* Similarly for scale down: load should be below min and if we are at or below ideal
	 * frequency we require that we have been at this frequency for at least down_rate_us: */
	else if (cpu_load < min_cpu_load && old_freq > policy->min &&
		 (old_freq > this_smartass->ideal_speed ||
		  cputime64_sub(update_time, this_smartass->freq_change_time) >= down_rate_us))
	{
		dprintk(SMARTASS_DEBUG_ALG,"smartassT @ %d ramp down: load %d (delta_idle %llu)\n",
			old_freq,cpu_load,delta_idle);
		this_smartass->ramp_dir = -1;
		work_cpumask_set(cpu);
		queue_work(down_wq, &freq_scale_work);
		queued_work = 1;
	}
	else this_smartass->ramp_dir = 0;

	/* To avoid unnecessary load when the CPU is already at high load, we don't
	 * reset ourselves if we are at max speed. If and when there are idle cycles,
	 * the idle loop will activate the timer.
	 * Additionally, if we queued some work, the work task will reset the timer
	 * after it has done its adjustments. */
	if (!queued_work && old_freq < policy->max)
		reset_timer(cpu,this_smartass);
}

static void cpufreq_idle(void)
{
	struct smartass_info_s *this_smartass = &per_cpu(smartass_info, smp_processor_id());
	struct cpufreq_policy *policy = this_smartass->cur_policy;

	if (!this_smartass->enable) {
		pm_idle_old();
		return;
	}

	if (policy->cur == policy->min && timer_pending(&this_smartass->timer))
		del_timer(&this_smartass->timer);

	pm_idle_old();

	if (!timer_pending(&this_smartass->timer))
		reset_timer(smp_processor_id(), this_smartass);
}

/* We use the same work function to sale up and down */
static void cpufreq_smartass_freq_change_time_work(struct work_struct *work)
{
	unsigned int cpu;
	int new_freq;
	int old_freq;
	int ramp_dir;
	struct smartass_info_s *this_smartass;
	struct cpufreq_policy *policy;
	unsigned int relation = CPUFREQ_RELATION_L;
	for_each_possible_cpu(cpu) {
		this_smartass = &per_cpu(smartass_info, cpu);
		if (!work_cpumask_test_and_clear(cpu))
			continue;

		ramp_dir = this_smartass->ramp_dir;
		this_smartass->ramp_dir = 0;

		old_freq = this_smartass->old_freq;
		policy = this_smartass->cur_policy;

		if (old_freq != policy->cur) {
			/* frequency was changed by someone else? */
			printk(KERN_WARNING "Smartass: frequency changed by 3rd party: %d to %d\n",
			       old_freq,policy->cur);
			new_freq = old_freq;
		}
		else if (ramp_dir > 0 && nr_running() > 1) {
			/* ramp up logic: */
			if (old_freq < this_smartass->ideal_speed)
				new_freq = this_smartass->ideal_speed;
			else if (ramp_up_step) {
				new_freq = old_freq + ramp_up_step;
				relation = CPUFREQ_RELATION_H;
			}
			else {
				new_freq = policy->max;
				relation = CPUFREQ_RELATION_H;
			}
			dprintk(SMARTASS_DEBUG_ALG,"smartassQ @ %d ramp up: ramp_dir=%d ideal=%d\n",
				old_freq,ramp_dir,this_smartass->ideal_speed);
		}
		else if (ramp_dir < 0) {
			/* ramp down logic: */
			if (old_freq > this_smartass->ideal_speed) {
				new_freq = this_smartass->ideal_speed;
				relation = CPUFREQ_RELATION_H;
			}
			else if (ramp_down_step)
				new_freq = old_freq - ramp_down_step;
			else {
				/* Load heuristics: Adjust new_freq such that, assuming a linear
				 * scaling of load vs. frequency, the load in the new frequency
				 * will be max_cpu_load: */
				new_freq = old_freq * this_smartass->cur_cpu_load / max_cpu_load;
				if (new_freq > old_freq) /* min_cpu_load > max_cpu_load ?! */
					new_freq = old_freq -1;
			}
			dprintk(SMARTASS_DEBUG_ALG,"smartassQ @ %d ramp down: ramp_dir=%d ideal=%d\n",
				old_freq,ramp_dir,this_smartass->ideal_speed);
		}
		else { 
			/* ramp_dir==0 ?! Could the timer change its mind about a queued ramp up/down
		         * before the work task gets to run?
		         * This may also happen if we refused to ramp up because the nr_running()==1 */
			new_freq = old_freq;
			dprintk(SMARTASS_DEBUG_ALG,"smartassQ @ %d nothing: ramp_dir=%d nr_running=%lu\n",
				old_freq,ramp_dir,nr_running());
		}

		/* do actual ramp up (returns 0, if frequency change failed): */
		new_freq = target_freq(policy,this_smartass,new_freq,old_freq,relation);
		if (new_freq)
			this_smartass->freq_change_time_in_idle =
				get_cpu_idle_time_us(cpu,&this_smartass->freq_change_time);

		/* reset timer: */
		if (new_freq < policy->max)
			reset_timer(cpu,this_smartass);
		/* if we are maxed out, it is pointless to use the timer
		 * (idle cycles wake up the timer when the timer comes) */
		else if (timer_pending(&this_smartass->timer))
			del_timer(&this_smartass->timer);
	}
}

static ssize_t show_debug_mask(struct kobject *kobj, struct attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", debug_mask);
}

static ssize_t store_debug_mask(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
	ssize_t res;
	unsigned long input;
	res = strict_strtoul(buf, 0, &input);
	if (res >= 0)
		debug_mask = input;
	return res;
}

static ssize_t show_up_rate_us(struct kobject *kobj, struct attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", up_rate_us);
}

static ssize_t store_up_rate_us(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
	ssize_t res;
	unsigned long input;
	res = strict_strtoul(buf, 0, &input);
	if (res >= 0 && input >= 0 && input <= 100000000)
		up_rate_us = input;
	return res;
}

static ssize_t show_down_rate_us(struct kobject *kobj, struct attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", down_rate_us);
}

static ssize_t store_down_rate_us(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
	ssize_t res;
	unsigned long input;
	res = strict_strtoul(buf, 0, &input);
	if (res >= 0 && input >= 0 && input <= 100000000)
		down_rate_us = input;
	return res;
}

static ssize_t show_sleep_ideal_freq(struct kobject *kobj, struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", sleep_ideal_freq);
}

static ssize_t store_sleep_ideal_freq(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
	ssize_t res;
	unsigned long input;
	res = strict_strtoul(buf, 0, &input);
	if (res >= 0 && input >= 0) {
		sleep_ideal_freq = input;
		if (suspended)
			smartass_update_min_max_allcpus();
	}
	return res;
}

static ssize_t show_sleep_wakeup_freq(struct kobject *kobj, struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", sleep_wakeup_freq);
}

static ssize_t store_sleep_wakeup_freq(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
	ssize_t res;
	unsigned long input;
	res = strict_strtoul(buf, 0, &input);
	if (res >= 0 && input >= 0)
		sleep_wakeup_freq = input;
	return res;
}

static ssize_t show_awake_ideal_freq(struct kobject *kobj, struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", awake_ideal_freq);
}

static ssize_t store_awake_ideal_freq(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
	ssize_t res;
	unsigned long input;
	res = strict_strtoul(buf, 0, &input);
	if (res >= 0 && input >= 0) {
		awake_ideal_freq = input;
		if (!suspended)
			smartass_update_min_max_allcpus();
	}
	return res;
}

static ssize_t show_sample_rate_jiffies(struct kobject *kobj, struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", sample_rate_jiffies);
}

static ssize_t store_sample_rate_jiffies(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
	ssize_t res;
	unsigned long input;
	res = strict_strtoul(buf, 0, &input);
	if (res >= 0 && input > 0 && input <= 1000)
		sample_rate_jiffies = input;
	return res;
}

static ssize_t show_ramp_up_step(struct kobject *kobj, struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", ramp_up_step);
}

static ssize_t store_ramp_up_step(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
	ssize_t res;
	unsigned long input;
	res = strict_strtoul(buf, 0, &input);
	if (res >= 0 && input >= 0)
		ramp_up_step = input;
	return res;
}

static ssize_t show_ramp_down_step(struct kobject *kobj, struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", ramp_down_step);
}

static ssize_t store_ramp_down_step(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
	ssize_t res;
	unsigned long input;
	res = strict_strtoul(buf, 0, &input);
	if (res >= 0 && input >= 0)
		ramp_down_step = input;
	return res;
}

static ssize_t show_max_cpu_load(struct kobject *kobj, struct attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", max_cpu_load);
}

static ssize_t store_max_cpu_load(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
	ssize_t res;
	unsigned long input;
	res = strict_strtoul(buf, 0, &input);
	if (res >= 0 && input > 0 && input <= 100)
		max_cpu_load = input;
	return res;
}

static ssize_t show_min_cpu_load(struct kobject *kobj, struct attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", min_cpu_load);
}

static ssize_t store_min_cpu_load(struct kobject *kobj, struct attribute *attr, const char *buf, size_t count)
{
	ssize_t res;
	unsigned long input;
	res = strict_strtoul(buf, 0, &input);
	if (res >= 0 && input > 0 && input < 100)
		min_cpu_load = input;
	return res;
}

#define define_global_rw_attr(_name)		\
static struct global_attr _name##_attr =	\
	__ATTR(_name, 0644, show_##_name, store_##_name)

define_global_rw_attr(debug_mask);
define_global_rw_attr(up_rate_us);
define_global_rw_attr(down_rate_us);
define_global_rw_attr(sleep_ideal_freq);
define_global_rw_attr(sleep_wakeup_freq);
define_global_rw_attr(awake_ideal_freq);
define_global_rw_attr(sample_rate_jiffies);
define_global_rw_attr(ramp_up_step);
define_global_rw_attr(ramp_down_step);
define_global_rw_attr(max_cpu_load);
define_global_rw_attr(min_cpu_load);

static struct attribute * smartass_attributes[] = {
	&debug_mask_attr.attr,
	&up_rate_us_attr.attr,
	&down_rate_us_attr.attr,
	&sleep_ideal_freq_attr.attr,
	&sleep_wakeup_freq_attr.attr,
	&awake_ideal_freq_attr.attr,
	&sample_rate_jiffies_attr.attr,
	&ramp_up_step_attr.attr,
	&ramp_down_step_attr.attr,
	&max_cpu_load_attr.attr,
	&min_cpu_load_attr.attr,
	NULL,
};

static struct attribute_group smartass_attr_group = {
	.attrs = smartass_attributes,
	.name = "smartass",
};

static int cpufreq_governor_smartass(struct cpufreq_policy *new_policy,
		unsigned int event)
{
	unsigned int cpu = new_policy->cpu;
	int rc;
	struct smartass_info_s *this_smartass = &per_cpu(smartass_info, cpu);

	switch (event) {
	case CPUFREQ_GOV_START:
		if ((!cpu_online(cpu)) || (!new_policy->cur))
			return -EINVAL;

		this_smartass->cur_policy = new_policy;

		this_smartass->enable = 1;

		smartass_update_min_max(this_smartass,new_policy,suspended);

		this_smartass->freq_table = cpufreq_frequency_get_table(cpu);
		if (!this_smartass->freq_table)
			printk(KERN_WARNING "Smartass: no frequency table for cpu %d?!\n",cpu);

		smp_wmb();

		/* Do not register the idle hook and create sysfs
		 * entries if we have already done so. */
		if (atomic_inc_return(&active_count) <= 1) {
			rc = sysfs_create_group(cpufreq_global_kobject,
						&smartass_attr_group);
			if (rc)
				return rc;

			pm_idle_old = pm_idle;
			pm_idle = cpufreq_idle;
		}

		if (this_smartass->cur_policy->cur < new_policy->max && !timer_pending(&this_smartass->timer))
			reset_timer(cpu,this_smartass);

		break;

	case CPUFREQ_GOV_LIMITS:
		smartass_update_min_max(this_smartass,new_policy,suspended);

		if (this_smartass->cur_policy->cur > new_policy->max) {
			dprintk(SMARTASS_DEBUG_JUMPS,"SmartassI: jumping to new max freq: %d\n",new_policy->max);
			__cpufreq_driver_target(this_smartass->cur_policy,
						new_policy->max, CPUFREQ_RELATION_H);
		}
		else if (this_smartass->cur_policy->cur < new_policy->min) {
			dprintk(SMARTASS_DEBUG_JUMPS,"SmartassI: jumping to new min freq: %d\n",new_policy->min);
			__cpufreq_driver_target(this_smartass->cur_policy,
						new_policy->min, CPUFREQ_RELATION_L);
		}

		if (this_smartass->cur_policy->cur < new_policy->max && !timer_pending(&this_smartass->timer))
			reset_timer(cpu,this_smartass);

		break;

	case CPUFREQ_GOV_STOP:
		this_smartass->enable = 0;
		smp_wmb();
		del_timer(&this_smartass->timer);
		flush_work(&freq_scale_work);
		this_smartass->idle_exit_time = 0;

		if (atomic_dec_return(&active_count) <= 1) {
			sysfs_remove_group(cpufreq_global_kobject,
					   &smartass_attr_group);
			pm_idle = pm_idle_old;
		}
		break;
	}

	return 0;
}
#ifdef CONFIG_HAS_EARLYSUSPEND
static void smartass_suspend(int cpu, int suspend)
{
	struct smartass_info_s *this_smartass = &per_cpu(smartass_info, smp_processor_id());
	struct cpufreq_policy *policy = this_smartass->cur_policy;
	unsigned int new_freq;

	if (!this_smartass->enable)
		return;

	smartass_update_min_max(this_smartass,policy,suspend);
	if (!suspend) { /* resume at max speed: */
		new_freq = validate_freq(policy,sleep_wakeup_freq);

		dprintk(SMARTASS_DEBUG_JUMPS,"SmartassS: awaking at %d\n",new_freq);

		__cpufreq_driver_target(policy, new_freq,
					CPUFREQ_RELATION_L);
	} else {
		/* to avoid wakeup issues with quick sleep/wakeup don't change actual frequency when entering sleep
		 * to allow some time to settle down. Instead we just reset our statistics (and reset the timer).
		 * Eventually, the timer will adjust the frequency if necessary. */

		this_smartass->freq_change_time_in_idle =
			get_cpu_idle_time_us(cpu,&this_smartass->freq_change_time);

		dprintk(SMARTASS_DEBUG_JUMPS,"SmartassS: suspending at %d\n",policy->cur);
	}

	reset_timer(smp_processor_id(),this_smartass);
}

static void smartass_early_suspend(struct early_suspend *handler) {
	int i;
	if (suspended || sleep_ideal_freq==0) /* disable behavior for sleep_ideal_freq==0 */
		return;
	suspended = 1;
	for_each_online_cpu(i)
		smartass_suspend(i,1);
}

static void smartass_late_resume(struct early_suspend *handler) {
	int i;
	if (!suspended) /* already not suspended so nothing to do */
		return;
	suspended = 0;
	for_each_online_cpu(i)
		smartass_suspend(i,0);
}

static struct early_suspend smartass_power_suspend = {
	.suspend = smartass_early_suspend,
	.resume = smartass_late_resume,
#ifdef CONFIG_MACH_HERO
	.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 1,
#endif
};
#endif
static int __init cpufreq_smartass_init(void)
{
	unsigned int i;
	struct smartass_info_s *this_smartass;
	debug_mask = 0;
	up_rate_us = DEFAULT_UP_RATE_US;
	down_rate_us = DEFAULT_DOWN_RATE_US;
	sleep_ideal_freq = DEFAULT_SLEEP_IDEAL_FREQ;
	sleep_wakeup_freq = DEFAULT_SLEEP_WAKEUP_FREQ;
	awake_ideal_freq = DEFAULT_AWAKE_IDEAL_FREQ;
	sample_rate_jiffies = DEFAULT_SAMPLE_RATE_JIFFIES;
	ramp_up_step = DEFAULT_RAMP_UP_STEP;
	ramp_down_step = DEFAULT_RAMP_DOWN_STEP;
	max_cpu_load = DEFAULT_MAX_CPU_LOAD;
	min_cpu_load = DEFAULT_MIN_CPU_LOAD;

	spin_lock_init(&cpumask_lock);

	suspended = 0;

	/* Initalize per-cpu data: */
	for_each_possible_cpu(i) {
		this_smartass = &per_cpu(smartass_info, i);
		this_smartass->enable = 0;
		this_smartass->cur_policy = 0;
		this_smartass->ramp_dir = 0;
		this_smartass->time_in_idle = 0;
		this_smartass->idle_exit_time = 0;
		this_smartass->freq_change_time = 0;
		this_smartass->freq_change_time_in_idle = 0;
		this_smartass->cur_cpu_load = 0;
		/* intialize timer: */
		init_timer_deferrable(&this_smartass->timer);
		this_smartass->timer.function = cpufreq_smartass_timer;
		this_smartass->timer.data = i;
		work_cpumask_test_and_clear(i);
	}

	/* Scale up is high priority */
	up_wq = alloc_workqueue("ksmartass_up", WQ_HIGHPRI, 1);
	down_wq = alloc_workqueue("ksmartass_down", 0, 1);
	if (!up_wq || !down_wq)
		return -ENOMEM;

	INIT_WORK(&freq_scale_work, cpufreq_smartass_freq_change_time_work);
#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&smartass_power_suspend);
#endif
	return cpufreq_register_governor(&cpufreq_gov_smartass2);
}

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_SMARTASS2
fs_initcall(cpufreq_smartass_init);
#else
module_init(cpufreq_smartass_init);
#endif

static void __exit cpufreq_smartass_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_smartass2);
	destroy_workqueue(up_wq);
	destroy_workqueue(down_wq);
}

module_exit(cpufreq_smartass_exit);

MODULE_AUTHOR ("Erasmux");
MODULE_DESCRIPTION ("'cpufreq_smartass2' - A smart cpufreq governor");
MODULE_LICENSE ("GPL");
