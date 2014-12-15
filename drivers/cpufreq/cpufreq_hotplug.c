/*
 * CPUFreq hotplug governor
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 *   Mike Turquette <mturquette@ti.com>
 *   Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * Based on ondemand governor
 * Copyright (C)  2001 Russell King
 *           (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>,
 *                     Jun Nakajima <jun.nakajima@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/jiffies.h>
#include <linux/kernel_stat.h>
#include <linux/mutex.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <plat/io.h>
#include <mach/io.h>
#include <mach/register.h>
#include <linux/sched/rt.h>
#include <linux/notifier.h>
#include "cpufreq_governor.h"
unsigned int max_cpu_num=NR_CPUS;
unsigned int last_max_cpu_num=NR_CPUS;

/* greater than 80% avg load across online CPUs increases frequency */
#define DEFAULT_UP_FREQ_MIN_LOAD			(80)

/* Keep 10% of idle under the up threshold when decreasing the frequency */
#define DEFAULT_FREQ_DOWN_DIFFERENTIAL			(20)

/* less than 35% avg load across online CPUs decreases frequency */
#define DEFAULT_DOWN_FREQ_MAX_LOAD			(35)

/* default sampling period (uSec) is bogus; 10x ondemand's default for x86 */
#define DEFAULT_SAMPLING_PERIOD				(100000)

/* default number of sampling periods to average before hotplug-in decision */
#define DEFAULT_HOTPLUG_IN_SAMPLING_PERIODS		(5)

/* default number of sampling periods to average before hotplug-out decision */
#define DEFAULT_HOTPLUG_OUT_SAMPLING_PERIODS		(20)
#define DEFAULT_EACHCPU_OUT_SAMPLING_PERIODS		(20)
#define CPU_HOTPLUG_NONE 0
#define CPU_HOTPLUG_PLUG 1
#define CPU_HOTPLUG_UNPLUG 2
extern int select_cpu_for_hotplug(struct task_struct *p, int sd_flags, int wake_flags);

static DEFINE_PER_CPU(struct hg_cpu_dbs_info_s, hg_cpu_dbs_info);
static unsigned int hispeed_freq = 816000;

static cpumask_var_t new_mask;
#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_HOTPLUG
struct cpufreq_governor cpufreq_gov_hotplug;
#endif
static struct task_struct *cpu_hotplug_task;
static struct task_struct *cpu_idle_task;
static int cpu_hotplug_flag = 0;
static DEFINE_PER_CPU(struct hg_cpu_dbs_info_s, hp_cpu_dbs_info);

static DEFINE_MUTEX(dbs_mutex);
DEFINE_SPINLOCK(hotplug_idle_wakeup);
static struct task_struct *NULL_task = NULL;
/************************** sysfs interface ************************/
static struct common_dbs_data hg_dbs_cdata;
/* XXX look at global sysfs macros in cpufreq.h, can those be used here? */

static ssize_t store_sampling_rate(struct dbs_data *dbs_data,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	struct hg_dbs_tuners *hg_tuners = dbs_data->tuners;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	mutex_lock(&dbs_mutex);
	hg_tuners->sampling_rate = input;
	mutex_unlock(&dbs_mutex);

	return count;
}

static ssize_t store_up_threshold(struct dbs_data *dbs_data,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	struct hg_dbs_tuners *hg_tuners = dbs_data->tuners;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input <= hg_tuners->down_threshold) {
		return -EINVAL;
	}

	mutex_lock(&dbs_mutex);
	hg_tuners->up_threshold = input;
	mutex_unlock(&dbs_mutex);

	return count;
}

static ssize_t store_down_differential(struct dbs_data *dbs_data,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	struct hg_dbs_tuners *hg_tuners = dbs_data->tuners;

	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input >= hg_tuners->up_threshold)
		return -EINVAL;

	mutex_lock(&dbs_mutex);
	hg_tuners->down_differential = input;
	mutex_unlock(&dbs_mutex);

	return count;
}

static ssize_t store_down_threshold(struct dbs_data *dbs_data,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	struct hg_dbs_tuners *hg_tuners = dbs_data->tuners;

	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input >= hg_tuners->up_threshold) {
		return -EINVAL;
	}

	mutex_lock(&dbs_mutex);
	hg_tuners->down_threshold = input;
	mutex_unlock(&dbs_mutex);

	return count;
}

