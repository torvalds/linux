/*
 * cpuidle.c - core cpuidle infrastructure
 *
 * (C) 2006-2007 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *               Shaohua Li <shaohua.li@intel.com>
 *               Adam Belay <abelay@novell.com>
 *
 * This code is licenced under the GPL.
 */

#include <linux/clockchips.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/notifier.h>
#include <linux/pm_qos.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/tick.h>
#include <trace/events/power.h>

#include "cpuidle.h"

DEFINE_PER_CPU(struct cpuidle_device *, cpuidle_devices);
DEFINE_PER_CPU(struct cpuidle_device, cpuidle_dev);

DEFINE_MUTEX(cpuidle_lock);
LIST_HEAD(cpuidle_detected_devices);

static int enabled_devices;
static int off __read_mostly;
static int initialized __read_mostly;

int cpuidle_disabled(void)
{
	return off;
}
void disable_cpuidle(void)
{
	off = 1;
}

bool cpuidle_not_available(struct cpuidle_driver *drv,
			   struct cpuidle_device *dev)
{
	return off || !initialized || !drv || !dev || !dev->enabled;
}

/**
 * cpuidle_play_dead - cpu off-lining
 *
 * Returns in case of an error or no driver
 */
int cpuidle_play_dead(void)
{
	struct cpuidle_device *dev = __this_cpu_read(cpuidle_devices);
	struct cpuidle_driver *drv = cpuidle_get_cpu_driver(dev);
	int i;

	if (!drv)
		return -ENODEV;

	/* Find lowest-power state that supports long-term idle */
	for (i = drv->state_count - 1; i >= 0; i--)
		if (drv->states[i].enter_dead)
			return drv->states[i].enter_dead(dev, i);

	return -ENODEV;
}

static int find_deepest_state(struct cpuidle_driver *drv,
			      struct cpuidle_device *dev,
			      unsigned int max_latency,
			      unsigned int forbidden_flags,
			      bool s2idle)
{
	unsigned int latency_req = 0;
	int i, ret = 0;

	for (i = 1; i < drv->state_count; i++) {
		struct cpuidle_state *s = &drv->states[i];
		struct cpuidle_state_usage *su = &dev->states_usage[i];

		if (s->disabled || su->disable || s->exit_latency <= latency_req
		    || s->exit_latency > max_latency
		    || (s->flags & forbidden_flags)
		    || (s2idle && !s->enter_s2idle))
			continue;

		latency_req = s->exit_latency;
		ret = i;
	}
	return ret;
}

/**
 * cpuidle_use_deepest_state - Set/clear governor override flag.
 * @enable: New value of the flag.
 *
 * Set/unset the current CPU to use the deepest idle state (override governors
 * going forward if set).
 */
void cpuidle_use_deepest_state(bool enable)
{
	struct cpuidle_device *dev;

	preempt_disable();
	dev = cpuidle_get_device();
	if (dev)
		dev->use_deepest_state = enable;
	preempt_enable();
}

/**
 * cpuidle_find_deepest_state - Find the deepest available idle state.
 * @drv: cpuidle driver for the given CPU.
 * @dev: cpuidle device for the given CPU.
 */
int cpuidle_find_deepest_state(struct cpuidle_driver *drv,
			       struct cpuidle_device *dev)
{
	return find_deepest_state(drv, dev, UINT_MAX, 0, false);
}

#ifdef CONFIG_SUSPEND
static void enter_s2idle_proper(struct cpuidle_driver *drv,
				struct cpuidle_device *dev, int index)
{
	ktime_t time_start, time_end;

	time_start = ns_to_ktime(local_clock());

