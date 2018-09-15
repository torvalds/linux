/*
 * intel_powerclamp.c - package c-state idle injection
 *
 * Copyright (c) 2012, Intel Corporation.
 *
 * Authors:
 *     Arjan van de Ven <arjan@linux.intel.com>
 *     Jacob Pan <jacob.jun.pan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 *	TODO:
 *           1. better handle wakeup from external interrupts, currently a fixed
 *              compensation is added to clamping duration when excessive amount
 *              of wakeups are observed during idle time. the reason is that in
 *              case of external interrupts without need for ack, clamping down
 *              cpu in non-irq context does not reduce irq. for majority of the
 *              cases, clamping down cpu does help reduce irq as well, we should
 *              be able to differentiate the two cases and give a quantitative
 *              solution for the irqs that we can control. perhaps based on
 *              get_cpu_iowait_time_us()
 *
 *	     2. synchronization with other hw blocks
 *
 *
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/cpu.h>
#include <linux/thermal.h>
#include <linux/slab.h>
#include <linux/tick.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/sched/rt.h>
#include <uapi/linux/sched/types.h>

#include <asm/nmi.h>
#include <asm/msr.h>
#include <asm/mwait.h>
#include <asm/cpu_device_id.h>
#include <asm/hardirq.h>

#define MAX_TARGET_RATIO (50U)
/* For each undisturbed clamping period (no extra wake ups during idle time),
 * we increment the confidence counter for the given target ratio.
 * CONFIDENCE_OK defines the level where runtime calibration results are
 * valid.
 */
#define CONFIDENCE_OK (3)
/* Default idle injection duration, driver adjust sleep time to meet target
 * idle ratio. Similar to frequency modulation.
 */
#define DEFAULT_DURATION_JIFFIES (6)

static unsigned int target_mwait;
static struct dentry *debug_dir;

/* user selected target */
static unsigned int set_target_ratio;
static unsigned int current_ratio;
static bool should_skip;
static bool reduce_irq;
static atomic_t idle_wakeup_counter;
static unsigned int control_cpu; /* The cpu assigned to collect stat and update
				  * control parameters. default to BSP but BSP
				  * can be offlined.
				  */
static bool clamping;

static const struct sched_param sparam = {
	.sched_priority = MAX_USER_RT_PRIO / 2,
};
struct powerclamp_worker_data {
	struct kthread_worker *worker;
	struct kthread_work balancing_work;
	struct kthread_delayed_work idle_injection_work;
	unsigned int cpu;
	unsigned int count;
	unsigned int guard;
	unsigned int window_size_now;
	unsigned int target_ratio;
	unsigned int duration_jiffies;
	bool clamping;
};

static struct powerclamp_worker_data * __percpu worker_data;
static struct thermal_cooling_device *cooling_dev;
static unsigned long *cpu_clamping_mask;  /* bit map for tracking per cpu
					   * clamping kthread worker
					   */

static unsigned int duration;
static unsigned int pkg_cstate_ratio_cur;
static unsigned int window_size;

static int duration_set(const char *arg, const struct kernel_param *kp)
{
	int ret = 0;
	unsigned long new_duration;

	ret = kstrtoul(arg, 10, &new_duration);
	if (ret)
		goto exit;
	if (new_duration > 25 || new_duration < 6) {
		pr_err("Out of recommended range %lu, between 6-25ms\n",
			new_duration);
		ret = -EINVAL;
	}

	duration = clamp(new_duration, 6ul, 25ul);
	smp_mb();

exit:

	return ret;
}

static const struct kernel_param_ops duration_ops = {
	.set = duration_set,
	.get = param_get_int,
};


module_param_cb(duration, &duration_ops, &duration, 0644);
MODULE_PARM_DESC(duration, "forced idle time for each attempt in msec.");

struct powerclamp_calibration_data {
	unsigned long confidence;  /* used for calibration, basically a counter
				    * gets incremented each time a clamping
				    * period is completed without extra wakeups
				    * once that counter is reached given level,
				    * compensation is deemed usable.
				    */
	unsigned long steady_comp; /* steady state compensation used when
				    * no extra wakeups occurred.
				    */
	unsigned long dynamic_comp; /* compensate excessive wakeup from idle
				     * mostly from external interrupts.
				     */
};

