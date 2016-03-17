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
 *              be able to differenciate the two cases and give a quantitative
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
#include <linux/freezer.h>
#include <linux/cpu.h>
#include <linux/thermal.h>
#include <linux/slab.h>
#include <linux/tick.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/sched/rt.h>

#include <asm/nmi.h>
#include <asm/msr.h>
#include <asm/mwait.h>
#include <asm/cpu_device_id.h>
#include <asm/idle.h>
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


static struct task_struct * __percpu *powerclamp_thread;
static struct thermal_cooling_device *cooling_dev;
static unsigned long *cpu_clamping_mask;  /* bit map for tracking per cpu
					   * clamping thread
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

static void noop_timer(unsigned long foo)
{
	/* empty... just the fact that we get the interrupt wakes us up */
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

static int clamp_thread(void *arg)
{
	int cpunr = (unsigned long)arg;
	DEFINE_TIMER(wakeup_timer, noop_timer, 0, 0);
	static const struct sched_param param = {
		.sched_priority = MAX_USER_RT_PRIO/2,
	};
	unsigned int count = 0;
	unsigned int target_ratio;

	set_bit(cpunr, cpu_clamping_mask);
	set_freezable();
	init_timer_on_stack(&wakeup_timer);
	sched_setscheduler(current, SCHED_FIFO, &param);

	while (true == clamping && !kthread_should_stop() &&
		cpu_online(cpunr)) {
		int sleeptime;
		unsigned long target_jiffies;
		unsigned int guard;
		unsigned int compensation = 0;
		int interval; /* jiffies to sleep for each attempt */
		unsigned int duration_jiffies = msecs_to_jiffies(duration);
		unsigned int window_size_now;

		try_to_freeze();
		/*
		 * make sure user selected ratio does not take effect until
		 * the next round. adjust target_ratio if user has changed
		 * target such that we can converge quickly.
		 */
		target_ratio = set_target_ratio;
		guard = 1 + target_ratio/20;
		window_size_now = window_size;
		count++;

		/*
		 * systems may have different ability to enter package level
		 * c-states, thus we need to compensate the injected idle ratio
		 * to achieve the actual target reported by the HW.
		 */
		compensation = get_compensation(target_ratio);
		interval = duration_jiffies*100/(target_ratio+compensation);

		/* align idle time */
		target_jiffies = roundup(jiffies, interval);
		sleeptime = target_jiffies - jiffies;
		if (sleeptime <= 0)
			sleeptime = 1;
		schedule_timeout_interruptible(sleeptime);
		/*
		 * only elected controlling cpu can collect stats and update
		 * control parameters.
		 */
		if (cpunr == control_cpu && !(count%window_size_now)) {
			should_skip =
				powerclamp_adjust_controls(target_ratio,
							guard, window_size_now);
			smp_mb();
		}

		if (should_skip)
			continue;

		target_jiffies = jiffies + duration_jiffies;
		mod_timer(&wakeup_timer, target_jiffies);
		if (unlikely(local_softirq_pending()))
			continue;
		/*
		 * stop tick sched during idle time, interrupts are still
		 * allowed. thus jiffies are updated properly.
		 */
		preempt_disable();
		/* mwait until target jiffies is reached */
		while (time_before(jiffies, target_jiffies)) {
			unsigned long ecx = 1;
			unsigned long eax = target_mwait;

			/*
			 * REVISIT: may call enter_idle() to notify drivers who
			 * can save power during cpu idle. same for exit_idle()
			 */
			local_touch_nmi();
			stop_critical_timings();
			mwait_idle_with_hints(eax, ecx);
			start_critical_timings();
			atomic_inc(&idle_wakeup_counter);
		}
		preempt_enable();
	}
	del_timer_sync(&wakeup_timer);
	clear_bit(cpunr, cpu_clamping_mask);

	return 0;
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
	static unsigned long jiffies_last;

	u64 msr_now;
	unsigned long jiffies_now;
	u64 tsc_now;
	u64 val64;

	msr_now = pkg_state_counter();
	tsc_now = rdtsc();
	jiffies_now = jiffies;

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
	jiffies_last = jiffies_now;
	tsc_last = tsc_now;

	if (true == clamping)
		schedule_delayed_work(&poll_pkg_cstate_work, HZ);
}

