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
#include <linux/fb.h>
#include <linux/pm_qos.h>

#include <mach/cpufreq.h>

/*
 * dbs is used in this file as a shortform for demandbased switching
 * It helps to keep variable names smaller, simpler
 */

#define DEF_FREQUENCY_DOWN_DIFFERENTIAL		(10)
#define DEF_FREQUENCY_UP_THRESHOLD		(80)
#define DEF_SAMPLING_DOWN_FACTOR		(1)
#define MAX_SAMPLING_DOWN_FACTOR		(100000)
#define MICRO_FREQUENCY_DOWN_DIFFERENTIAL	(3)
#define MICRO_FREQUENCY_UP_THRESHOLD		(90)
#define MICRO_FREQUENCY_MIN_SAMPLE_RATE		(80000)
#define MIN_FREQUENCY_UP_THRESHOLD		(10)
#define MAX_FREQUENCY_UP_THRESHOLD		(100)
#define MAX_FREQ_BLANK				(1600000)

#define DEF_FREQUENCY_UP_THRESHOLD_L		(50)
#define DEF_FREQUENCY_UP_STEP_LEVEL_B		(1200000)
#define DEF_FREQUENCY_UP_STEP_LEVEL_L		(600000)
#define DEF_FREQUENCY_DOWN_STEP_LEVEL           (800000)
#define DEF_FREQUENCY_DOWN_DIFFER_L		(20)
#define DEF_FREQUENCY_HIGH_ZONE			(1200000)
#define DEF_FREQUENCY_CONSERVATIVE_STEP		(100000)
#define MICRO_FREQUENCY_UP_THRESHOLD_H		(90)
#define MICRO_FREQUENCY_UP_THRESHOLD_L		(60)
#define MICRO_FREQUENCY_UP_STEP_LEVEL_B		(1200000)
#define MICRO_FREQUENCY_UP_STEP_LEVEL_L		(600000)
#define MICRO_FREQUENCY_DOWN_STEP_LEVEL		(800000)
#define MICRO_FREQUENCY_DOWN_DIFFER_L		(20)
#define MIN_FREQUENCY_UP_STEP_LEVEL		(500000)
#define MAX_FREQUENCY_UP_STEP_LEVEL		(1800000)

#define BIN2_FREQUENCY_UP_STEP_LEVEL_L (450000)
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
#define MIN_SAMPLING_RATE_RATIO			(1)

static unsigned int min_sampling_rate;

#ifdef CONFIG_EXYNOS5_DYNAMIC_CPU_HOTPLUG
static bool lcd_is_on;
#endif

#define LATENCY_MULTIPLIER			(1000)
#define MIN_LATENCY_MULTIPLIER			(100)
#define TRANSITION_LATENCY_LIMIT		(10 * 1000 * 1000)

static void do_dbs_timer(struct work_struct *work);
static int cpufreq_governor_dbs(struct cpufreq_policy *policy,
				unsigned int event);

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_ONDEMAND
static
#endif
struct cpufreq_governor cpufreq_gov_ondemand = {
       .name                   = "ondemand",
       .governor               = cpufreq_governor_dbs,
       .max_transition_latency = TRANSITION_LATENCY_LIMIT,
       .owner                  = THIS_MODULE,
};

/* Sampling types */
enum {DBS_NORMAL_SAMPLE, DBS_SUB_SAMPLE};

struct cpu_dbs_info_s {
	cputime64_t prev_cpu_idle;
	cputime64_t prev_cpu_iowait;
	cputime64_t prev_cpu_wall;
	cputime64_t prev_cpu_nice;
	struct cpufreq_policy *cur_policy;
	struct delayed_work work;
	struct cpufreq_frequency_table *freq_table;
	unsigned int freq_lo;
	unsigned int freq_lo_jiffies;
	unsigned int freq_hi_jiffies;
	unsigned int rate_mult;
	int cpu;
	unsigned int sample_type:1;
	/*
	 * percpu mutex that serializes governor limit change with
	 * do_dbs_timer invocation. We do not want do_dbs_timer to run
	 * when user is changing the governor or limits.
	 */
	struct mutex timer_mutex;
};
static DEFINE_PER_CPU(struct cpu_dbs_info_s, od_cpu_dbs_info);

static unsigned int dbs_enable;	/* number of CPUs using this policy */

/*
 * dbs_mutex protects dbs_enable in governor start/stop.
 */
static DEFINE_MUTEX(dbs_mutex);

static struct dbs_tuners {
	unsigned int sampling_rate;
	unsigned int up_threshold;
	unsigned int down_differential;
	unsigned int ignore_nice;
	unsigned int sampling_down_factor;
	unsigned int powersave_bias;
	unsigned int io_is_busy;
	unsigned int up_threshold_l;
	unsigned int up_threshold_h;
	unsigned int up_step_level_b;
	unsigned int up_step_level_l;
	unsigned int down_step_level;
	unsigned int down_differ_l;
	unsigned int high_freq_zone;
	unsigned int conservative_step;
	bool up_conservative_mode;
	unsigned int max_freq_blank;
} dbs_tuners_ins = {
	.up_threshold = DEF_FREQUENCY_UP_THRESHOLD,
	.sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR,
	.down_differential = DEF_FREQUENCY_DOWN_DIFFERENTIAL,
	.ignore_nice = 0,
	.powersave_bias = 0,
	.up_threshold_l = DEF_FREQUENCY_UP_THRESHOLD_L,
	.up_threshold_h = MICRO_FREQUENCY_UP_THRESHOLD_L,
	.up_step_level_b = DEF_FREQUENCY_UP_STEP_LEVEL_B,
	.up_step_level_l = DEF_FREQUENCY_UP_STEP_LEVEL_L,
	.down_step_level = DEF_FREQUENCY_DOWN_STEP_LEVEL,
	.down_differ_l = DEF_FREQUENCY_DOWN_DIFFER_L,
	.high_freq_zone = DEF_FREQUENCY_HIGH_ZONE,
	.conservative_step = DEF_FREQUENCY_CONSERVATIVE_STEP,
	.up_conservative_mode = false,
	.max_freq_blank = MAX_FREQ_BLANK,
};