	/*
	 * trace_suspend_resume() called by tick_freeze() for the last CPU
	 * executing it contains RCU usage regarded as invalid in the idle
	 * context, so tell RCU about that.
	 */
	RCU_NONIDLE(tick_freeze());
	/*
	 * The state used here cannot be a "coupled" one, because the "coupled"
	 * cpuidle mechanism enables interrupts and doing that with timekeeping
	 * suspended is generally unsafe.
	 */
	stop_critical_timings();
	drv->states[index].enter_s2idle(dev, drv, index);
	WARN_ON(!irqs_disabled());
	/*
	 * timekeeping_resume() that will be called by tick_unfreeze() for the
	 * first CPU executing it calls functions containing RCU read-side
	 * critical sections, so tell RCU about that.
	 */
	RCU_NONIDLE(tick_unfreeze());
	start_critical_timings();

	time_end = ns_to_ktime(local_clock());

	dev->states_usage[index].s2idle_time += ktime_us_delta(time_end, time_start);
	dev->states_usage[index].s2idle_usage++;
}

/**
 * cpuidle_enter_s2idle - Enter an idle state suitable for suspend-to-idle.
 * @drv: cpuidle driver for the given CPU.
 * @dev: cpuidle device for the given CPU.
 *
 * If there are states with the ->enter_s2idle callback, find the deepest of
 * them and enter it with frozen tick.
 */
int cpuidle_enter_s2idle(struct cpuidle_driver *drv, struct cpuidle_device *dev)
{
	int index;

	/*
	 * Find the deepest state with ->enter_s2idle present, which guarantees
	 * that interrupts won't be enabled when it exits and allows the tick to
	 * be frozen safely.
	 */
	index = find_deepest_state(drv, dev, UINT_MAX, 0, true);
	if (index > 0)
		enter_s2idle_proper(drv, dev, index);

	return index;
}
#endif /* CONFIG_SUSPEND */

/**
 * cpuidle_enter_state - enter the state and update stats
 * @dev: cpuidle device for this cpu
 * @drv: cpuidle driver for this cpu
 * @index: index into the states table in @drv of the state to enter
 */
int cpuidle_enter_state(struct cpuidle_device *dev, struct cpuidle_driver *drv,
			int index)
{
	int entered_state;

	struct cpuidle_state *target_state = &drv->states[index];
	bool broadcast = !!(target_state->flags & CPUIDLE_FLAG_TIMER_STOP);
	ktime_t time_start, time_end;
	s64 diff;

	/*
	 * Tell the time framework to switch to a broadcast timer because our
	 * local timer will be shut down.  If a local timer is used from another
	 * CPU as a broadcast timer, this call may fail if it is not available.
	 */
	if (broadcast && tick_broadcast_enter()) {
		index = find_deepest_state(drv, dev, target_state->exit_latency,
					   CPUIDLE_FLAG_TIMER_STOP, false);
		if (index < 0) {
			default_idle_call();
			return -EBUSY;
		}
		target_state = &drv->states[index];
		broadcast = false;
	}

	/* Take note of the planned idle state. */
	sched_idle_set_state(target_state);

	trace_cpu_idle_rcuidle(index, dev->cpu);
	time_start = ns_to_ktime(local_clock());

	stop_critical_timings();
	entered_state = target_state->enter(dev, drv, index);
	start_critical_timings();

	sched_clock_idle_wakeup_event();
	time_end = ns_to_ktime(local_clock());
	trace_cpu_idle_rcuidle(PWR_EVENT_EXIT, dev->cpu);

	/* The cpu is no longer idle or about to enter idle. */
	sched_idle_set_state(NULL);

	if (broadcast) {
		if (WARN_ON_ONCE(!irqs_disabled()))
			local_irq_disable();

		tick_broadcast_exit();
	}

	if (!cpuidle_state_is_coupled(drv, index))
		local_irq_enable();

	diff = ktime_us_delta(time_end, time_start);
	if (diff > INT_MAX)
		diff = INT_MAX;

	dev->last_residency = (int) diff;

	if (entered_state >= 0) {
		/* Update cpuidle counters */
		/* This can be moved to within driver enter routine
		 * but that results in multiple copies of same code.
		 */
		dev->states_usage[entered_state].time += dev->last_residency;
		dev->states_usage[entered_state].usage++;
	} else {
		dev->last_residency = 0;
	}

	return entered_state;
}

