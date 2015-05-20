/*
 * Copyright (c) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/cpuquiet.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/cpufreq.h>
#include <linux/pm_qos.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/tick.h>
#include <asm/cputime.h>

#define CPUNAMELEN 8

typedef enum {
	CPU_SPEED_BALANCED,
	CPU_SPEED_BIASED,
	CPU_SPEED_SKEWED,
} CPU_SPEED_BALANCE;

typedef enum {
	IDLE,
	DOWN,
	UP,
} BALANCED_STATE;

struct idle_info {
	u64 idle_last;
	u64 last_timestamp;
	u64 idle_current;
	u64 timestamp;
};

static DEFINE_PER_CPU(struct idle_info, idleinfo);
static DEFINE_PER_CPU(unsigned int, cpu_load);

static struct timer_list load_timer;
static bool load_timer_active;

/* configurable parameters */
static unsigned int  balance_level = 60;
static unsigned int  idle_bottom_freq;
static unsigned int  idle_top_freq;
static unsigned long up_delay;
static unsigned long down_delay;
static unsigned long last_change_time;
static unsigned int  load_sample_rate = 20; /* msec */
static struct workqueue_struct *balanced_wq;
static struct delayed_work balanced_work;
static BALANCED_STATE balanced_state;
static struct kobject *balanced_kobject;

static void calculate_load_timer(unsigned long data)
{
	int i;
	u64 idle_time, elapsed_time;

	if (!load_timer_active)
		return;

	for_each_online_cpu(i) {
		struct idle_info *iinfo = &per_cpu(idleinfo, i);
		unsigned int *load = &per_cpu(cpu_load, i);

		iinfo->idle_last = iinfo->idle_current;
		iinfo->last_timestamp = iinfo->timestamp;
		iinfo->idle_current =
			get_cpu_idle_time_us(i, &iinfo->timestamp);
		elapsed_time = iinfo->timestamp - iinfo->last_timestamp;

		idle_time = iinfo->idle_current - iinfo->idle_last;
		idle_time *= 100;
		do_div(idle_time, elapsed_time);
		*load = 100 - idle_time;
	}
	mod_timer(&load_timer, jiffies + msecs_to_jiffies(load_sample_rate));
}

static void start_load_timer(void)
{
	int i;

	if (load_timer_active)
		return;

	load_timer_active = true;

	for_each_online_cpu(i) {
		struct idle_info *iinfo = &per_cpu(idleinfo, i);

		iinfo->idle_current =
			get_cpu_idle_time_us(i, &iinfo->timestamp);
	}
	mod_timer(&load_timer, jiffies + msecs_to_jiffies(100));
}

static void stop_load_timer(void)
{
	if (!load_timer_active)
		return;

	load_timer_active = false;
	del_timer(&load_timer);
}

static unsigned int get_slowest_cpu_n(void)
{
	unsigned int cpu = nr_cpu_ids;
	unsigned long minload = ULONG_MAX;
	int i;

	for_each_online_cpu(i) {
		unsigned int *load = &per_cpu(cpu_load, i);

		if ((i > 0) && (minload > *load)) {
			cpu = i;
			minload = *load;
		}
	}

	return cpu;
}

static unsigned int cpu_highest_speed(void)
{
	unsigned int maxload = 0;
	int i;

	for_each_online_cpu(i) {
		unsigned int *load = &per_cpu(cpu_load, i);

		maxload = max(maxload, *load);
	}

	return maxload;
}

static unsigned int count_slow_cpus(unsigned int limit)
{
	unsigned int cnt = 0;
	int i;

	for_each_online_cpu(i) {
		unsigned int *load = &per_cpu(cpu_load, i);

		if (*load <= limit)
			cnt++;
	}

	return cnt;
}

#define NR_FSHIFT	2

static unsigned int rt_profile_sel;
static unsigned int core_bias; //Dummy variable exposed to userspace

static unsigned int rt_profile_default[] = {
/*      1,  2,  3,  4 - on-line cpus target */
	5,  9, 10, UINT_MAX
};