static int start_power_clamp(void)
{
	unsigned long cpu;
	struct task_struct *thread;

	set_target_ratio = clamp(set_target_ratio, 0U, MAX_TARGET_RATIO - 1);
	/* prevent cpu hotplug */
	get_online_cpus();

	/* prefer BSP */
	control_cpu = 0;
	if (!cpu_online(control_cpu))
		control_cpu = smp_processor_id();

	clamping = true;
	schedule_delayed_work(&poll_pkg_cstate_work, 0);

	/* start one thread per online cpu */
	for_each_online_cpu(cpu) {
		struct task_struct **p =
			per_cpu_ptr(powerclamp_thread, cpu);

		thread = kthread_create_on_node(clamp_thread,
						(void *) cpu,
						cpu_to_node(cpu),
						"kidle_inject/%ld", cpu);
		/* bind to cpu here */
		if (likely(!IS_ERR(thread))) {
			kthread_bind(thread, cpu);
			wake_up_process(thread);
			*p = thread;
		}

	}
	put_online_cpus();

	return 0;
}

static void end_power_clamp(void)
{
	int i;
	struct task_struct *thread;

	clamping = false;
	/*
	 * make clamping visible to other cpus and give per cpu clamping threads
	 * sometime to exit, or gets killed later.
	 */
	smp_mb();
	msleep(20);
	if (bitmap_weight(cpu_clamping_mask, num_possible_cpus())) {
		for_each_set_bit(i, cpu_clamping_mask, num_possible_cpus()) {
			pr_debug("clamping thread for cpu %d alive, kill\n", i);
			thread = *per_cpu_ptr(powerclamp_thread, i);
			kthread_stop(thread);
		}
	}
}

static int powerclamp_cpu_callback(struct notifier_block *nfb,
				unsigned long action, void *hcpu)
{
	unsigned long cpu = (unsigned long)hcpu;
	struct task_struct *thread;
	struct task_struct **percpu_thread =
		per_cpu_ptr(powerclamp_thread, cpu);

	if (false == clamping)
		goto exit_ok;

	switch (action) {
	case CPU_ONLINE:
		thread = kthread_create_on_node(clamp_thread,
						(void *) cpu,
						cpu_to_node(cpu),
						"kidle_inject/%lu", cpu);
		if (likely(!IS_ERR(thread))) {
			kthread_bind(thread, cpu);
			wake_up_process(thread);
			*percpu_thread = thread;
		}
		/* prefer BSP as controlling CPU */
		if (cpu == 0) {
			control_cpu = 0;
			smp_mb();
		}
		break;
	case CPU_DEAD:
		if (test_bit(cpu, cpu_clamping_mask)) {
			pr_err("cpu %lu dead but powerclamping thread is not\n",
				cpu);
			kthread_stop(*percpu_thread);
		}
		if (cpu == control_cpu) {
			control_cpu = smp_processor_id();
			smp_mb();
		}
	}

exit_ok:
	return NOTIFY_OK;
}

static struct notifier_block powerclamp_cpu_notifier = {
	.notifier_call = powerclamp_cpu_callback,
};

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
		set_target_ratio = 0;
		end_power_clamp();
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

static const struct x86_cpu_id intel_powerclamp_ids[] __initconst = {
	{ X86_VENDOR_INTEL, X86_FAMILY_ANY, X86_MODEL_ANY, X86_FEATURE_MWAIT },
	{ X86_VENDOR_INTEL, X86_FAMILY_ANY, X86_MODEL_ANY, X86_FEATURE_ARAT },
	{ X86_VENDOR_INTEL, X86_FAMILY_ANY, X86_MODEL_ANY, X86_FEATURE_NONSTOP_TSC },
	{ X86_VENDOR_INTEL, X86_FAMILY_ANY, X86_MODEL_ANY, X86_FEATURE_CONSTANT_TSC},
	{}
};
MODULE_DEVICE_TABLE(x86cpu, intel_powerclamp_ids);

static int __init powerclamp_probe(void)
{
	if (!x86_match_cpu(intel_powerclamp_ids)) {
		pr_err("Intel powerclamp does not run on family %d model %d\n",
				boot_cpu_data.x86, boot_cpu_data.x86_model);
		return -ENODEV;
	}

	/* The goal for idle time alignment is to achieve package cstate. */
	if (!has_pkg_state_counter()) {
		pr_info("No package C-state available");
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
	register_hotcpu_notifier(&powerclamp_cpu_notifier);

	powerclamp_thread = alloc_percpu(struct task_struct *);
	if (!powerclamp_thread) {
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
	free_percpu(powerclamp_thread);
exit_unregister:
	unregister_hotcpu_notifier(&powerclamp_cpu_notifier);
exit_free:
	kfree(cpu_clamping_mask);
	return retval;
}
module_init(powerclamp_init);

static void __exit powerclamp_exit(void)
{
	unregister_hotcpu_notifier(&powerclamp_cpu_notifier);
	end_power_clamp();
	free_percpu(powerclamp_thread);
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