static struct powerclamp_calibration_data cal_data[MAX_TARGET_RATIO];

static int window_size_set(const char *arg, const struct kernel_param *kp)
{
	int ret = 0;
	unsigned long new_window_size;

	ret = kstrtoul(arg, 10, &new_window_size);
	if (ret)
		goto exit_win;
	if (new_window_size > 10 || new_window_size < 2) {
		pr_err("Out of recommended window size %lu, between 2-10\n",
			new_window_size);
		ret = -EINVAL;
	}

	window_size = clamp(new_window_size, 2ul, 10ul);
	smp_mb();

exit_win:

	return ret;
}

static const struct kernel_param_ops window_size_ops = {
	.set = window_size_set,
	.get = param_get_int,
};

module_param_cb(window_size, &window_size_ops, &window_size, 0644);
MODULE_PARM_DESC(window_size, "sliding window in number of clamping cycles\n"
	"\tpowerclamp controls idle ratio within this window. larger\n"
	"\twindow size results in slower response time but more smooth\n"
	"\tclamping results. default to 2.");

static void find_target_mwait(void)
{
	unsigned int eax, ebx, ecx, edx;
	unsigned int highest_cstate = 0;
	unsigned int highest_subcstate = 0;
	int i;

	if (boot_cpu_data.cpuid_level < CPUID_MWAIT_LEAF)
		return;

	cpuid(CPUID_MWAIT_LEAF, &eax, &ebx, &ecx, &edx);

	if (!(ecx & CPUID5_ECX_EXTENSIONS_SUPPORTED) ||
	    !(ecx & CPUID5_ECX_INTERRUPT_BREAK))
		return;

	edx >>= MWAIT_SUBSTATE_SIZE;
	for (i = 0; i < 7 && edx; i++, edx >>= MWAIT_SUBSTATE_SIZE) {
		if (edx & MWAIT_SUBSTATE_MASK) {
			highest_cstate = i;
			highest_subcstate = edx & MWAIT_SUBSTATE_MASK;
		}
	}
	target_mwait = (highest_cstate << MWAIT_SUBSTATE_SIZE) |
		(highest_subcstate - 1);

}

struct pkg_cstate_info {
	bool skip;
	int msr_index;
	int cstate_id;
};

#define PKG_CSTATE_INIT(id) {				\
		.msr_index = MSR_PKG_C##id##_RESIDENCY, \
		.cstate_id = id				\
			}

static struct pkg_cstate_info pkg_cstates[] = {
	PKG_CSTATE_INIT(2),
	PKG_CSTATE_INIT(3),
	PKG_CSTATE_INIT(6),
	PKG_CSTATE_INIT(7),
	PKG_CSTATE_INIT(8),
	PKG_CSTATE_INIT(9),
	PKG_CSTATE_INIT(10),
	{NULL},
};

static bool has_pkg_state_counter(void)
{
	u64 val;
	struct pkg_cstate_info *info = pkg_cstates;

	/* check if any one of the counter msrs exists */
	while (info->msr_index) {
		if (!rdmsrl_safe(info->msr_index, &val))
			return true;
		info++;
	}

	return false;
}

static u64 pkg_state_counter(void)
{
	u64 val;
	u64 count = 0;
	struct pkg_cstate_info *info = pkg_cstates;

	while (info->msr_index) {
		if (!info->skip) {
			if (!rdmsrl_safe(info->msr_index, &val))
				count += val;
			else
				info->skip = true;
		}
		info++;
	}

	return count;
}