/**
 * cpuidle_select - ask the cpuidle framework to choose an idle state
 *
 * @drv: the cpuidle driver
 * @dev: the cpuidle device
 * @stop_tick: indication on whether or not to stop the tick
 *
 * Returns the index of the idle state.  The return value must not be negative.
 *
 * The memory location pointed to by @stop_tick is expected to be written the
 * 'false' boolean value if the scheduler tick should not be stopped before
 * entering the returned state.
 */
int cpuidle_select(struct cpuidle_driver *drv, struct cpuidle_device *dev,
		   bool *stop_tick)
{
	return cpuidle_curr_governor->select(drv, dev, stop_tick);
}

/**
 * cpuidle_enter - enter into the specified idle state
 *
 * @drv:   the cpuidle driver tied with the cpu
 * @dev:   the cpuidle device
 * @index: the index in the idle state table
 *
 * Returns the index in the idle state, < 0 in case of error.
 * The error code depends on the backend driver
 */
int cpuidle_enter(struct cpuidle_driver *drv, struct cpuidle_device *dev,
		  int index)
{
	if (cpuidle_state_is_coupled(drv, index))
		return cpuidle_enter_state_coupled(dev, drv, index);
	return cpuidle_enter_state(dev, drv, index);
}

/**
 * cpuidle_reflect - tell the underlying governor what was the state
 * we were in
 *
 * @dev  : the cpuidle device
 * @index: the index in the idle state table
 *
 */
void cpuidle_reflect(struct cpuidle_device *dev, int index)
{
	if (cpuidle_curr_governor->reflect && index >= 0)
		cpuidle_curr_governor->reflect(dev, index);
}

/**
 * cpuidle_install_idle_handler - installs the cpuidle idle loop handler
 */
void cpuidle_install_idle_handler(void)
{
	if (enabled_devices) {
		/* Make sure all changes finished before we switch to new idle */
		smp_wmb();
		initialized = 1;
	}
}

/**
 * cpuidle_uninstall_idle_handler - uninstalls the cpuidle idle loop handler
 */
void cpuidle_uninstall_idle_handler(void)
{
	if (enabled_devices) {
		initialized = 0;
		wake_up_all_idle_cpus();
	}

	/*
	 * Make sure external observers (such as the scheduler)
	 * are done looking at pointed idle states.
	 */
	synchronize_rcu();
}

/**
 * cpuidle_pause_and_lock - temporarily disables CPUIDLE
 */
void cpuidle_pause_and_lock(void)
{
	mutex_lock(&cpuidle_lock);
	cpuidle_uninstall_idle_handler();
}

EXPORT_SYMBOL_GPL(cpuidle_pause_and_lock);

/**
 * cpuidle_resume_and_unlock - resumes CPUIDLE operation
 */
void cpuidle_resume_and_unlock(void)
{
	cpuidle_install_idle_handler();
	mutex_unlock(&cpuidle_lock);
}

EXPORT_SYMBOL_GPL(cpuidle_resume_and_unlock);

/* Currently used in suspend/resume path to suspend cpuidle */
void cpuidle_pause(void)
{
	mutex_lock(&cpuidle_lock);
	cpuidle_uninstall_idle_handler();
	mutex_unlock(&cpuidle_lock);
}

/* Currently used in suspend/resume path to resume cpuidle */
void cpuidle_resume(void)
{
	mutex_lock(&cpuidle_lock);
	cpuidle_install_idle_handler();
	mutex_unlock(&cpuidle_lock);
}

/**
 * cpuidle_enable_device - enables idle PM for a CPU
 * @dev: the CPU
 *
 * This function must be called between cpuidle_pause_and_lock and
 * cpuidle_resume_and_unlock when used externally.
 */