static ssize_t store_hotplug_in_sampling_periods(struct dbs_data *dbs_data,
												 const char *buf, size_t count)
{
	unsigned int input;
	unsigned int *temp;
	unsigned int max_windows;
	int ret;
	struct hg_dbs_tuners *hg_tuners = dbs_data->tuners;

	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	/* already using this value, bail out */
	if (input == hg_tuners->hotplug_in_sampling_periods)
		return count;

	mutex_lock(&dbs_mutex);
	ret = count;
	max_windows = max(hg_tuners->hotplug_in_sampling_periods,
			hg_tuners->hotplug_out_sampling_periods);

	/* no need to resize array */
	if (input <= max_windows) {
		hg_tuners->hotplug_in_sampling_periods = input;
		goto out;
	}

	/* resize array */
	temp = kmalloc((sizeof(unsigned int) * input), GFP_KERNEL);

	if (!temp || IS_ERR(temp)) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(temp, hg_tuners->hotplug_load_history,
			(max_windows * sizeof(unsigned int)));
	kfree(hg_tuners->hotplug_load_history);

	/* replace old buffer, old number of sampling periods & old index */
	hg_tuners->hotplug_load_history = temp;
	hg_tuners->hotplug_in_sampling_periods = input;
	hg_tuners->hotplug_load_index = max_windows;
out:
	mutex_unlock(&dbs_mutex);

	return ret;
}

static ssize_t store_hotplug_out_sampling_periods(struct dbs_data *dbs_data,
												  const char *buf, size_t count)
{
	unsigned int input;
	unsigned int *temp;
	unsigned int max_windows;
	int ret;
	struct hg_dbs_tuners *hg_tuners = dbs_data->tuners;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	/* already using this value, bail out */
	if (input == hg_tuners->hotplug_out_sampling_periods)
		return count;

	mutex_lock(&dbs_mutex);
	ret = count;
	max_windows = max(hg_tuners->hotplug_in_sampling_periods,
			hg_tuners->hotplug_out_sampling_periods);

	/* no need to resize array */
	if (input <= max_windows) {
		hg_tuners->hotplug_out_sampling_periods = input;
		goto out;
	}

	/* resize array */
	temp = kmalloc((sizeof(unsigned int) * input), GFP_KERNEL);

	if (!temp || IS_ERR(temp)) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(temp, hg_tuners->hotplug_load_history,
			(max_windows * sizeof(unsigned int)));
	kfree(hg_tuners->hotplug_load_history);

	/* replace old buffer, old number of sampling periods & old index */
	hg_tuners->hotplug_load_history = temp;
	hg_tuners->hotplug_out_sampling_periods = input;
	hg_tuners->hotplug_load_index = max_windows;
out:
	mutex_unlock(&dbs_mutex);

	return ret;
}

static ssize_t store_ignore_nice_load(struct dbs_data *dbs_data,
				      const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	unsigned int j;
	struct hg_dbs_tuners *hg_tuners = dbs_data->tuners;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	mutex_lock(&dbs_mutex);
	if (input == hg_tuners->ignore_nice_load) { /* nothing to do */
		mutex_unlock(&dbs_mutex);
		return count;
	}
	hg_tuners->ignore_nice_load = input;

	/* we need to re-evaluate prev_cpu_idle */
	for_each_online_cpu(j) {
		struct hg_cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(hp_cpu_dbs_info, j);
		dbs_info->cdbs.prev_cpu_idle = get_cpu_idle_time(j,
						&dbs_info->cdbs.prev_cpu_wall, hg_tuners->io_is_busy);
		if (hg_tuners->ignore_nice_load)
			dbs_info->cdbs.prev_cpu_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE];

	}
	mutex_unlock(&dbs_mutex);

	return count;
}

static ssize_t store_io_is_busy(struct dbs_data *dbs_data,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	struct hg_dbs_tuners *hg_tuners = dbs_data->tuners;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	mutex_lock(&dbs_mutex);
	hg_tuners->io_is_busy = !!input;
	mutex_unlock(&dbs_mutex);

	return count;
}

static ssize_t store_cpu_num_unplug_once(struct dbs_data *dbs_data,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	struct hg_dbs_tuners *hg_tuners = dbs_data->tuners;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if(input >= NR_CPUS || input <= 0){
		return -EINVAL;
	}

	mutex_lock(&dbs_mutex);
	hg_tuners->cpu_num_unplug_once = input;
	mutex_unlock(&dbs_mutex);

	return count;
}