static unsigned int get_compensation(int ratio)
{
	unsigned int comp = 0;

	/* we only use compensation if all adjacent ones are good */
	if (ratio == 1 &&
		cal_data[ratio].confidence >= CONFIDENCE_OK &&
		cal_data[ratio + 1].confidence >= CONFIDENCE_OK &&
		cal_data[ratio + 2].confidence >= CONFIDENCE_OK) {
		comp = (cal_data[ratio].steady_comp +
			cal_data[ratio + 1].steady_comp +
			cal_data[ratio + 2].steady_comp) / 3;
	} else if (ratio == MAX_TARGET_RATIO - 1 &&
		cal_data[ratio].confidence >= CONFIDENCE_OK &&
		cal_data[ratio - 1].confidence >= CONFIDENCE_OK &&
		cal_data[ratio - 2].confidence >= CONFIDENCE_OK) {
		comp = (cal_data[ratio].steady_comp +
			cal_data[ratio - 1].steady_comp +
			cal_data[ratio - 2].steady_comp) / 3;
	} else if (cal_data[ratio].confidence >= CONFIDENCE_OK &&
		cal_data[ratio - 1].confidence >= CONFIDENCE_OK &&
		cal_data[ratio + 1].confidence >= CONFIDENCE_OK) {
		comp = (cal_data[ratio].steady_comp +
			cal_data[ratio - 1].steady_comp +
			cal_data[ratio + 1].steady_comp) / 3;
	}

	/* REVISIT: simple penalty of double idle injection */
	if (reduce_irq)
		comp = ratio;
	/* do not exceed limit */
	if (comp + ratio >= MAX_TARGET_RATIO)
		comp = MAX_TARGET_RATIO - ratio - 1;

	return comp;
}

static void adjust_compensation(int target_ratio, unsigned int win)
{
	int delta;
	struct powerclamp_calibration_data *d = &cal_data[target_ratio];

	/*
	 * adjust compensations if confidence level has not been reached or
	 * there are too many wakeups during the last idle injection period, we
	 * cannot trust the data for compensation.
	 */
	if (d->confidence >= CONFIDENCE_OK ||
		atomic_read(&idle_wakeup_counter) >
		win * num_online_cpus())
		return;

	delta = set_target_ratio - current_ratio;
	/* filter out bad data */
	if (delta >= 0 && delta <= (1+target_ratio/10)) {
		if (d->steady_comp)
			d->steady_comp =
				roundup(delta+d->steady_comp, 2)/2;
		else
			d->steady_comp = delta;
		d->confidence++;
	}
}

static bool powerclamp_adjust_controls(unsigned int target_ratio,
				unsigned int guard, unsigned int win)
{
	static u64 msr_last, tsc_last;
	u64 msr_now, tsc_now;
	u64 val64;

	/* check result for the last window */
	msr_now = pkg_state_counter();
	tsc_now = rdtsc();

	/* calculate pkg cstate vs tsc ratio */
	if (!msr_last || !tsc_last)
		current_ratio = 1;
	else if (tsc_now-tsc_last) {
		val64 = 100*(msr_now-msr_last);
		do_div(val64, (tsc_now-tsc_last));
		current_ratio = val64;
	}

	/* update record */
	msr_last = msr_now;
	tsc_last = tsc_now;

	adjust_compensation(target_ratio, win);
	/*
	 * too many external interrupts, set flag such
	 * that we can take measure later.
	 */
	reduce_irq = atomic_read(&idle_wakeup_counter) >=
		2 * win * num_online_cpus();

	atomic_set(&idle_wakeup_counter, 0);
	/* if we are above target+guard, skip */
	return set_target_ratio + guard <= current_ratio;
}

static void clamp_balancing_func(struct kthread_work *work)
{
	struct powerclamp_worker_data *w_data;
	int sleeptime;
	unsigned long target_jiffies;
	unsigned int compensated_ratio;
	int interval; /* jiffies to sleep for each attempt */

	w_data = container_of(work, struct powerclamp_worker_data,
			      balancing_work);

	/*
	 * make sure user selected ratio does not take effect until
	 * the next round. adjust target_ratio if user has changed
	 * target such that we can converge quickly.
	 */
	w_data->target_ratio = READ_ONCE(set_target_ratio);
	w_data->guard = 1 + w_data->target_ratio / 20;
	w_data->window_size_now = window_size;
	w_data->duration_jiffies = msecs_to_jiffies(duration);
	w_data->count++;

	/*
	 * systems may have different ability to enter package level
	 * c-states, thus we need to compensate the injected idle ratio
	 * to achieve the actual target reported by the HW.
	 */
	compensated_ratio = w_data->target_ratio +
		get_compensation(w_data->target_ratio);
	if (compensated_ratio <= 0)
		compensated_ratio = 1;
	interval = w_data->duration_jiffies * 100 / compensated_ratio;

	/* align idle time */
	target_jiffies = roundup(jiffies, interval);
	sleeptime = target_jiffies - jiffies;
	if (sleeptime <= 0)
		sleeptime = 1;

	if (clamping && w_data->clamping && cpu_online(w_data->cpu))
		kthread_queue_delayed_work(w_data->worker,
					   &w_data->idle_injection_work,
					   sleeptime);
}

