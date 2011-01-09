/*
 * cpuidle.c - core cpuidle infrastructure
 *
 * (C) 2006-2007 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *               Shaohua Li <shaohua.li@intel.com>
 *               Adam Belay <abelay@novell.com>
 *
 * This code is licenced under the GPL.
 */

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/notifier.h>
#include <linux/pm_qos_params.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <trace/events/power.h>

#include "cpuidle.h"

DEFINE_PER_CPU(struct cpuidle_device *, cpuidle_devices);

DEFINE_MUTEX(cpuidle_lock);
LIST_HEAD(cpuidle_detected_devices);
static void (*pm_idle_old)(void);

static int enabled_devices;

#if defined(CONFIG_ARCH_HAS_CPU_IDLE_WAIT)
static void cpuidle_kick_cpus(void)
{
	cpu_idle_wait();
}
#elif defined(CONFIG_SMP)
# error "Arch needs cpu_idle_wait() equivalent here"
#else /* !CONFIG_ARCH_HAS_CPU_IDLE_WAIT && !CONFIG_SMP */
static void cpuidle_kick_cpus(void) {}
#endif

static int __cpuidle_register_device(struct cpuidle_device *dev);

/**
 * cpuidle_idle_call - the main idle loop
 *
 * NOTE: no locks or semaphores should be used here
 */
static void cpuidle_idle_call(void)
{
	struct cpuidle_device *dev = __this_cpu_read(cpuidle_devices);
	struct cpuidle_state *target_state;
	int next_state;

	/* check if the device is ready */
	if (!dev || !dev->enabled) {
		if (pm_idle_old)
			pm_idle_old();
		else
#if defined(CONFIG_ARCH_HAS_DEFAULT_IDLE)
			default_idle();
#else
			local_irq_enable();
#endif
		return;
	}

#if 0
	/* shows regressions, re-enable for 2.6.29 */
	/*
	 * run any timers that can be run now, at this point
	 * before calculating the idle duration etc.
	 */
	hrtimer_peek_ahead_timers();
#endif

	/*
	 * Call the device's prepare function before calling the
	 * governor's select function.  ->prepare gives the device's
	 * cpuidle driver a chance to update any dynamic information
	 * of its cpuidle states for the current idle period, e.g.
	 * state availability, latencies, residencies, etc.
	 */
	if (dev->prepare)
		dev->prepare(dev);

	/* ask the governor for the next state */
	next_state = cpuidle_curr_governor->select(dev);
	if (need_resched()) {
		local_irq_enable();
		return;
	}

	target_state = &dev->states[next_state];

	/* enter the state and update stats */
	dev->last_state = target_state;
	dev->last_residency = target_state->enter(dev, target_state);
	if (dev->last_state)
		target_state = dev->last_state;

	target_state->time += (unsigned long long)dev->last_residency;
	target_state->usage++;

	/* give the governor an opportunity to reflect on the outcome */
	if (cpuidle_curr_governor->reflect)
		cpuidle_curr_governor->reflect(dev);
	trace_power_end(smp_processor_id());
	trace_cpu_idle(PWR_EVENT_EXIT, smp_processor_id());
}

/**
 * cpuidle_install_idle_handler - installs the cpuidle idle loop handler
 */
void cpuidle_install_idle_handler(void)
{
	if (enabled_devices && (pm_idle != cpuidle_idle_call)) {
		/* Make sure all changes finished before we switch to new idle */
		smp_wmb();
		pm_idle = cpuidle_idle_call;
	}
}

/**
 * cpuidle_uninstall_idle_handler - uninstalls the cpuidle idle loop handler
 */