static unsigned int rt_profile_1[] = {
/*      1,  2,  3,  4 - on-line cpus target */
	8,  9, 10, UINT_MAX
};

static unsigned int rt_profile_2[] = {
/*      1,  2,  3,  4 - on-line cpus target */
	5,  13, 14, UINT_MAX
};

static unsigned int rt_profile_disable[] = {
/*      1,  2,  3,  4 - on-line cpus target */
	0,  0, 0, UINT_MAX
};

static unsigned int *rt_profiles[] = {
	rt_profile_default,
	rt_profile_1,
	rt_profile_2,
	rt_profile_disable
};

static unsigned int nr_run_hysteresis = 2;	/* 0.5 thread */
static unsigned int nr_run_last;

struct runnables_avg_sample {
	u64 previous_integral;
	unsigned int avg;
	bool integral_sampled;
	u64 prev_timestamp;
};

static DEFINE_PER_CPU(struct runnables_avg_sample, avg_nr_sample);

static unsigned int get_avg_nr_runnables(void)
{
	unsigned int i, sum = 0;
	struct runnables_avg_sample *sample;
	u64 integral, old_integral, delta_integral, delta_time, cur_time;

	for_each_online_cpu(i) {
		sample = &per_cpu(avg_nr_sample, i);
		integral = nr_running_integral(i);
		old_integral = sample->previous_integral;
		sample->previous_integral = integral;
		cur_time = ktime_to_ns(ktime_get());
		delta_time = cur_time - sample->prev_timestamp;
		sample->prev_timestamp = cur_time;

		if (!sample->integral_sampled) {
			sample->integral_sampled = true;
			/* First sample to initialize prev_integral, skip
			 * avg calculation
			 */
			continue;
		}

		if (integral < old_integral) {
			/* Overflow */
			delta_integral = (ULLONG_MAX - old_integral) + integral;
		} else {
			delta_integral = integral - old_integral;
		}

		/* Calculate average for the previous sample window */
		do_div(delta_integral, delta_time);
		sample->avg = delta_integral;
		sum += sample->avg;
	}

	return sum;
}

static CPU_SPEED_BALANCE balanced_speed_balance(void)
{
	unsigned long highest_speed = cpu_highest_speed();
	unsigned long balanced_speed = highest_speed * balance_level / 100;
	unsigned long skewed_speed = balanced_speed / 2;
	unsigned int nr_cpus = num_online_cpus();
	unsigned int max_cpus = pm_qos_request(PM_QOS_MAX_ONLINE_CPUS) ? : 4;
	unsigned int avg_nr_run = get_avg_nr_runnables();
	unsigned int nr_run;
	unsigned int *current_profile = rt_profiles[rt_profile_sel];

	/* balanced: freq targets for all CPUs are above 50% of highest speed
	   biased: freq target for at least one CPU is below 50% threshold
	   skewed: freq targets for at least 2 CPUs are below 25% threshold */
	for (nr_run = 1; nr_run < ARRAY_SIZE(rt_profile_default); nr_run++) {
		unsigned int nr_threshold = current_profile[nr_run - 1];
		if (nr_run_last <= nr_run)
			nr_threshold += nr_run_hysteresis;
		if (avg_nr_run <= (nr_threshold << (FSHIFT - NR_FSHIFT)))
			break;
	}
	nr_run_last = nr_run;

	if (count_slow_cpus(skewed_speed) >= 2 || nr_cpus > max_cpus ||
		nr_run < nr_cpus)
		return CPU_SPEED_SKEWED;

	if (count_slow_cpus(balanced_speed) >= 1 || nr_cpus == max_cpus ||
		nr_run <= nr_cpus)
		return CPU_SPEED_BIASED;

	return CPU_SPEED_BALANCED;
}