static void clamp_idle_injection_func(struct kthread_work *work)
{
	struct powerclamp_worker_data *w_data;

	w_data = container_of(work, struct powerclamp_worker_data,
			      idle_injection_work.work);

	/*
	 * only elected controlling cpu can collect stats and update
	 * control parameters.
	 */
	if (w_data->cpu == control_cpu &&
	    !(w_data->count % w_data->window_size_now)) {
		should_skip =
			powerclamp_adjust_controls(w_data->target_ratio,
						   w_data->guard,
						   w_data->window_size_now);
		smp_mb();
	}

	if (should_skip)
		goto balance;

	play_idle(jiffies_to_msecs(w_data->duration_jiffies));

balance:
	if (clamping && w_data->clamping && cpu_online(w_data->cpu))
		kthread_queue_work(w_data->worker, &w_data->balancing_work);
}

/*
 * 1 HZ polling while clamping is active, useful for userspace
 * to monitor actual idle ratio.
 */
static void poll_pkg_cstate(struct work_struct *dummy);
static DECLARE_DELAYED_WORK(poll_pkg_cstate_work, poll_pkg_cstate);
static void poll_pkg_cstate(struct work_struct *dummy)
{
	static u64 msr_last;
	static u64 tsc_last;

	u64 msr_now;
	u64 tsc_now;
	u64 val64;

	msr_now = pkg_state_counter();
	tsc_now = rdtsc();

	/* calculate pkg cstate vs tsc ratio */
	if (!msr_last || !tsc_last)
		pkg_cstate_ratio_cur = 1;
	else {
		if (tsc_now - tsc_last) {
			val64 = 100 * (msr_now - msr_last);
			do_div(val64, (tsc_now - tsc_last));
			pkg_cstate_ratio_cur = val64;
		}
	}

	/* update record */
	msr_last = msr_now;
	tsc_last = tsc_now;

	if (true == clamping)
		schedule_delayed_work(&poll_pkg_cstate_work, HZ);
}

static void start_power_clamp_worker(unsigned long cpu)
{
	struct powerclamp_worker_data *w_data = per_cpu_ptr(worker_data, cpu);
	struct kthread_worker *worker;

	worker = kthread_create_worker_on_cpu(cpu, 0, "kidle_inject/%ld", cpu);
	if (IS_ERR(worker))
		return;

	w_data->worker = worker;
	w_data->count = 0;
	w_data->cpu = cpu;
	w_data->clamping = true;
	set_bit(cpu, cpu_clamping_mask);
	sched_setscheduler(worker->task, SCHED_FIFO, &sparam);
	kthread_init_work(&w_data->balancing_work, clamp_balancing_func);
	kthread_init_delayed_work(&w_data->idle_injection_work,
				  clamp_idle_injection_func);
	kthread_queue_work(w_data->worker, &w_data->balancing_work);
}