#ifdef CONFIG_EXYNOS5_DYNAMIC_CPU_HOTPLUG
/*
 * Increase this value if cpu load is less than base load of hotplug
 * out condition.
 */
#define ENABLE_HOTPLUG_OUT_H

/* This means permitted consecutive boost level */
#define BOOST_LV_CNT   			20

#define HOTPLUG_OUT_LOAD		10

#define HOTPLUG_OUT_CNT_H		6
#define HOTPLUG_OUT_CNT_L		5

#define HOTPLUG_TRANS_H			1600000
#define HOTPLUG_TRANS_H_CPUS		2
#define HOTPLUG_TRANS_L			250000
#define HOTPLUG_TRANS_L_CPUS		1

#define UP_THRESHOLD_FB_BLANK		(90)

static struct cpumask out_cpus;
static struct cpumask to_be_out_cpus;
static struct work_struct hotplug_work;
static bool hotplug_out;
static unsigned int consecutive_boost_level;

static DEFINE_PER_CPU(unsigned int, hotplug_out_cnt_h);
static int hotplug_out_cnt_l;

static DEFINE_MUTEX(hotplug_mutex);

static void __do_hotplug(void)
{
	unsigned int cpu, ret;

	if (hotplug_out) {
		for_each_cpu(cpu, &to_be_out_cpus) {
			if (cpu == 0)
				continue;

			ret = cpu_down(cpu);
			if (ret) {
				pr_debug("%s: CPU%d down fail: %d\n",
					__func__, cpu, ret);
				continue;
			} else {
				cpumask_set_cpu(cpu, &out_cpus);
			}
		}
		cpumask_clear(&to_be_out_cpus);
	} else {
		for_each_cpu(cpu, &out_cpus) {
			if (cpu == 0)
				continue;

			ret = cpu_up(cpu);
			if (ret) {
				pr_debug("%s: CPU%d up fail: %d\n",
					__func__, cpu, ret);
				continue;
			} else {
				cpumask_clear_cpu(cpu, &out_cpus);
			}
		}
	}
}

static void do_hotplug(struct work_struct *work)
{
	mutex_lock(&hotplug_mutex);
	__do_hotplug();
	mutex_unlock(&hotplug_mutex);

	return;
}

static int fb_state_change(struct notifier_block *nb,
		unsigned long val, void *data)
{
	struct fb_event *evdata = data;
	unsigned int blank;

	if (val != FB_EVENT_BLANK)
		return 0;

	blank = *(int *)evdata->data;

	switch (blank) {
	case FB_BLANK_POWERDOWN:
		dbs_tuners_ins.up_threshold_l = UP_THRESHOLD_FB_BLANK;
		lcd_is_on = false;
		break;
	case FB_BLANK_UNBLANK:
		/*
		 * LCD blank CPU qos is set by exynos-ikcs-cpufreq
		 * This line of code release max limit when LCD is
		 * turned on.
		 */
#ifdef CONFIG_ARM_EXYNOS_IKS_CLUSTER
		if (pm_qos_request_active(&max_cpu_qos_blank))
			pm_qos_remove_request(&max_cpu_qos_blank);
#endif

		dbs_tuners_ins.up_threshold_l = MICRO_FREQUENCY_UP_THRESHOLD_L;
		lcd_is_on = true;
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block fb_block = {
	.notifier_call = fb_state_change,
};
#endif

static inline u64 get_cpu_idle_time_jiffy(unsigned int cpu, u64 *wall)
{
	u64 idle_time;
	u64 cur_wall_time;
	u64 busy_time;

	cur_wall_time = jiffies64_to_cputime64(get_jiffies_64());

	busy_time  = kcpustat_cpu(cpu).cpustat[CPUTIME_USER];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SYSTEM];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_IRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SOFTIRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_STEAL];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_NICE];

	idle_time = cur_wall_time - busy_time;
	if (wall)
		*wall = jiffies_to_usecs(cur_wall_time);

	return jiffies_to_usecs(idle_time);
}

static inline cputime64_t get_cpu_idle_time(unsigned int cpu, cputime64_t *wall)
{
	u64 idle_time = get_cpu_idle_time_us(cpu, NULL);

	if (idle_time == -1ULL)
		return get_cpu_idle_time_jiffy(cpu, wall);
	else
		idle_time += get_cpu_iowait_time_us(cpu, wall);

	return idle_time;
}

static inline cputime64_t get_cpu_iowait_time(unsigned int cpu, cputime64_t *wall)
{
	u64 iowait_time = get_cpu_iowait_time_us(cpu, wall);

	if (iowait_time == -1ULL)
		return 0;

	return iowait_time;
}

/*
 * Find right freq to be set now with powersave_bias on.
 * Returns the freq_hi to be used right now and will set freq_hi_jiffies,
 * freq_lo, and freq_lo_jiffies in percpu area for averaging freqs.
 */