static ssize_t store_cpu_num_plug_once(struct dbs_data *dbs_data,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	struct hg_dbs_tuners *hg_tuners = dbs_data->tuners;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if(input >= NR_CPUS || input <= 0){
		return -EINVAL;
	}

	mutex_lock(&dbs_mutex);
	hg_tuners->cpu_num_plug_once = input;
	mutex_unlock(&dbs_mutex);

	return count;
}
static ssize_t store_hotplug_min_freq(struct dbs_data *dbs_data,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	struct hg_dbs_tuners *hg_tuners = dbs_data->tuners;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if(input >= NR_CPUS || input <= 0){
		return -EINVAL;
	}

	mutex_lock(&dbs_mutex);
	hg_tuners->hotplug_min_freq = input;
	mutex_unlock(&dbs_mutex);

	return count;
}

static ssize_t store_hotplug_max_freq(struct dbs_data *dbs_data,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	struct hg_dbs_tuners *hg_tuners = dbs_data->tuners;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if(input >= NR_CPUS || input <= 0){
		return -EINVAL;
	}

	mutex_lock(&dbs_mutex);
	hg_tuners->hotplug_max_freq = input;
	mutex_unlock(&dbs_mutex);

	return count;
}
/* cpufreq_hotplug Governor Tunables */
show_store_one(hg, sampling_rate);
show_store_one(hg, up_threshold);
show_store_one(hg, down_differential);
show_store_one(hg, down_threshold);
show_store_one(hg, hotplug_in_sampling_periods);
show_store_one(hg, hotplug_out_sampling_periods);
show_store_one(hg, ignore_nice_load);
show_store_one(hg, io_is_busy);
show_store_one(hg, cpu_num_unplug_once);
show_store_one(hg, cpu_num_plug_once);
show_store_one(hg, hotplug_min_freq);
show_store_one(hg, hotplug_max_freq);

gov_sys_pol_attr_rw(sampling_rate);
gov_sys_pol_attr_rw(down_differential);
gov_sys_pol_attr_rw(up_threshold);
gov_sys_pol_attr_rw(down_threshold);
gov_sys_pol_attr_rw(ignore_nice_load);
gov_sys_pol_attr_rw(hotplug_in_sampling_periods);
gov_sys_pol_attr_rw(hotplug_out_sampling_periods);
gov_sys_pol_attr_rw(io_is_busy);
gov_sys_pol_attr_rw(cpu_num_unplug_once);
gov_sys_pol_attr_rw(cpu_num_plug_once);
gov_sys_pol_attr_rw(hotplug_min_freq);
gov_sys_pol_attr_rw(hotplug_max_freq);

static struct attribute *dbs_attributes[] = {
	&sampling_rate_gov_sys.attr,
	&up_threshold_gov_sys.attr,
	&down_differential_gov_sys.attr,
	&down_threshold_gov_sys.attr,
	&hotplug_in_sampling_periods_gov_sys.attr,
	&hotplug_out_sampling_periods_gov_sys.attr,
	&ignore_nice_load_gov_sys.attr,
	&io_is_busy_gov_sys.attr,
	&cpu_num_unplug_once_gov_sys.attr,
	&cpu_num_plug_once_gov_sys.attr,
	&hotplug_min_freq_gov_sys.attr,
	&hotplug_max_freq_gov_sys.attr,
	NULL
};

static struct attribute_group hg_attr_group_gov_sys = {
	.attrs = dbs_attributes,
	.name = "hotplug",
};

static struct attribute *dbs_attributes_gov_pol[] = {
	&sampling_rate_gov_pol.attr,
	&up_threshold_gov_pol.attr,
	&down_differential_gov_pol.attr,
	&down_threshold_gov_pol.attr,
	&hotplug_in_sampling_periods_gov_pol.attr,
	&hotplug_out_sampling_periods_gov_pol.attr,
	&ignore_nice_load_gov_pol.attr,
	&io_is_busy_gov_pol.attr,
	&cpu_num_unplug_once_gov_pol.attr,
	&cpu_num_plug_once_gov_pol.attr,
	&hotplug_min_freq_gov_pol.attr,
	&hotplug_max_freq_gov_pol.attr,
	NULL
};