static void stop_power_clamp_worker(unsigned long cpu)
{
	struct powerclamp_worker_data *w_data = per_cpu_ptr(worker_data, cpu);

	if (!w_data->worker)
		return;

	w_data->clamping = false;
	/*
	 * Make sure that all works that get queued after this point see
	 * the clamping disabled. The counter part is not needed because
	 * there is an implicit memory barrier when the queued work
	 * is proceed.
	 */
	smp_wmb();
	kthread_cancel_work_sync(&w_data->balancing_work);
	kthread_cancel_delayed_work_sync(&w_data->idle_injection_work);
	/*
	 * The balancing work still might be queued here because
	 * the handling of the "clapming" variable, cancel, and queue
	 * operations are not synchronized via a lock. But it is not
	 * a big deal. The balancing work is fast and destroy kthread
	 * will wait for it.
	 */
	clear_bit(w_data->cpu, cpu_clamping_mask);
	kthread_destroy_worker(w_data->worker);

	w_data->worker = NULL;
}

static int start_power_clamp(void)
{
	unsigned long cpu;

	set_target_ratio = clamp(set_target_ratio, 0U, MAX_TARGET_RATIO - 1);
	/* prevent cpu hotplug */
	get_online_cpus();

	/* prefer BSP */
	control_cpu = 0;
	if (!cpu_online(control_cpu))
		control_cpu = smp_processor_id();

	clamping = true;
	schedule_delayed_work(&poll_pkg_cstate_work, 0);

	/* start one kthread worker per online cpu */
	for_each_online_cpu(cpu) {
		start_power_clamp_worker(cpu);
	}
	put_online_cpus();

	return 0;
}

static void end_power_clamp(void)
{
	int i;

	/*
	 * Block requeuing in all the kthread workers. They will flush and
	 * stop faster.
	 */
	clamping = false;
	if (bitmap_weight(cpu_clamping_mask, num_possible_cpus())) {
		for_each_set_bit(i, cpu_clamping_mask, num_possible_cpus()) {
			pr_debug("clamping worker for cpu %d alive, destroy\n",
				 i);
			stop_power_clamp_worker(i);
		}
	}
}

static int powerclamp_cpu_online(unsigned int cpu)
{
	if (clamping == false)
		return 0;
	start_power_clamp_worker(cpu);
	/* prefer BSP as controlling CPU */
	if (cpu == 0) {
		control_cpu = 0;
		smp_mb();
	}
	return 0;
}

static int powerclamp_cpu_predown(unsigned int cpu)
{
	if (clamping == false)
		return 0;

	stop_power_clamp_worker(cpu);
	if (cpu != control_cpu)
		return 0;

	control_cpu = cpumask_first(cpu_online_mask);
	if (control_cpu == cpu)
		control_cpu = cpumask_next(cpu, cpu_online_mask);
	smp_mb();
	return 0;
}

static int powerclamp_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	*state = MAX_TARGET_RATIO;

	return 0;
}

static int powerclamp_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	if (true == clamping)
		*state = pkg_cstate_ratio_cur;
	else
		/* to save power, do not poll idle ratio while not clamping */
		*state = -1; /* indicates invalid state */

	return 0;
}

static int powerclamp_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long new_target_ratio)
{
	int ret = 0;

	new_target_ratio = clamp(new_target_ratio, 0UL,
				(unsigned long) (MAX_TARGET_RATIO-1));
	if (set_target_ratio == 0 && new_target_ratio > 0) {
		pr_info("Start idle injection to reduce power\n");
		set_target_ratio = new_target_ratio;
		ret = start_power_clamp();
		goto exit_set;
	} else	if (set_target_ratio > 0 && new_target_ratio == 0) {
		pr_info("Stop forced idle injection\n");
		end_power_clamp();
		set_target_ratio = 0;
	} else	/* adjust currently running */ {
		set_target_ratio = new_target_ratio;
		/* make new set_target_ratio visible to other cpus */
		smp_mb();
	}

exit_set:
	return ret;
}

/* bind to generic thermal layer as cooling device*/
static struct thermal_cooling_device_ops powerclamp_cooling_ops = {
	.get_max_state = powerclamp_get_max_state,
	.get_cur_state = powerclamp_get_cur_state,
	.set_cur_state = powerclamp_set_cur_state,
};

static const struct x86_cpu_id __initconst intel_powerclamp_ids[] = {
	{ X86_VENDOR_INTEL, X86_FAMILY_ANY, X86_MODEL_ANY, X86_FEATURE_MWAIT },
	{}
};
MODULE_DEVICE_TABLE(x86cpu, intel_powerclamp_ids);