static unsigned int powersave_bias_target(struct cpufreq_policy *policy,
					  unsigned int freq_next,
					  unsigned int relation)
{
	unsigned int freq_req, freq_reduc, freq_avg;
	unsigned int freq_hi, freq_lo;
	unsigned int index = 0;
	unsigned int jiffies_total, jiffies_hi, jiffies_lo;
	struct cpu_dbs_info_s *dbs_info = &per_cpu(od_cpu_dbs_info,
						   policy->cpu);

	if (!dbs_info->freq_table) {
		dbs_info->freq_lo = 0;
		dbs_info->freq_lo_jiffies = 0;
		return freq_next;
	}

	cpufreq_frequency_table_target(policy, dbs_info->freq_table, freq_next,
			relation, &index);
	freq_req = dbs_info->freq_table[index].frequency;
	freq_reduc = freq_req * dbs_tuners_ins.powersave_bias / 1000;
	freq_avg = freq_req - freq_reduc;

	/* Find freq bounds for freq_avg in freq_table */
	index = 0;
	cpufreq_frequency_table_target(policy, dbs_info->freq_table, freq_avg,
			CPUFREQ_RELATION_H, &index);
	freq_lo = dbs_info->freq_table[index].frequency;
	index = 0;
	cpufreq_frequency_table_target(policy, dbs_info->freq_table, freq_avg,
			CPUFREQ_RELATION_L, &index);
	freq_hi = dbs_info->freq_table[index].frequency;

	/* Find out how long we have to be in hi and lo freqs */
	if (freq_hi == freq_lo) {
		dbs_info->freq_lo = 0;
		dbs_info->freq_lo_jiffies = 0;
		return freq_lo;
	}
	jiffies_total = usecs_to_jiffies(dbs_tuners_ins.sampling_rate);
	jiffies_hi = (freq_avg - freq_lo) * jiffies_total;
	jiffies_hi += ((freq_hi - freq_lo) / 2);
	jiffies_hi /= (freq_hi - freq_lo);
	jiffies_lo = jiffies_total - jiffies_hi;
	dbs_info->freq_lo = freq_lo;
	dbs_info->freq_lo_jiffies = jiffies_lo;
	dbs_info->freq_hi_jiffies = jiffies_hi;
	return freq_hi;
}

static void ondemand_powersave_bias_init_cpu(int cpu)
{
	struct cpu_dbs_info_s *dbs_info = &per_cpu(od_cpu_dbs_info, cpu);
	dbs_info->freq_table = cpufreq_frequency_get_table(cpu);
	dbs_info->freq_lo = 0;
}

static void ondemand_powersave_bias_init(void)
{
	int i;
	for_each_online_cpu(i) {
		ondemand_powersave_bias_init_cpu(i);
	}
}

/************************** sysfs interface ************************/

static ssize_t show_sampling_rate_min(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", min_sampling_rate);
}

define_one_global_ro(sampling_rate_min);

/* cpufreq_ondemand Governor Tunables */
#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)              \
{									\
	return sprintf(buf, "%u\n", dbs_tuners_ins.object);		\
}
show_one(sampling_rate, sampling_rate);
show_one(io_is_busy, io_is_busy);
show_one(up_threshold, up_threshold);
show_one(sampling_down_factor, sampling_down_factor);
show_one(ignore_nice_load, ignore_nice);
show_one(powersave_bias, powersave_bias);
show_one(up_threshold_l, up_threshold_l);
show_one(up_threshold_h, up_threshold_h);
show_one(up_step_level_b, up_step_level_b);
show_one(up_step_level_l, up_step_level_l);
show_one(down_step_level, down_step_level);
show_one(high_freq_zone, high_freq_zone);
show_one(conservative_step, conservative_step);
show_one(up_conservative_mode, up_conservative_mode);
show_one(max_freq_blank, max_freq_blank);

/**
 * update_sampling_rate - update sampling rate effective immediately if needed.
 * @new_rate: new sampling rate
 *
 * If new rate is smaller than the old, simply updaing
 * dbs_tuners_int.sampling_rate might not be appropriate. For example,
 * if the original sampling_rate was 1 second and the requested new sampling
 * rate is 10 ms because the user needs immediate reaction from ondemand
 * governor, but not sure if higher frequency will be required or not,
 * then, the governor may change the sampling rate too late; up to 1 second
 * later. Thus, if we are reducing the sampling rate, we need to make the
 * new value effective immediately.
 */
static void update_sampling_rate(unsigned int new_rate)
{
	int cpu;

	dbs_tuners_ins.sampling_rate = new_rate
				     = max(new_rate, min_sampling_rate);

	for_each_online_cpu(cpu) {
		struct cpufreq_policy *policy;
		struct cpu_dbs_info_s *dbs_info;
		unsigned long next_sampling, appointed_at;

		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		dbs_info = &per_cpu(od_cpu_dbs_info, policy->cpu);
		cpufreq_cpu_put(policy);

		mutex_lock(&dbs_info->timer_mutex);

		if (!delayed_work_pending(&dbs_info->work)) {
			mutex_unlock(&dbs_info->timer_mutex);
			continue;
		}

		next_sampling  = jiffies + usecs_to_jiffies(new_rate);
		appointed_at = dbs_info->work.timer.expires;


		if (time_before(next_sampling, appointed_at)) {

			mutex_unlock(&dbs_info->timer_mutex);
			cancel_delayed_work_sync(&dbs_info->work);
			mutex_lock(&dbs_info->timer_mutex);

			schedule_delayed_work_on(dbs_info->cpu, &dbs_info->work,
						 usecs_to_jiffies(new_rate));

		}
		mutex_unlock(&dbs_info->timer_mutex);
	}
}

static ssize_t store_sampling_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	update_sampling_rate(input);
	return count;
}

static ssize_t store_io_is_busy(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	dbs_tuners_ins.io_is_busy = !!input;
	return count;
}

static ssize_t store_up_threshold(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
			input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	dbs_tuners_ins.up_threshold = input;
	return count;
}

static ssize_t store_up_threshold_l(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
			input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	dbs_tuners_ins.up_threshold_l = input;
	return count;
}

static ssize_t store_up_threshold_h(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
			input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	dbs_tuners_ins.up_threshold_h = input;
	return count;
}

static ssize_t store_up_step_level_b(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_STEP_LEVEL ||
			input < MIN_FREQUENCY_UP_STEP_LEVEL) {
		return -EINVAL;
	}
	dbs_tuners_ins.up_step_level_b = input;
	return count;
}

static ssize_t store_up_step_level_l(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_STEP_LEVEL ||
			input < MIN_FREQUENCY_UP_STEP_LEVEL) {
		return -EINVAL;
	}
	dbs_tuners_ins.up_step_level_l = input;
	return count;
}