static struct attribute_group hg_attr_group_gov_pol = {
	.attrs = dbs_attributes_gov_pol,
	.name = "hotplug",
};
/************************** sysfs end ************************/
static int hg_init(struct dbs_data *dbs_data)
{
	struct hg_dbs_tuners *tuners;

	tuners = kzalloc(sizeof(struct hg_dbs_tuners), GFP_KERNEL);
	if (!tuners) {
		pr_err("%s: kzalloc failed\n", __func__);
		return -ENOMEM;
	}
	tuners->up_threshold				=	DEFAULT_UP_FREQ_MIN_LOAD;
	tuners->down_differential			=	DEFAULT_FREQ_DOWN_DIFFERENTIAL;
	tuners->down_threshold				=	DEFAULT_DOWN_FREQ_MAX_LOAD;
	tuners->hotplug_in_sampling_periods =	DEFAULT_HOTPLUG_IN_SAMPLING_PERIODS;
	tuners->hotplug_out_sampling_periods =	DEFAULT_HOTPLUG_OUT_SAMPLING_PERIODS;
	tuners->each_cpu_out_sampling_periods	=	DEFAULT_EACHCPU_OUT_SAMPLING_PERIODS;
	tuners->hotplug_load_index			=	0;
	tuners->ignore_nice_load			=	0;
	tuners->io_is_busy					=	0;
	tuners->cpu_num_unplug_once			=	2;
	tuners->each_cpu_num_unplug_once	=	2;
	tuners->cpu_num_plug_once			=	1;
	tuners->hotplug_min_freq			=	96000;
	tuners->hotplug_max_freq			=	96000;

	dbs_data->tuners = tuners;
	dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
		jiffies_to_usecs(10);
	mutex_init(&dbs_data->mutex);
	return 0;
}

static void hg_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
}
static int cpu_idle_thread(void *data)
{
	int cpu;
	struct hg_cpu_dbs_info_s *dbs_info = NULL;
	struct cpufreq_policy *policy = NULL;
	struct dbs_data *dbs_data = NULL;
	struct cpu_dbs_common_info *cdbs = NULL;
	struct hg_dbs_tuners *hg_tuners = NULL;
	unsigned int sampling_rate;

	cpu = get_cpu();
	put_cpu();
	dbs_info = &per_cpu(hg_cpu_dbs_info, cpu);
	while(1){
		if(dbs_info->enable)
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		set_current_state(TASK_RUNNING);
	}
	policy = dbs_info->cdbs.cur_policy;
	dbs_data = policy->governor_data;
	cdbs = dbs_data->cdata->get_cpu_cdbs(cpu);
	hg_tuners = dbs_data->tuners;
	sampling_rate = hg_tuners->sampling_rate;

	dbs_info = &per_cpu(hg_cpu_dbs_info, policy->cpu);
	while(1){
		if (kthread_should_stop())
			break;
		if(!mutex_trylock(&dbs_info->cdbs.timer_mutex))
			goto wait_next_event;
		if (!dbs_info->enable) {
			mutex_unlock(&dbs_info->cdbs.timer_mutex);
			goto wait_next_event;
		}
		gov_cancel_work(dbs_data, policy);
		gov_queue_work(dbs_data, policy,
						delay_for_sampling_rate(sampling_rate), true);
		mutex_unlock(&dbs_info->cdbs.timer_mutex);
wait_next_event:
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		set_current_state(TASK_RUNNING);
	}
	return 1;
}
void cpufreq_set_max_cpu_num(unsigned int cpu_num)
{
	if(cpu_num>=NR_CPUS){
		max_cpu_num=NR_CPUS;
	}else{
		if(cpu_num>last_max_cpu_num)
			max_cpu_num=cpu_num;
		else{
			max_cpu_num=cpu_num;
			if(cpu_num>=num_online_cpus())
				return ;
			cpu_hotplug_flag = CPU_HOTPLUG_UNPLUG;
			if(cpu_hotplug_task)
				wake_up_process(cpu_hotplug_task);
		}
	}
	last_max_cpu_num=max_cpu_num;
	return ;
}
static int __ref cpu_hotplug_thread(void *data)
{
	int i, j,target_cpu = 1;
	unsigned long flags, cpu_down_num;
	int cpu;
	int *hotplug_flag = NULL;
	struct hg_cpu_dbs_info_s *dbs_info = NULL;
	struct cpufreq_policy *policy = NULL;
	struct dbs_data *dbs_data = NULL;
	struct hg_dbs_tuners *hg_tuners = NULL;

	hotplug_flag = (int *)data;
	cpu = get_cpu();
	put_cpu();
	dbs_info = &per_cpu(hg_cpu_dbs_info, cpu);
	while(1){
		if(dbs_info->enable)
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		set_current_state(TASK_RUNNING);
	}
	policy = dbs_info->cdbs.cur_policy;
	dbs_data = policy->governor_data;
	hg_tuners = dbs_data->tuners;

	dbs_info = &per_cpu(hg_cpu_dbs_info, policy->cpu);

	while(1){
		if (kthread_should_stop())
			break;
		mutex_lock(&dbs_info->hotplug_thread_mutex);
		if(!dbs_info->enable)
			goto wait_next_hotplug;
		if(*hotplug_flag == CPU_HOTPLUG_PLUG){
			*hotplug_flag = CPU_HOTPLUG_NONE;
			j = 0;
			for(i = 0; i < max_cpu_num; i++){
				if(cpu_online(i))
					continue;
				j++;
				cpu_up(i);
				cpumask_set_cpu(i, tsk_cpus_allowed(NULL_task));
				if(j >= hg_tuners->cpu_num_plug_once)
					break;
			}
		}else if(*hotplug_flag == CPU_HOTPLUG_UNPLUG){
			*hotplug_flag = CPU_HOTPLUG_NONE;
			cpu_down_num = 0;
			for(i = 0; i < num_online_cpus()-1; i++){
				raw_spin_lock_irqsave(&NULL_task->pi_lock, flags);
				target_cpu = select_cpu_for_hotplug(NULL_task, SD_BALANCE_EXEC, 0);
				raw_spin_unlock_irqrestore(&NULL_task->pi_lock, flags);
				if(target_cpu == 0){
					i--;
					goto clear_cpu;
				}
				if(!cpu_active(target_cpu)){
					goto clear_cpu;
				}
				cpu_down(target_cpu);
				cpu_down_num++;
clear_cpu:
				cpumask_clear_cpu(target_cpu, tsk_cpus_allowed(NULL_task));
				if(cpu_down_num >= hg_tuners->cpu_num_unplug_once ||
				   cpu_down_num >= hg_tuners->each_cpu_num_unplug_once
				   ){
					break;
				}
			}
		}
wait_next_hotplug:
		mutex_unlock(&dbs_info->hotplug_thread_mutex);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		set_current_state(TASK_RUNNING);
	}
	return 1;
}