int cpuidle_enable_device(struct cpuidle_device *dev)
{
	int ret;
	struct cpuidle_driver *drv;

	if (!dev)
		return -EINVAL;

	if (dev->enabled)
		return 0;

	if (!cpuidle_curr_governor)
		return -EIO;

	drv = cpuidle_get_cpu_driver(dev);

	if (!drv)
		return -EIO;

	if (!dev->registered)
		return -EINVAL;

	ret = cpuidle_add_device_sysfs(dev);
	if (ret)
		return ret;

	if (cpuidle_curr_governor->enable) {
		ret = cpuidle_curr_governor->enable(drv, dev);
		if (ret)
			goto fail_sysfs;
	}

	smp_wmb();

	dev->enabled = 1;

	enabled_devices++;
	return 0;

fail_sysfs:
	cpuidle_remove_device_sysfs(dev);

	return ret;
}

EXPORT_SYMBOL_GPL(cpuidle_enable_device);

/**
 * cpuidle_disable_device - disables idle PM for a CPU
 * @dev: the CPU
 *
 * This function must be called between cpuidle_pause_and_lock and
 * cpuidle_resume_and_unlock when used externally.
 */
void cpuidle_disable_device(struct cpuidle_device *dev)
{
	struct cpuidle_driver *drv = cpuidle_get_cpu_driver(dev);

	if (!dev || !dev->enabled)
		return;

	if (!drv || !cpuidle_curr_governor)
		return;

	dev->enabled = 0;

	if (cpuidle_curr_governor->disable)
		cpuidle_curr_governor->disable(drv, dev);

	cpuidle_remove_device_sysfs(dev);
	enabled_devices--;
}

EXPORT_SYMBOL_GPL(cpuidle_disable_device);

static void __cpuidle_unregister_device(struct cpuidle_device *dev)
{
	struct cpuidle_driver *drv = cpuidle_get_cpu_driver(dev);

	list_del(&dev->device_list);
	per_cpu(cpuidle_devices, dev->cpu) = NULL;
	module_put(drv->owner);

	dev->registered = 0;
}

static void __cpuidle_device_init(struct cpuidle_device *dev)
{
	memset(dev->states_usage, 0, sizeof(dev->states_usage));
	dev->last_residency = 0;
}

/**
 * __cpuidle_register_device - internal register function called before register
 * and enable routines
 * @dev: the cpu
 *
 * cpuidle_lock mutex must be held before this is called
 */
static int __cpuidle_register_device(struct cpuidle_device *dev)
{
	int ret;
	struct cpuidle_driver *drv = cpuidle_get_cpu_driver(dev);

	if (!try_module_get(drv->owner))
		return -EINVAL;

	per_cpu(cpuidle_devices, dev->cpu) = dev;
	list_add(&dev->device_list, &cpuidle_detected_devices);

	ret = cpuidle_coupled_register_device(dev);
	if (ret)
		__cpuidle_unregister_device(dev);
	else
		dev->registered = 1;

	return ret;
}

/**
 * cpuidle_register_device - registers a CPU's idle PM feature
 * @dev: the cpu
 */
int cpuidle_register_device(struct cpuidle_device *dev)
{
	int ret = -EBUSY;

	if (!dev)
		return -EINVAL;

	mutex_lock(&cpuidle_lock);

	if (dev->registered)
		goto out_unlock;

	__cpuidle_device_init(dev);

	ret = __cpuidle_register_device(dev);
	if (ret)
		goto out_unlock;

	ret = cpuidle_add_sysfs(dev);
	if (ret)
		goto out_unregister;

	ret = cpuidle_enable_device(dev);
	if (ret)
		goto out_sysfs;

	cpuidle_install_idle_handler();

out_unlock:
	mutex_unlock(&cpuidle_lock);

	return ret;

out_sysfs:
	cpuidle_remove_sysfs(dev);
out_unregister:
	__cpuidle_unregister_device(dev);
	goto out_unlock;
}

EXPORT_SYMBOL_GPL(cpuidle_register_device);

/**
 * cpuidle_unregister_device - unregisters a CPU's idle PM feature
 * @dev: the cpu
 */