static ssize_t store_down_step_level(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_STEP_LEVEL ||
			input < MIN_FREQUENCY_UP_STEP_LEVEL) {
		return -EINVAL;
	}
	dbs_tuners_ins.down_step_level = input;
	return count;
}

static ssize_t store_high_freq_zone(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_STEP_LEVEL ||
			input < dbs_tuners_ins.up_step_level_b) {
		return -EINVAL;
	}
	dbs_tuners_ins.high_freq_zone = input;
	return count;
}

static ssize_t store_conservative_step(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > 400 * 1000 * 1000 ||
			input < 100 * 1000 * 1000) {
		return -EINVAL;
	}
	dbs_tuners_ins.conservative_step = input;
	return count;
}

static ssize_t store_up_conservative_mode(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	dbs_tuners_ins.up_conservative_mode = input ? true : false;
	return count;
}

static ssize_t store_sampling_down_factor(struct kobject *a,
			struct attribute *b, const char *buf, size_t count)
{
	unsigned int input, j;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_SAMPLING_DOWN_FACTOR || input < 1)
		return -EINVAL;
	dbs_tuners_ins.sampling_down_factor = input;

	/* Reset down sampling multiplier in case it was active */
	for_each_online_cpu(j) {
		struct cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(od_cpu_dbs_info, j);
		dbs_info->rate_mult = 1;
	}
	return count;
}

static ssize_t store_max_freq_blank(struct kobject *a, struct attribute *b,
					const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_STEP_LEVEL ||
			input < MIN_FREQUENCY_UP_STEP_LEVEL) {
		return -EINVAL;
	}
	dbs_tuners_ins.max_freq_blank = input;
	return count;
}

static ssize_t store_ignore_nice_load(struct kobject *a, struct attribute *b,
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

	if (input == dbs_tuners_ins.ignore_nice) { /* nothing to do */
		return count;
	}
	dbs_tuners_ins.ignore_nice = input;

	/* we need to re-evaluate prev_cpu_idle */
	for_each_online_cpu(j) {
		struct cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(od_cpu_dbs_info, j);
		dbs_info->prev_cpu_idle = get_cpu_idle_time(j,
						&dbs_info->prev_cpu_wall);
		if (dbs_tuners_ins.ignore_nice)
			dbs_info->prev_cpu_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE];

	}
	return count;
}

static ssize_t store_powersave_bias(struct kobject *a, struct attribute *b,
				    const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	if (input > 1000)
		input = 1000;

	dbs_tuners_ins.powersave_bias = input;
	ondemand_powersave_bias_init();
	return count;
}

define_one_global_rw(sampling_rate);
define_one_global_rw(io_is_busy);
define_one_global_rw(up_threshold);
define_one_global_rw(sampling_down_factor);
define_one_global_rw(ignore_nice_load);
define_one_global_rw(powersave_bias);
define_one_global_rw(up_threshold_l);
define_one_global_rw(up_threshold_h);
define_one_global_rw(up_step_level_b);
define_one_global_rw(up_step_level_l);
define_one_global_rw(down_step_level);
define_one_global_rw(high_freq_zone);
define_one_global_rw(conservative_step);
define_one_global_rw(up_conservative_mode);
define_one_global_rw(max_freq_blank);

static int cpu_util[4];

static ssize_t show_cpu_utilization(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	return sprintf(buf, "%d %d %d %d\n", cpu_util[0], cpu_util[1],
				cpu_util[2], cpu_util[3]);
}

define_one_global_ro(cpu_utilization);

static struct attribute *dbs_attributes[] = {
	&sampling_rate_min.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&sampling_down_factor.attr,
	&ignore_nice_load.attr,
	&powersave_bias.attr,
	&io_is_busy.attr,
	&cpu_utilization.attr,
	&up_threshold_l.attr,
	&up_threshold_h.attr,
	&up_step_level_b.attr,
	&up_step_level_l.attr,
	&down_step_level.attr,
	&high_freq_zone.attr,
	&up_conservative_mode.attr,
	&conservative_step.attr,
	&max_freq_blank.attr,
	NULL
};

static struct attribute_group dbs_attr_group = {
	.attrs = dbs_attributes,
	.name = "ondemand",
};

/************************** sysfs end ************************/

static void dbs_freq_increase(struct cpufreq_policy *p, unsigned int freq)
{
	bool hotplug_in = false;

	if (dbs_tuners_ins.powersave_bias)
		freq = powersave_bias_target(p, freq, CPUFREQ_RELATION_H);

	if (!lcd_is_on && freq > dbs_tuners_ins.max_freq_blank)
		freq = dbs_tuners_ins.max_freq_blank;

#ifdef CONFIG_EXYNOS5_DYNAMIC_CPU_HOTPLUG
	/*
	 * If boost level is sustaning over than BOOST_LV_CNT, replace frequency
	 * with HOTPLUG_TRANS_H level and try cpu hotplug in after changing frequency
	 */
	if ((consecutive_boost_level > BOOST_LV_CNT) &&
		(freq > HOTPLUG_TRANS_H)) {
		freq = HOTPLUG_TRANS_H;
		hotplug_in = true;
	}
#endif
	__cpufreq_driver_target(p, freq, dbs_tuners_ins.powersave_bias ?
			CPUFREQ_RELATION_L : CPUFREQ_RELATION_H);

#ifdef CONFIG_EXYNOS5_DYNAMIC_CPU_HOTPLUG
	/*
	 * Hotplug in case :
	 * - Boost level is continuing over than BOOST_LV_CNT
	 * - Prior to cpu hotplug, frequency should be changed below 1.6Ghz
	 */
	if (!cpumask_empty(&out_cpus) && hotplug_in) {
		mutex_lock(&hotplug_mutex);
		hotplug_out = false;
		__do_hotplug();
		mutex_unlock(&hotplug_mutex);
	}
#endif
}