static void hg_check_cpu(int cpu, unsigned int max_load)
{
	/* largest CPU load in terms of frequency */
	unsigned int max_load_freq = 0;
	/* average load across all enabled CPUs */
	unsigned int avg_load = 0;
	/* average load across multiple sampling periods for hotplug events */
	unsigned int hotplug_in_avg_load = 0;
	unsigned int hotplug_out_avg_load = 0;
	unsigned int each_cpu_out_avg_load[NR_CPUS];
	/* number of sampling periods averaged for hotplug decisions */
	unsigned int periods;
	unsigned int i, j, k;

	struct hg_cpu_dbs_info_s *dbs_info = &per_cpu(hg_cpu_dbs_info, cpu);
	struct cpufreq_policy *policy = dbs_info->cdbs.cur_policy;
	struct dbs_data *dbs_data = policy->governor_data;
	struct hg_dbs_tuners *hg_tuners = dbs_data->tuners;
	memset(each_cpu_out_avg_load,0,sizeof(each_cpu_out_avg_load));
	avg_load = hg_tuners->hotplug_load_history[hg_tuners->hotplug_load_index];
	max_load_freq = hg_tuners->max_load_freq;
	/*
	 * hotplug load accounting
	 * average load over multiple sampling periods
	 */
	/* how many sampling periods do we use for hotplug decisions? */
	periods = max(hg_tuners->hotplug_in_sampling_periods,
			hg_tuners->hotplug_out_sampling_periods);
	periods = max(periods, hg_tuners->each_cpu_out_sampling_periods);

	/* compute average load across in & out sampling periods */
	for (i = 0, j = hg_tuners->hotplug_load_index;
			i < periods; i++, j--) {
		if (i < hg_tuners->hotplug_in_sampling_periods)
			hotplug_in_avg_load +=
				hg_tuners->hotplug_load_history[j];
		if (i < hg_tuners->hotplug_out_sampling_periods)
			hotplug_out_avg_load +=
				hg_tuners->hotplug_load_history[j];
		if (i < hg_tuners->each_cpu_out_sampling_periods){
			for(k = 0; k < NR_CPUS; k++)
				each_cpu_out_avg_load[k] += hg_tuners->cpu_load_history[k][j];
		}
		if (j == 0)
			j = periods;
	}

	hotplug_in_avg_load = hotplug_in_avg_load /
		hg_tuners->hotplug_in_sampling_periods;

	hotplug_out_avg_load = hotplug_out_avg_load /
		hg_tuners->hotplug_out_sampling_periods;

	for(k = 0; k < NR_CPUS; k++)
		each_cpu_out_avg_load[k] /= hg_tuners->each_cpu_out_sampling_periods;
	/* return to first element if we're at the circular buffer's end */
	if (++hg_tuners->hotplug_load_index == periods)
		hg_tuners->hotplug_load_index = 0;

	/* check if auxiliary CPU is needed based on avg_load */
	if (avg_load > hg_tuners->up_threshold) {
		/* should we enable auxillary CPUs? */
		if (num_online_cpus() < NR_CPUS && hotplug_in_avg_load >
			hg_tuners->up_threshold && policy->cur >=  hg_tuners->hotplug_max_freq) {
			/* hotplug with cpufreq is nasty
			 * a call to cpufreq_governor_dbs may cause a lockup.
			 * wq is not running here so its safe.
			 */
			cpu_hotplug_flag = CPU_HOTPLUG_PLUG;
			wake_up_process(cpu_hotplug_task);
			goto out;
		}
	}
	if (max_load > hg_tuners->up_threshold ||
		!(avg_load < hg_tuners->down_threshold &&
		(num_online_cpus() > 1 && hotplug_out_avg_load < hg_tuners->down_threshold))){
		if (num_online_cpus() > 2){
			i = 0;
			for(k = 0; k < NR_CPUS; k++){
				if(each_cpu_out_avg_load[k] < hg_tuners->down_threshold)
					i++;
			}
			if(i > 1){
				hg_tuners->each_cpu_num_unplug_once = i - 1;
				cpu_hotplug_flag = CPU_HOTPLUG_UNPLUG;
				printk(KERN_DEBUG"-----hotplug:%u\n", i);
				wake_up_process(cpu_hotplug_task);
			}
			else
				hg_tuners->each_cpu_num_unplug_once = hg_tuners->cpu_num_unplug_once;
		}
	}
	/* check for frequency increase based on max_load */
	if (max_load > hg_tuners->up_threshold) {
#if 0
		if (num_online_cpus() < NR_CPUS) {
			/* hotplug with cpufreq is nasty
			 * a call to cpufreq_governor_dbs may cause a lockup.
			 * wq is not running here so its safe.
			 */
			cpu_hotplug_flag = CPU_HOTPLUG_PLUG;
			wake_up_process(cpu_hotplug_task);
			goto out;
		}
#endif
		/* increase to highest frequency supported */
		if (policy->cur < policy->max){
			dbs_info->requested_freq = policy->max;
			__cpufreq_driver_target(policy, policy->max,
					CPUFREQ_RELATION_H);
		}
		goto out;
	}

	/* check for frequency decrease */
	if (avg_load < hg_tuners->down_threshold) {
		/* are we at the minimum frequency already? */
		if (policy->cur <= hg_tuners->hotplug_min_freq) {
			/* should we disable auxillary CPUs? */
			if (num_online_cpus() > 1 && hotplug_out_avg_load <
				hg_tuners->down_threshold) {
				cpu_hotplug_flag = CPU_HOTPLUG_UNPLUG;
				hg_tuners->each_cpu_num_unplug_once = hg_tuners->cpu_num_unplug_once;
				wake_up_process(cpu_hotplug_task);
				goto out;
			}
		}
	}

	if ((max_load > hg_tuners->up_threshold - hg_tuners->down_differential)
		 &&(policy->cur < policy->max)) {
		unsigned int freq_next;

		freq_next = hispeed_freq;

		if (freq_next < policy->min)
			freq_next = policy->min;
		if (freq_next > policy->max)
			freq_next = policy->max;

		if(freq_next == policy->cur)
			goto out;

		dbs_info->requested_freq = freq_next;

		__cpufreq_driver_target(policy, freq_next,
					 CPUFREQ_RELATION_L);
		goto out;
	}
	/*
	 * go down to the lowest frequency which can sustain the load by
	 * keeping 30% of idle in order to not cross the up_threshold
	 */
	if ((max_load_freq <
	    (hg_tuners->up_threshold - hg_tuners->down_differential) *
	     policy->cur) && (policy->cur > policy->min)) {
		unsigned int freq_next;

		freq_next = max_load_freq /
				(hg_tuners->down_threshold);

		if (freq_next < policy->min)
			freq_next = policy->min;

		if(freq_next == policy->cur)
			goto out;

		dbs_info->requested_freq = freq_next;

		__cpufreq_driver_target(policy, freq_next,
					 CPUFREQ_RELATION_L);
	}
out:
	//printk(KERN_DEBUG"hg dbs: %u %u %u o avg:%u i avg:%u %u \n", policy->cur, policy->min,
	//	   max_load_freq, hotplug_out_avg_load, hotplug_in_avg_load, max_load);
	return;
}