static void balanced_work_func(struct work_struct *work)
{
	bool up = false;
	unsigned int cpu = nr_cpu_ids;
	unsigned long now = jiffies;

	CPU_SPEED_BALANCE balance;

	switch (balanced_state) {
	case IDLE:
		break;
	case DOWN:
		cpu = get_slowest_cpu_n();
		if (cpu < nr_cpu_ids) {
			up = false;
			queue_delayed_work(balanced_wq,
						 &balanced_work, up_delay);
		} else
			stop_load_timer();
		break;
	case UP:
		balance = balanced_speed_balance();
		switch (balance) {

		/* cpu speed is up and balanced - one more on-line */
		case CPU_SPEED_BALANCED:
			cpu = cpumask_next_zero(0, cpu_online_mask);
			if (cpu < nr_cpu_ids)
				up = true;
			break;
		/* cpu speed is up, but skewed - remove one core */
		case CPU_SPEED_SKEWED:
			cpu = get_slowest_cpu_n();
			if (cpu < nr_cpu_ids)
				up = false;
			break;
		/* cpu speed is up, but under-utilized - do nothing */
		case CPU_SPEED_BIASED:
		default:
			break;
		}
		queue_delayed_work(
			balanced_wq, &balanced_work, up_delay);
		break;
	default:
		pr_err("%s: invalid cpuquiet balanced governor state %d\n",
		       __func__, balanced_state);
	}

	if (!up && ((now - last_change_time) < down_delay))
		cpu = nr_cpu_ids;

	if (cpu < nr_cpu_ids) {
		last_change_time = now;
		if (up)
			cpuquiet_wake_cpu(cpu, false);
		else
			cpuquiet_quiesence_cpu(cpu, false);
	}
}