static void dbs_check_cpu(struct cpu_dbs_info_s *this_dbs_info)
{
	unsigned int max_load_freq;
	unsigned int tmp;

	struct cpufreq_policy *policy;
	unsigned int j;
	unsigned int cpu_util_sum = 0;

	this_dbs_info->freq_lo = 0;
	policy = this_dbs_info->cur_policy;

	/*
	 * Every sampling_rate, we check, if current idle time is less
	 * than 20% (default), then we try to increase frequency
	 * Every sampling_rate, we look for a the lowest
	 * frequency which can sustain the load while keeping idle time over
	 * 30%. If such a frequency exist, we try to decrease to this frequency.
	 *
	 * Any frequency increase takes it to the maximum frequency.
	 * Frequency reduction happens at minimum steps of
	 * 5% (default) of current frequency
	 */

	/* Get Absolute Load - in terms of freq */
	max_load_freq = 0;

	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_info_s *j_dbs_info;
		cputime64_t cur_wall_time, cur_idle_time, cur_iowait_time;
		unsigned int idle_time, wall_time, iowait_time;
		unsigned int load, load_freq;
		int freq_avg;

		j_dbs_info = &per_cpu(od_cpu_dbs_info, j);

		cur_idle_time = get_cpu_idle_time(j, &cur_wall_time);
		cur_iowait_time = get_cpu_iowait_time(j, &cur_wall_time);

		wall_time = (unsigned int)
			(cur_wall_time - j_dbs_info->prev_cpu_wall);
		j_dbs_info->prev_cpu_wall = cur_wall_time;

		idle_time = (unsigned int)
			(cur_idle_time - j_dbs_info->prev_cpu_idle);
		j_dbs_info->prev_cpu_idle = cur_idle_time;

		iowait_time = (unsigned int)
			(cur_iowait_time - j_dbs_info->prev_cpu_iowait);
		j_dbs_info->prev_cpu_iowait = cur_iowait_time;

		if (dbs_tuners_ins.ignore_nice) {
			u64 cur_nice;
			unsigned long cur_nice_jiffies;

			cur_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE] -
					 j_dbs_info->prev_cpu_nice;
			/*
			 * Assumption: nice time between sampling periods will
			 * be less than 2^32 jiffies for 32 bit sys
			 */
			cur_nice_jiffies = (unsigned long)
					cputime64_to_jiffies64(cur_nice);

			j_dbs_info->prev_cpu_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE];
			idle_time += jiffies_to_usecs(cur_nice_jiffies);
		}

		/*
		 * For the purpose of ondemand, waiting for disk IO is an
		 * indication that you're performance critical, and not that
		 * the system is actually idle. So subtract the iowait time
		 * from the cpu idle time.
		 */

		if (dbs_tuners_ins.io_is_busy && idle_time >= iowait_time)
			idle_time -= iowait_time;

		if (unlikely(!wall_time || wall_time < idle_time))
			continue;

		load = 100 * (wall_time - idle_time) / wall_time;
		cpu_util[j] = load;
		cpu_util_sum += load;

		freq_avg = __cpufreq_driver_getavg(policy, j);
		if (freq_avg <= 0)
			freq_avg = policy->cur;

		load_freq = load * freq_avg;
		if (load_freq > max_load_freq)
			max_load_freq = load_freq;
	}

#ifdef CONFIG_EXYNOS5_DYNAMIC_CPU_HOTPLUG
#ifdef ENABLE_HOTPLUG_OUT_H
	/* Hotplug out case : Frequency stay over maximum quad level */
	if ((policy->cur < HOTPLUG_TRANS_H) ||
			num_online_cpus() <= HOTPLUG_TRANS_H_CPUS)
		goto skip_hotplug_out_1;

	/*
	 * If next transition is descending from over HOTPLUG_TRANS_H
	 * do not try hotplug out
	 */
	if (max_load_freq <= dbs_tuners_ins.up_threshold * policy->cur)
		goto skip_hotplug_out_1;

	/*
	 * If policy->cur >= 1.6Ghz(HOTPLUG_TRANS_H) and next transition is
	 * ascending check cpu_util value for each online cpu.
	 * If cpu_util is less than 10%(HOTPLUG_OUT_LOAD) for 3(HOTPLUG_OUT_CNT_H)
	 * times sampling rate(100ms), plugged out on this cpu.
	 */
	for_each_online_cpu(j) {
		/* core 0 must not be hotplugged out */
		if (j == 0)
			continue;
		if (cpumask_weight(cpu_online_mask) - cpumask_weight(&to_be_out_cpus) <=
				HOTPLUG_TRANS_H_CPUS)
			break;

		if (cpu_util[j] < HOTPLUG_OUT_LOAD) {
			per_cpu(hotplug_out_cnt_h, j)++;

			if (per_cpu(hotplug_out_cnt_h, j) > HOTPLUG_OUT_CNT_H) {
				cpumask_set_cpu(j, &to_be_out_cpus);
				per_cpu(hotplug_out_cnt_h, j) = 0;
			}
		} else {
			/* Reset out trigger counter */
			per_cpu(hotplug_out_cnt_h, j) = 0;
		}
	}
	/*
	 * Hotplug Out:
	 * - Frequency is 1.6/1.7Ghz
	 * - Some cpu's utilization is less than 10%
	 */
	if (!cpumask_empty(&to_be_out_cpus)) {
		mutex_lock(&hotplug_mutex);
		hotplug_out = true;
		__do_hotplug();
		mutex_unlock(&hotplug_mutex);
	}

skip_hotplug_out_1:
	/*
	 * Increase consecutive_boost_level if policy->cur is higher than
	 * 1.6Ghz(HOTPLUG_TRANS_H) for consecutive period.
	 * If not, reset this variable.
	 */
	if (policy->cur > HOTPLUG_TRANS_H)
		consecutive_boost_level++;
	else
		consecutive_boost_level = 0;