static void hg_dbs_timer(struct work_struct *work)
{
	struct hg_cpu_dbs_info_s *dbs_info =
		container_of(work, struct hg_cpu_dbs_info_s, cdbs.work.work);
	struct cpufreq_policy *policy = dbs_info->cdbs.cur_policy;
	unsigned int cpu = policy->cpu;
	struct hg_cpu_dbs_info_s *core_dbs_info = &per_cpu(hg_cpu_dbs_info,
			cpu);
	struct dbs_data *dbs_data = policy->governor_data;
	struct hg_dbs_tuners *hg_tuners = dbs_data->tuners;

	/* We want all related CPUs to do sampling nearly on same jiffy */
	int delay = delay_for_sampling_rate(hg_tuners->sampling_rate);
	bool modify_all = true;

	mutex_lock(&core_dbs_info->cdbs.timer_mutex);
	if (!core_dbs_info->enable) {
		mutex_unlock(&core_dbs_info->cdbs.timer_mutex);
		return;
	}
	if (!need_load_eval(&core_dbs_info->cdbs, hg_tuners->sampling_rate))
		modify_all = false;
	else
		dbs_check_cpu(dbs_data, cpu);
	gov_queue_work(dbs_data, dbs_info->cdbs.cur_policy, delay, modify_all);
	mutex_unlock(&core_dbs_info->cdbs.timer_mutex);
}