void cpuidle_uninstall_idle_handler(void)
{
	if (enabled_devices && pm_idle_old && (pm_idle != pm_idle_old)) {
		pm_idle = pm_idle_old;
		cpuidle_kick_cpus();
	}
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

/**
 * cpuidle_enable_device - enables idle PM for a CPU
 * @dev: the CPU
 *
 * This function must be called between cpuidle_pause_and_lock and
 * cpuidle_resume_and_unlock when used externally.
 */
int cpuidle_enable_device(struct cpuidle_device *dev)
{
	int ret, i;

	if (dev->enabled)
		return 0;
	if (!cpuidle_get_driver() || !cpuidle_curr_governor)
		return -EIO;
	if (!dev->state_count)
		return -EINVAL;

	if (dev->registered == 0) {
		ret = __cpuidle_register_device(dev);
		if (ret)
			return ret;
	}

	if ((ret = cpuidle_add_state_sysfs(dev)))
		return ret;

	if (cpuidle_curr_governor->enable &&
	    (ret = cpuidle_curr_governor->enable(dev)))
		goto fail_sysfs;

	for (i = 0; i < dev->state_count; i++) {
		dev->states[i].usage = 0;
		dev->states[i].time = 0;
	}
	dev->last_residency = 0;
	dev->last_state = NULL;

	smp_wmb();

	dev->enabled = 1;

	enabled_devices++;
	return 0;

fail_sysfs:
	cpuidle_remove_state_sysfs(dev);

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
	if (!dev->enabled)
		return;
	if (!cpuidle_get_driver() || !cpuidle_curr_governor)
		return;

	dev->enabled = 0;

	if (cpuidle_curr_governor->disable)
		cpuidle_curr_governor->disable(dev);

	cpuidle_remove_state_sysfs(dev);
	enabled_devices--;
}

EXPORT_SYMBOL_GPL(cpuidle_disable_device);

#ifdef CONFIG_ARCH_HAS_CPU_RELAX
static int poll_idle(struct cpuidle_device *dev, struct cpuidle_state *st)
{
	ktime_t	t1, t2;
	s64 diff;
	int ret;

	t1 = ktime_get();
	local_irq_enable();
	while (!need_resched())
		cpu_relax();

	t2 = ktime_get();
	diff = ktime_to_us(ktime_sub(t2, t1));
	if (diff > INT_MAX)
		diff = INT_MAX;

	ret = (int) diff;
	return ret;
}

static void poll_idle_init(struct cpuidle_device *dev)
{
	struct cpuidle_state *state = &dev->states[0];

	cpuidle_set_statedata(state, NULL);

	snprintf(state->name, CPUIDLE_NAME_LEN, "C0");
	snprintf(state->desc, CPUIDLE_DESC_LEN, "CPUIDLE CORE POLL IDLE");
	state->exit_latency = 0;
	state->target_residency = 0;
	state->power_usage = -1;
	state->flags = CPUIDLE_FLAG_POLL;
	state->enter = poll_idle;
}
#else
static void poll_idle_init(struct cpuidle_device *dev) {}
#endif /* CONFIG_ARCH_HAS_CPU_RELAX */

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
	struct sys_device *sys_dev = get_cpu_sysdev((unsigned long)dev->cpu);
	struct cpuidle_driver *cpuidle_driver = cpuidle_get_driver();

	if (!sys_dev)
		return -EINVAL;
	if (!try_module_get(cpuidle_driver->owner))
		return -EINVAL;

	init_completion(&dev->kobj_unregister);

	poll_idle_init(dev);

	/*
	 * cpuidle driver should set the dev->power_specified bit
	 * before registering the device if the driver provides
	 * power_usage numbers.
	 *
	 * For those devices whose ->power_specified is not set,
	 * we fill in power_usage with decreasing values as the
	 * cpuidle code has an implicit assumption that state Cn
	 * uses less power than C(n-1).
	 *
	 * With CONFIG_ARCH_HAS_CPU_RELAX, C0 is already assigned
	 * an power value of -1.  So we use -2, -3, etc, for other
	 * c-states.
	 */
	if (!dev->power_specified) {
		int i;
		for (i = CPUIDLE_DRIVER_STATE_START; i < dev->state_count; i++)
			dev->states[i].power_usage = -1 - i;
	}

	per_cpu(cpuidle_devices, dev->cpu) = dev;
	list_add(&dev->device_list, &cpuidle_detected_devices);
	if ((ret = cpuidle_add_sysfs(sys_dev))) {
		module_put(cpuidle_driver->owner);
		return ret;
	}

	dev->registered = 1;
	return 0;
}

/**
 * cpuidle_register_device - registers a CPU's idle PM feature
 * @dev: the cpu
 */
int cpuidle_register_device(struct cpuidle_device *dev)
{
	int ret;

	mutex_lock(&cpuidle_lock);

	if ((ret = __cpuidle_register_device(dev))) {
		mutex_unlock(&cpuidle_lock);
		return ret;
	}

	cpuidle_enable_device(dev);
	cpuidle_install_idle_handler();

	mutex_unlock(&cpuidle_lock);

	return 0;

}

EXPORT_SYMBOL_GPL(cpuidle_register_device);

/**
 * cpuidle_unregister_device - unregisters a CPU's idle PM feature
 * @dev: the cpu
 */
void cpuidle_unregister_device(struct cpuidle_device *dev)
{
	struct sys_device *sys_dev = get_cpu_sysdev((unsigned long)dev->cpu);
	struct cpuidle_driver *cpuidle_driver = cpuidle_get_driver();

	if (dev->registered == 0)
		return;

	cpuidle_pause_and_lock();

	cpuidle_disable_device(dev);

	cpuidle_remove_sysfs(sys_dev);
	list_del(&dev->device_list);
	wait_for_completion(&dev->kobj_unregister);
	per_cpu(cpuidle_devices, dev->cpu) = NULL;

	cpuidle_resume_and_unlock();

	module_put(cpuidle_driver->owner);
}

EXPORT_SYMBOL_GPL(cpuidle_unregister_device);

#ifdef CONFIG_SMP

static void smp_callback(void *v)
{
	/* we already woke the CPU up, nothing more to do */
}

/*
 * This function gets called when a part of the kernel has a new latency
 * requirement.  This means we need to get all processors out of their C-state,
 * and then recalculate a new suitable C-state. Just do a cross-cpu IPI; that
 * wakes them all right up.
 */
static int cpuidle_latency_notify(struct notifier_block *b,
		unsigned long l, void *v)
{
	smp_call_function(smp_callback, NULL, 1);
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

	pm_idle_old = pm_idle;

	ret = cpuidle_add_class_sysfs(&cpu_sysdev_class);
	if (ret)
		return ret;

	latency_notifier_init(&cpuidle_latency_notifier);

	return 0;
}

core_initcall(cpuidle_init);