void cpuidle_unregister_device(struct cpuidle_device *dev)
{
	if (!dev || dev->registered == 0)
		return;

	cpuidle_pause_and_lock();

	cpuidle_disable_device(dev);

	cpuidle_remove_sysfs(dev);

	__cpuidle_unregister_device(dev);

	cpuidle_coupled_unregister_device(dev);

	cpuidle_resume_and_unlock();
}

EXPORT_SYMBOL_GPL(cpuidle_unregister_device);

/**
 * cpuidle_unregister: unregister a driver and the devices. This function
 * can be used only if the driver has been previously registered through
 * the cpuidle_register function.
 *
 * @drv: a valid pointer to a struct cpuidle_driver
 */
void cpuidle_unregister(struct cpuidle_driver *drv)
{
	int cpu;
	struct cpuidle_device *device;

	for_each_cpu(cpu, drv->cpumask) {
		device = &per_cpu(cpuidle_dev, cpu);
		cpuidle_unregister_device(device);
	}

	cpuidle_unregister_driver(drv);
}
EXPORT_SYMBOL_GPL(cpuidle_unregister);

/**
 * cpuidle_register: registers the driver and the cpu devices with the
 * coupled_cpus passed as parameter. This function is used for all common
 * initialization pattern there are in the arch specific drivers. The
 * devices is globally defined in this file.
 *
 * @drv         : a valid pointer to a struct cpuidle_driver
 * @coupled_cpus: a cpumask for the coupled states
 *
 * Returns 0 on success, < 0 otherwise
 */
int cpuidle_register(struct cpuidle_driver *drv,
		     const struct cpumask *const coupled_cpus)
{
	int ret, cpu;
	struct cpuidle_device *device;

	ret = cpuidle_register_driver(drv);
	if (ret) {
		pr_err("failed to register cpuidle driver\n");
		return ret;
	}

	for_each_cpu(cpu, drv->cpumask) {
		device = &per_cpu(cpuidle_dev, cpu);
		device->cpu = cpu;

#ifdef CONFIG_ARCH_NEEDS_CPU_IDLE_COUPLED
		/*
		 * On multiplatform for ARM, the coupled idle states could be
		 * enabled in the kernel even if the cpuidle driver does not
		 * use it. Note, coupled_cpus is a struct copy.
		 */
		if (coupled_cpus)
			device->coupled_cpus = *coupled_cpus;
#endif
		ret = cpuidle_register_device(device);
		if (!ret)
			continue;

		pr_err("Failed to register cpuidle device for cpu%d\n", cpu);

		cpuidle_unregister(drv);
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(cpuidle_register);

#ifdef CONFIG_SMP

/*
 * This function gets called when a part of the kernel has a new latency
 * requirement.  This means we need to get all processors out of their C-state,
 * and then recalculate a new suitable C-state. Just do a cross-cpu IPI; that
 * wakes them all right up.
 */
static int cpuidle_latency_notify(struct notifier_block *b,
		unsigned long l, void *v)
{
	wake_up_all_idle_cpus();
	return NOTIFY_OK;
}

static struct notifier_block cpuidle_latency_notifier = {
	.notifier_call = cpuidle_latency_notify,
};

static inline void latency_notifier_init(struct notifier_block *n)
{
	pm_qos_add_notifier(PM_QOS_CPU_DMA_LATENCY, n);
}

#else /* CONFIG_SMP */

#define latency_notifier_init(x) do { } while (0)

#endif /* CONFIG_SMP */

/**
 * cpuidle_init - core initializer
 */
static int __init cpuidle_init(void)
{
	int ret;

	if (cpuidle_disabled())
		return -ENODEV;

	ret = cpuidle_add_interface(cpu_subsys.dev_root);
	if (ret)
		return ret;

	latency_notifier_init(&cpuidle_latency_notifier);

	return 0;
}

module_param(off, int, 0444);
core_initcall(cpuidle_init);