static void cpufreq_hotplug_idle_start(void)
{
#if 0
	int cpu = get_cpu();
	put_cpu();
	struct hg_cpu_dbs_info_s *dbs_info = &per_cpu(hg_cpu_dbs_info, cpu);
	struct cpufreq_policy *policy = dbs_info->cdbs.cur_policy;
	struct dbs_data *dbs_data = policy->governor_data;
	struct cpu_dbs_common_info *cdbs = dbs_data->cdata->get_cpu_cdbs(cpu);
	struct hg_dbs_tuners *hg_tuners = dbs_data->tuners;
	unsigned int sampling_rate = hg_tuners->sampling_rate;

	dbs_info = &per_cpu(hg_cpu_dbs_info, policy->cpu);

	if(!mutex_trylock(&dbs_info->cdbs.timer_mutex))
		return;
	if (!dbs_info->enable) {
		mutex_unlock(&dbs_info->cdbs.timer_mutex);
		return;
	}

	if (dbs_info->requested_freq != policy->min) {
		/*
		 * Entering idle while not at lowest speed.  On some
		 * platforms this can hold the other CPU(s) at that speed
		 * even though the CPU is idle. Set a timer to re-evaluate
		 * speed so this idle CPU doesn't hold the other CPUs above
		 * min indefinitely.  This should probably be a quirk of
		 * the CPUFreq driver.
		 */
		if (!pending)
			cpufreq_interactive_timer_resched(pcpu);
	}

	mutex_unlock(&dbs_info->cdbs.timer_mutex);
#endif
}
static void cpufreq_hotplug_idle_end(void)
{
	unsigned long flags;

	if(spin_trylock_irqsave(&hotplug_idle_wakeup, flags)){
		wake_up_process(cpu_idle_task);
		spin_unlock_irqrestore(&hotplug_idle_wakeup, flags);
	}
}

static int cpufreq_hotplug_idle_notifier(struct notifier_block *nb,
					     unsigned long val, void *data)
{
	switch (val) {
	case IDLE_START:
		cpufreq_hotplug_idle_start();
		break;
	case IDLE_END:
		cpufreq_hotplug_idle_end();
		break;
	}

	return 0;
}

static struct notifier_block cpufreq_hotplug_idle_nb = {
	.notifier_call = cpufreq_hotplug_idle_notifier,
};


define_get_cpu_dbs_routines(hg_cpu_dbs_info);