#endif
#endif

	if (policy->cur < dbs_tuners_ins.up_step_level_l) {
		/*
		 * If current freq is under 600MHz, and load freq is bigger than
		 * up_threshold 60, increase freq by step level 600MHz.
		 */
		if (max_load_freq > dbs_tuners_ins.up_threshold_l * policy->cur) {
			dbs_freq_increase(policy, dbs_tuners_ins.up_step_level_l);
#ifdef CONFIG_EXYNOS5_DYNAMIC_CPU_HOTPLUG
			/*
			 * Hotplug In:
			 * - If frequency up from KFC 500Mhz(HOTPLUG_TRANS_L)
			 */

			if (!cpumask_empty(&out_cpus)) {
				mutex_lock(&hotplug_mutex);
				hotplug_out = false;
				__do_hotplug();
				mutex_unlock(&hotplug_mutex);
			}
#endif
			return;
		}
	} else if (policy->cur < dbs_tuners_ins.up_step_level_b ||
			(policy->cur >= dbs_tuners_ins.up_step_level_b &&
			policy->cur < dbs_tuners_ins.high_freq_zone)) {
		/*
		 * If current freq is same or over 600MHz, and load freq is bigger than
		 * up_threshold 95, increase freq as below conditions.
		 * Condition 1: current freq is under 1.2GHz, apply step level to 1.2GHz
		 * Condition 2: current freq is same or over 1.2GHz, increase to max freq.
		 */
		if (max_load_freq > dbs_tuners_ins.up_threshold * policy->cur) {
#ifdef CONFIG_EXYNOS5_DYNAMIC_CPU_HOTPLUG
			/*
			 * Hotplug In:
			 * - Sometimes user lock can make this situation
			 */
			if (!cpumask_empty(&out_cpus) && policy->cur < HOTPLUG_TRANS_H) {
				mutex_lock(&hotplug_mutex);
				hotplug_out = false;
				__do_hotplug();
				mutex_unlock(&hotplug_mutex);
			}
#endif
			dbs_freq_increase(policy, policy->cur < dbs_tuners_ins.up_step_level_b ?
					dbs_tuners_ins.up_step_level_b : policy->max);
			return;
		}
	} else {
		if (max_load_freq > dbs_tuners_ins.up_threshold_h * policy->cur) {
#ifdef CONFIG_EXYNOS5_DYNAMIC_CPU_HOTPLUG
			/*
			 * Hotplug In:
			 * - Sometimes user lock can make this situation
			 */
			if (!cpumask_empty(&out_cpus) && policy->cur < HOTPLUG_TRANS_H) {
				mutex_lock(&hotplug_mutex);
				hotplug_out = false;
				__do_hotplug();
				mutex_unlock(&hotplug_mutex);
			}
#endif
			/* If switching to max speed, apply sampling_down_factor */
			this_dbs_info->rate_mult =
				dbs_tuners_ins.sampling_down_factor;
			dbs_freq_increase(policy, dbs_tuners_ins.up_conservative_mode ?
				policy->cur + dbs_tuners_ins.conservative_step : policy->max);
			return;
	        }
	}

#ifdef CONFIG_EXYNOS5_DYNAMIC_CPU_HOTPLUG
	/*
	 * Hotplug Out:
	 * - Frequency stay at lowest level
	 */
	if ((policy->cur > HOTPLUG_TRANS_L) ||
			num_online_cpus() <= HOTPLUG_TRANS_L_CPUS ||
			lcd_is_on)
		goto skip_hotplug_out_2;

	if (cpu_util_sum <
		dbs_tuners_ins.up_threshold_l - dbs_tuners_ins.down_differ_l) {

		hotplug_out_cnt_l++;

		if (hotplug_out_cnt_l > HOTPLUG_OUT_CNT_L) {
			cpumask_setall(&to_be_out_cpus);
			cpumask_clear_cpu(0, &to_be_out_cpus);
			hotplug_out_cnt_l = 0;
		}
	} else {
		/* Reset out trigger counter */
		hotplug_out_cnt_l = 0;
	}

	if (!cpumask_empty(&to_be_out_cpus)) {
		mutex_lock(&hotplug_mutex);
		hotplug_out = true;
		__do_hotplug();
		mutex_unlock(&hotplug_mutex);
	}