static int balanced_cpufreq_transition(struct notifier_block *nb,
	unsigned long state, void *data)
{
	struct cpufreq_freqs *freqs = data;
	unsigned long cpu_freq;

	if (state == CPUFREQ_POSTCHANGE || state == CPUFREQ_RESUMECHANGE) {
		cpu_freq = freqs->new;

		switch (balanced_state) {
		case IDLE:
			if (cpu_freq >= idle_top_freq) {
				balanced_state = UP;
				queue_delayed_work(
					balanced_wq, &balanced_work, up_delay);
				start_load_timer();
			} else if (cpu_freq <= idle_bottom_freq) {
				balanced_state = DOWN;
				queue_delayed_work(
					balanced_wq, &balanced_work,
					down_delay);
				start_load_timer();
			}
			break;
		case DOWN:
			if (cpu_freq >= idle_top_freq) {
				balanced_state = UP;
				queue_delayed_work(
					balanced_wq, &balanced_work, up_delay);
				start_load_timer();
			}
			break;
		case UP:
			if (cpu_freq <= idle_bottom_freq) {
				balanced_state = DOWN;
				queue_delayed_work(balanced_wq,
					&balanced_work, up_delay);
				start_load_timer();
			}
			break;
		default:
			pr_err("%s: invalid cpuquiet balanced governor "
				"state %d\n", __func__, balanced_state);
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block balanced_cpufreq_nb = {
	.notifier_call = balanced_cpufreq_transition,
};

static void delay_callback(struct cpuquiet_attribute *attr)
{
	unsigned long val;

	if (attr) {
		val = (*((unsigned long *)(attr->param)));
		(*((unsigned long *)(attr->param))) = msecs_to_jiffies(val);
	}
}

static void core_bias_callback (struct cpuquiet_attribute *attr)
{
	unsigned long val;
	if (attr) {
		val = (*((unsigned int*)(attr->param)));
		if (val < ARRAY_SIZE(rt_profiles)) {
			rt_profile_sel = val;
		}
		else {	//Revert the change due to invalid range
			core_bias = rt_profile_sel;
		}
	}
}

CPQ_BASIC_ATTRIBUTE(balance_level, 0644, uint);
CPQ_BASIC_ATTRIBUTE(idle_bottom_freq, 0644, uint);
CPQ_BASIC_ATTRIBUTE(idle_top_freq, 0644, uint);
CPQ_BASIC_ATTRIBUTE(load_sample_rate, 0644, uint);
CPQ_ATTRIBUTE(core_bias, 0644, uint, core_bias_callback);
CPQ_ATTRIBUTE(up_delay, 0644, ulong, delay_callback);
CPQ_ATTRIBUTE(down_delay, 0644, ulong, delay_callback);

static struct attribute *balanced_attributes[] = {
	&balance_level_attr.attr,
	&idle_bottom_freq_attr.attr,
	&idle_top_freq_attr.attr,
	&up_delay_attr.attr,
	&down_delay_attr.attr,
	&load_sample_rate_attr.attr,
	&core_bias_attr.attr,
	NULL,
};

static const struct sysfs_ops balanced_sysfs_ops = {
	.show = cpuquiet_auto_sysfs_show,
	.store = cpuquiet_auto_sysfs_store,
};

static struct kobj_type ktype_balanced = {
	.sysfs_ops = &balanced_sysfs_ops,
	.default_attrs = balanced_attributes,
};

static int balanced_sysfs(void)
{
	int err;

	balanced_kobject = kzalloc(sizeof(*balanced_kobject),
				GFP_KERNEL);

	if (!balanced_kobject)
		return -ENOMEM;

	err = cpuquiet_kobject_init(balanced_kobject, &ktype_balanced,
				"balanced");

	if (err)
		kfree(balanced_kobject);

	return err;
}

static void balanced_stop(void)
{
	/*
	   first unregister the notifiers. This ensures the governor state
	   can't be modified by a cpufreq transition
	*/
	cpufreq_unregister_notifier(&balanced_cpufreq_nb,
		CPUFREQ_TRANSITION_NOTIFIER);

	/* now we can force the governor to be idle */
	balanced_state = IDLE;
	cancel_delayed_work_sync(&balanced_work);
	destroy_workqueue(balanced_wq);
	del_timer(&load_timer);

	kobject_put(balanced_kobject);
}

static int balanced_start(void)
{
	int err, count;
	struct cpufreq_frequency_table *table;
	struct cpufreq_freqs initial_freq;

	err = balanced_sysfs();
	if (err)
		return err;

	balanced_wq = alloc_workqueue("cpuquiet-balanced",
			WQ_UNBOUND | WQ_FREEZABLE, 1);
	if (!balanced_wq)
		return -ENOMEM;

	INIT_DELAYED_WORK(&balanced_work, balanced_work_func);

	up_delay = msecs_to_jiffies(100);
	down_delay = msecs_to_jiffies(2000);

	table = cpufreq_frequency_get_table(0);
	if (!table)
		return -EINVAL;

	for (count = 0; table[count].frequency != CPUFREQ_TABLE_END; count++);

	if (count < 4)
		return -EINVAL;

	idle_top_freq = table[(count / 2) - 1].frequency;
	idle_bottom_freq = table[(count / 2) - 2].frequency;

	cpufreq_register_notifier(&balanced_cpufreq_nb,
		CPUFREQ_TRANSITION_NOTIFIER);

	init_timer(&load_timer);
	load_timer.function = calculate_load_timer;

	/*FIXME: Kick start the state machine by faking a freq notification*/
	initial_freq.new = cpufreq_get(0);
	if (initial_freq.new != 0)
		balanced_cpufreq_transition(NULL, CPUFREQ_RESUMECHANGE,
						&initial_freq);
	return 0;
}

struct cpuquiet_governor balanced_governor = {
	.name		= "balanced",
	.start		= balanced_start,
	.stop		= balanced_stop,
	.owner		= THIS_MODULE,
};

static int __init init_balanced(void)
{
	return cpuquiet_register_governor(&balanced_governor);
}

static void __exit exit_balanced(void)
{
	cpuquiet_unregister_governor(&balanced_governor);
}

MODULE_LICENSE("GPL");
#ifdef CONFIG_CPUQUIET_DEFAULT_GOV_BALANCED
fs_initcall(init_balanced);
#else
module_init(init_balanced);
#endif
module_exit(exit_balanced);