static struct hg_ops hg_ops = {
	.notifier_block = &cpufreq_hotplug_idle_nb,
};

static struct common_dbs_data hg_dbs_cdata = {
	.governor = GOV_HOTPLUG,
	.attr_group_gov_sys = &hg_attr_group_gov_sys,
	.attr_group_gov_pol = &hg_attr_group_gov_pol,
	.get_cpu_cdbs = get_cpu_cdbs,
	.get_cpu_dbs_info_s = get_cpu_dbs_info_s,
	.gov_dbs_timer = hg_dbs_timer,
	.gov_check_cpu = hg_check_cpu,
	.gov_ops = &hg_ops,
	.init = hg_init,
	.exit = hg_exit,
};

static int hg_cpufreq_governor_dbs(struct cpufreq_policy *policy,
		unsigned int event)
{
	return cpufreq_governor_dbs(policy, &hg_dbs_cdata, event);
}
static int do_null_task(void *data)
{
	while(1){
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		set_current_state(TASK_RUNNING);
		printk("---add for hotplug governor\n");
	}
	return 1;
}
struct cpufreq_governor cpufreq_gov_hotplug = {
	.name			= "hotplug",
	.governor		= hg_cpufreq_governor_dbs,
	.max_transition_latency	= TRANSITION_LATENCY_LIMIT,
	.owner			= THIS_MODULE,
};
static int __init cpufreq_gov_dbs_init(void)
{
	int err, i = 0;
	cputime64_t wall;
	u64 idle_time;
	int cpu = get_cpu();
	struct sched_param param = { .sched_priority = MAX_RT_PRIO-1 };

	idle_time = get_cpu_idle_time_us(cpu, &wall);
	put_cpu();
	if (idle_time == -1ULL) {
		pr_err("cpufreq-hotplug: %s: assumes CONFIG_NO_HZ\n",
				__func__);
		return -EINVAL;
	}

	if (!alloc_cpumask_var(&new_mask, GFP_KERNEL))
		return -ENOMEM;
	/*******************NULL task******************/
	NULL_task = kthread_create(do_null_task, NULL, "NULL_task_for_hotplug");
	if(!NULL_task){
		err = PTR_ERR(NULL_task);
		NULL_task = NULL;
		return err;
	}

	for(i = 1; i < NR_CPUS; i++){
		cpumask_set_cpu(i, tsk_cpus_allowed(NULL_task));
	}
	cpumask_clear_cpu(0, tsk_cpus_allowed(NULL_task));
	wake_up_process(NULL_task);

	/*******************cpu hotplug task******************/
	cpu_hotplug_task =
		kthread_create(cpu_hotplug_thread, &cpu_hotplug_flag,
			       "cpu_hotplug_gdbs");
	if (IS_ERR(cpu_hotplug_task))
		return PTR_ERR(cpu_hotplug_task);

	sched_setscheduler_nocheck(cpu_hotplug_task, SCHED_FIFO, &param);
	get_task_struct(cpu_hotplug_task);
	cpu_hotplug_flag = CPU_HOTPLUG_NONE;
	wake_up_process(cpu_hotplug_task);
	/*******************cpu idle task******************/
	cpu_idle_task =
		kthread_create_on_cpu(cpu_idle_thread, NULL, 0,
			       "cpu_idle_gdbs");
	if (IS_ERR(cpu_idle_task)){
		printk("------ Error: create hotplug scaling idle thread fail\n");
		return PTR_ERR(cpu_idle_task);
	}
	sched_setscheduler_nocheck(cpu_idle_task, SCHED_FIFO, &param);
	get_task_struct(cpu_idle_task);
	wake_up_process(cpu_idle_task);

	err = cpufreq_register_governor(&cpufreq_gov_hotplug);

	return err;
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_hotplug);
	free_cpumask_var(new_mask);
	kthread_stop(cpu_idle_task);
	put_task_struct(cpu_idle_task);
	kthread_stop(NULL_task);
	kthread_stop(cpu_hotplug_task);
	put_task_struct(cpu_hotplug_task);
}

MODULE_AUTHOR("Mike Turquette <mturquette@ti.com>");
MODULE_DESCRIPTION("'cpufreq_hotplug' - cpufreq governor for dynamic frequency scaling and CPU hotplugging");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_HOTPLUG
fs_initcall(cpufreq_gov_dbs_init);
#else
module_init(cpufreq_gov_dbs_init);
#endif
module_exit(cpufreq_gov_dbs_exit);