static int __init powerclamp_probe(void)
{

	if (!x86_match_cpu(intel_powerclamp_ids)) {
		pr_err("CPU does not support MWAIT\n");
		return -ENODEV;
	}

	/* The goal for idle time alignment is to achieve package cstate. */
	if (!has_pkg_state_counter()) {
		pr_info("No package C-state available\n");
		return -ENODEV;
	}

	/* find the deepest mwait value */
	find_target_mwait();

	return 0;
}

static int powerclamp_debug_show(struct seq_file *m, void *unused)
{
	int i = 0;

	seq_printf(m, "controlling cpu: %d\n", control_cpu);
	seq_printf(m, "pct confidence steady dynamic (compensation)\n");
	for (i = 0; i < MAX_TARGET_RATIO; i++) {
		seq_printf(m, "%d\t%lu\t%lu\t%lu\n",
			i,
			cal_data[i].confidence,
			cal_data[i].steady_comp,
			cal_data[i].dynamic_comp);
	}

	return 0;
}

static int powerclamp_debug_open(struct inode *inode,
			struct file *file)
{
	return single_open(file, powerclamp_debug_show, inode->i_private);
}

static const struct file_operations powerclamp_debug_fops = {
	.open		= powerclamp_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.owner		= THIS_MODULE,
};

static inline void powerclamp_create_debug_files(void)
{
	debug_dir = debugfs_create_dir("intel_powerclamp", NULL);
	if (!debug_dir)
		return;

	if (!debugfs_create_file("powerclamp_calib", S_IRUGO, debug_dir,
					cal_data, &powerclamp_debug_fops))
		goto file_error;

	return;

file_error:
	debugfs_remove_recursive(debug_dir);
}

static enum cpuhp_state hp_state;

static int __init powerclamp_init(void)
{
	int retval;
	int bitmap_size;

	bitmap_size = BITS_TO_LONGS(num_possible_cpus()) * sizeof(long);
	cpu_clamping_mask = kzalloc(bitmap_size, GFP_KERNEL);
	if (!cpu_clamping_mask)
		return -ENOMEM;

	/* probe cpu features and ids here */
	retval = powerclamp_probe();
	if (retval)
		goto exit_free;

	/* set default limit, maybe adjusted during runtime based on feedback */
	window_size = 2;
	retval = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					   "thermal/intel_powerclamp:online",
					   powerclamp_cpu_online,
					   powerclamp_cpu_predown);
	if (retval < 0)
		goto exit_free;

	hp_state = retval;

	worker_data = alloc_percpu(struct powerclamp_worker_data);
	if (!worker_data) {
		retval = -ENOMEM;
		goto exit_unregister;
	}

	cooling_dev = thermal_cooling_device_register("intel_powerclamp", NULL,
						&powerclamp_cooling_ops);
	if (IS_ERR(cooling_dev)) {
		retval = -ENODEV;
		goto exit_free_thread;
	}

	if (!duration)
		duration = jiffies_to_msecs(DEFAULT_DURATION_JIFFIES);

	powerclamp_create_debug_files();

	return 0;

exit_free_thread:
	free_percpu(worker_data);
exit_unregister:
	cpuhp_remove_state_nocalls(hp_state);
exit_free:
	kfree(cpu_clamping_mask);
	return retval;
}
module_init(powerclamp_init);

static void __exit powerclamp_exit(void)
{
	end_power_clamp();
	cpuhp_remove_state_nocalls(hp_state);
	free_percpu(worker_data);
	thermal_cooling_device_unregister(cooling_dev);
	kfree(cpu_clamping_mask);

	cancel_delayed_work_sync(&poll_pkg_cstate_work);
	debugfs_remove_recursive(debug_dir);
}
module_exit(powerclamp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arjan van de Ven <arjan@linux.intel.com>");
MODULE_AUTHOR("Jacob Pan <jacob.jun.pan@linux.intel.com>");
MODULE_DESCRIPTION("Package Level C-state Idle Injection for Intel CPUs");