skip_hotplug_out_2:
#endif

	/* Check for frequency decrease */
	/* if we cannot reduce the frequency anymore, break out early */
	if (policy->cur == policy->min)
		return;

	/*
	 * The optimal frequency is the frequency that is the lowest that
	 * can support the current CPU usage without triggering the up
	 * policy. To be safe, we focus 10 points under the threshold.
	 */
	if (policy->cur > dbs_tuners_ins.down_step_level) {
		/*
		 * If current freq is over 800MHz, and load freq is smaller than
		 * 92(by 95-3), decrease freq as below condition.
		 * Condition: next freq is under 800MHz decrease to 800MHz
		 */
		tmp = policy->cur > dbs_tuners_ins.high_freq_zone ?
				dbs_tuners_ins.up_threshold_h : dbs_tuners_ins.up_threshold;

		if (max_load_freq < (tmp - dbs_tuners_ins.down_differential) * policy->cur) {
			unsigned int freq_next;
			freq_next = max_load_freq /
					(tmp - dbs_tuners_ins.down_differential);

			/* No longer fully busy, reset rate_mult */
			this_dbs_info->rate_mult = 1;

			if (freq_next < dbs_tuners_ins.down_step_level)
				freq_next = dbs_tuners_ins.down_step_level;

			if (!dbs_tuners_ins.powersave_bias) {
				__cpufreq_driver_target(policy, freq_next,
						CPUFREQ_RELATION_L);
			} else {
				int freq = powersave_bias_target(policy, freq_next,
						CPUFREQ_RELATION_L);
				__cpufreq_driver_target(policy, freq,
					CPUFREQ_RELATION_L);
			}
#ifdef CONFIG_EXYNOS5_DYNAMIC_CPU_HOTPLUG
			/*
			 * Hotplug In case : Decrease frequency to over down step level
			 * If 1.8Ghz -> 1.7Ghz transition, need to keep current hotplug
			 * state. Do not perform below routine.
			 * If next level is descending below 1.6Ghz(HOTPLUG_TRANS_H), try
			 * hotplug in on all plugged out cpus.
			 */
			if (freq_next <= HOTPLUG_TRANS_H && !cpumask_empty(&out_cpus)) {
				mutex_lock(&hotplug_mutex);
				hotplug_out = false;
				__do_hotplug();
				mutex_unlock(&hotplug_mutex);
			}
#endif
		}
	} else {
		/*
		 * If current freq is same or under 800MHz, and load freq is smaller than
		 * 40(by 60-20), decrease freq.
		 */
		if (max_load_freq <
		    (dbs_tuners_ins.up_threshold_l - dbs_tuners_ins.down_differ_l) *
		     policy->cur) {
			unsigned int freq_next;
			freq_next = max_load_freq /
					(dbs_tuners_ins.up_threshold_l -
					 dbs_tuners_ins.down_differ_l);

                        /* No longer fully busy, reset rate_mult */
			this_dbs_info->rate_mult = 1;

			if (freq_next < policy->min)
				freq_next = policy->min;

#ifdef CONFIG_EXYNOS5_DYNAMIC_CPU_HOTPLUG
			/*
			 * Hotplug In:
			 * - Descending frequency from down_step_level try hotplug in
			 * first to reduce pluging out latency and then down frequency.
			 */
			if (!cpumask_empty(&out_cpus)) {
				mutex_lock(&hotplug_mutex);
				hotplug_out = false;
				__do_hotplug();
				mutex_unlock(&hotplug_mutex);
			}
#endif

			if (!dbs_tuners_ins.powersave_bias) {
				__cpufreq_driver_target(policy, freq_next,
						CPUFREQ_RELATION_L);
			} else {
				int freq = powersave_bias_target(policy, freq_next,
						CPUFREQ_RELATION_L);
				__cpufreq_driver_target(policy, freq,
					CPUFREQ_RELATION_L);
			}
		}
	}
}

static void do_dbs_timer(struct work_struct *work)
{
	struct cpu_dbs_info_s *dbs_info =
		container_of(work, struct cpu_dbs_info_s, work.work);
	unsigned int cpu = dbs_info->cpu;
	int sample_type = dbs_info->sample_type;

	int delay;

	mutex_lock(&dbs_info->timer_mutex);

	/* Common NORMAL_SAMPLE setup */
	dbs_info->sample_type = DBS_NORMAL_SAMPLE;
	if (!dbs_tuners_ins.powersave_bias ||
	    sample_type == DBS_NORMAL_SAMPLE) {
		dbs_check_cpu(dbs_info);
		if (dbs_info->freq_lo) {
			/* Setup timer for SUB_SAMPLE */
			dbs_info->sample_type = DBS_SUB_SAMPLE;
			delay = dbs_info->freq_hi_jiffies;
		} else {
			/* We want all CPUs to do sampling nearly on
			 * same jiffy
			 */
			struct cpufreq_policy *policy = dbs_info->cur_policy;
			dbs_info->rate_mult = 1;

			delay = usecs_to_jiffies(dbs_tuners_ins.sampling_rate
				/ dbs_info->rate_mult);

			if (num_online_cpus() > 1)
				delay -= jiffies % delay;
		}
	} else {
		__cpufreq_driver_target(dbs_info->cur_policy,
			dbs_info->freq_lo, CPUFREQ_RELATION_H);
		delay = dbs_info->freq_lo_jiffies;
	}
	schedule_delayed_work_on(cpu, &dbs_info->work, delay);
	mutex_unlock(&dbs_info->timer_mutex);
}

static inline void dbs_timer_init(struct cpu_dbs_info_s *dbs_info)
{
	/* We want all CPUs to do sampling nearly on same jiffy */
	int delay = usecs_to_jiffies(dbs_tuners_ins.sampling_rate);

	if (num_online_cpus() > 1)
		delay -= jiffies % delay;

	dbs_info->sample_type = DBS_NORMAL_SAMPLE;
	INIT_DELAYED_WORK(&dbs_info->work, do_dbs_timer);
	schedule_delayed_work_on(dbs_info->cpu, &dbs_info->work, delay);
}

static inline void dbs_timer_exit(struct cpu_dbs_info_s *dbs_info)
{
	cancel_delayed_work_sync(&dbs_info->work);
}

/*
 * Not all CPUs want IO time to be accounted as busy; this dependson how
 * efficient idling at a higher frequency/voltage is.
 * Pavel Machek says this is not so for various generations of AMD and old
 * Intel systems.
 * Mike Chan (androidlcom) calis this is also not true for ARM.
 * Because of this, whitelist specific known (series) of CPUs by default, and
 * leave all others up to the user.
 */
static int should_io_be_busy(void)
{
#if defined(CONFIG_X86)
	/*
	 * For Intel, Core 2 (model 15) andl later have an efficient idle.
	 */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
	    boot_cpu_data.x86 == 6 &&
	    boot_cpu_data.x86_model >= 15)
		return 1;
#endif
	return 0;
}

static int cpufreq_governor_dbs(struct cpufreq_policy *policy,
				   unsigned int event)
{
	unsigned int cpu = policy->cpu;
	struct cpu_dbs_info_s *this_dbs_info;
	unsigned int j;
	int rc;

	this_dbs_info = &per_cpu(od_cpu_dbs_info, cpu);

	switch (event) {
	case CPUFREQ_GOV_START:
		if ((!cpu_online(cpu)) || (!policy->cur))
			return -EINVAL;

		mutex_lock(&dbs_mutex);

		dbs_enable++;
		for_each_cpu(j, policy->cpus) {
			struct cpu_dbs_info_s *j_dbs_info;
			j_dbs_info = &per_cpu(od_cpu_dbs_info, j);
			j_dbs_info->cur_policy = policy;

			j_dbs_info->prev_cpu_idle = get_cpu_idle_time(j,
						&j_dbs_info->prev_cpu_wall);
			if (dbs_tuners_ins.ignore_nice)
				j_dbs_info->prev_cpu_nice =
						kcpustat_cpu(j).cpustat[CPUTIME_NICE];
		}
		this_dbs_info->cpu = cpu;
		this_dbs_info->rate_mult = 1;
		ondemand_powersave_bias_init_cpu(cpu);
		/*
		 * Start the timerschedule work, when this governor
		 * is used for first time
		 */
		if (dbs_enable == 1) {
			unsigned int latency;

			rc = sysfs_create_group(cpufreq_global_kobject,
						&dbs_attr_group);
			if (rc) {
				mutex_unlock(&dbs_mutex);
				return rc;
			}

			/* policy latency is in nS. Convert it to uS first */
			latency = policy->cpuinfo.transition_latency / 1000;
			if (latency == 0)
				latency = 1;
			/* Bring kernel and HW constraints together */
			min_sampling_rate = max(min_sampling_rate,
					MIN_LATENCY_MULTIPLIER * latency);
			dbs_tuners_ins.sampling_rate =
				max(min_sampling_rate,
				    latency * LATENCY_MULTIPLIER);
			dbs_tuners_ins.io_is_busy = should_io_be_busy();
		}
		mutex_unlock(&dbs_mutex);

		mutex_init(&this_dbs_info->timer_mutex);
		dbs_timer_init(this_dbs_info);
#ifdef CONFIG_EXYNOS5_DYNAMIC_CPU_HOTPLUG
		cpumask_clear(&out_cpus);
		cpumask_clear(&to_be_out_cpus);
		mutex_init(&hotplug_mutex);
		INIT_WORK(&hotplug_work, do_hotplug);
		consecutive_boost_level = 0;
#endif
		break;

	case CPUFREQ_GOV_STOP:
		dbs_timer_exit(this_dbs_info);

		mutex_lock(&dbs_mutex);
		mutex_destroy(&this_dbs_info->timer_mutex);
		dbs_enable--;
		mutex_unlock(&dbs_mutex);
		if (!dbs_enable)
			sysfs_remove_group(cpufreq_global_kobject,
					   &dbs_attr_group);

#ifdef CONFIG_EXYNOS5_DYNAMIC_CPU_HOTPLUG
		cancel_work_sync(&hotplug_work);
		mutex_destroy(&hotplug_mutex);
		for_each_cpu(j, &out_cpus)
			cpu_up(j);

		cpumask_clear(&out_cpus);
		cpumask_clear(&to_be_out_cpus);
		consecutive_boost_level = 0;
#endif
		break;

	case CPUFREQ_GOV_LIMITS:
		mutex_lock(&this_dbs_info->timer_mutex);
		if (policy->max < this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(this_dbs_info->cur_policy,
				policy->max, CPUFREQ_RELATION_H);
		else if (policy->min > this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(this_dbs_info->cur_policy,
				policy->min, CPUFREQ_RELATION_L);
		mutex_unlock(&this_dbs_info->timer_mutex);
		break;
	}
	return 0;
}

extern bool get_asv_is_bin2(void);
static int __init cpufreq_gov_dbs_init(void)
{
	u64 idle_time;
	int cpu = get_cpu();

	idle_time = get_cpu_idle_time_us(cpu, NULL);
	put_cpu();
	if (idle_time != -1ULL) {
		/* Idle micro accounting is supported. Use finer thresholds */
		dbs_tuners_ins.up_threshold = MICRO_FREQUENCY_UP_THRESHOLD;
		dbs_tuners_ins.up_threshold_l = MICRO_FREQUENCY_UP_THRESHOLD_L;
		dbs_tuners_ins.up_threshold_h = MICRO_FREQUENCY_UP_THRESHOLD_H;
		dbs_tuners_ins.up_step_level_b = MICRO_FREQUENCY_UP_STEP_LEVEL_B;
		dbs_tuners_ins.up_step_level_l = MICRO_FREQUENCY_UP_STEP_LEVEL_L;
		dbs_tuners_ins.down_step_level = MICRO_FREQUENCY_DOWN_STEP_LEVEL;
		dbs_tuners_ins.down_differential =
					MICRO_FREQUENCY_DOWN_DIFFERENTIAL;
		dbs_tuners_ins.down_differ_l =
					MICRO_FREQUENCY_DOWN_DIFFER_L;
		/*
		 * In nohz/micro accounting case we set the minimum frequency
		 * not depending on HZ, but fixed (very low). The deferred
		 * timer might skip some samples if idle/sleeping as needed.
		*/
		min_sampling_rate = MICRO_FREQUENCY_MIN_SAMPLE_RATE;
	} else {
		/* For correct statistics, we need 10 ticks for each measure */
		min_sampling_rate =
			MIN_SAMPLING_RATE_RATIO * jiffies_to_usecs(10);
	}

    if(get_asv_is_bin2())
        dbs_tuners_ins.up_step_level_l = BIN2_FREQUENCY_UP_STEP_LEVEL_L;

#ifdef CONFIG_EXYNOS5_DYNAMIC_CPU_HOTPLUG
	fb_register_client(&fb_block);

	lcd_is_on = true;
#endif

	return cpufreq_register_governor(&cpufreq_gov_ondemand);
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_ondemand);
}


MODULE_AUTHOR("Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>");
MODULE_AUTHOR("Alexey Starikovskiy <alexey.y.starikovskiy@intel.com>");
MODULE_DESCRIPTION("'cpufreq_ondemand' - A dynamic cpufreq governor for "
	"Low Latency Frequency Transition capable processors");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_ONDEMAND
fs_initcall(cpufreq_gov_dbs_init);
#else
module_init(cpufreq_gov_dbs_init);
#endif
module_exit(cpufreq_gov_dbs_exit);
