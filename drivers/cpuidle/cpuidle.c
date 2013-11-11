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
#include <linux/notifier.h>
#include <linux/pm_qos.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/module.h>
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

static int __cpuidle_register_device(struct cpuidle_device *dev);

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
	for (i = drv->state_count - 1; i >= CPUIDLE_DRIVER_STATE_START; i--)
		if (drv->states[i].enter_dead)
			return drv->states[i].enter_dead(dev, i);

	return -ENODEV;
}

/**
 * cpuidle_enter_state - enter the state and update stats
 * @dev: cpuidle device for this cpu
 * @drv: cpuidle driver for this cpu
 * @next_state: index into drv->states of the state to enter
 */
int cpuidle_enter_state(struct cpuidle_device *dev, struct cpuidle_driver *drv,
			int index)
{
	int entered_state;

	struct cpuidle_state *target_state = &drv->states[index];
	ktime_t time_start, time_end;
	s64 diff;

	time_start = ktime_get();

	entered_state = target_state->enter(dev, drv, index);

	time_end = ktime_get();

	local_irq_enable();

	diff = ktime_to_us(ktime_sub(time_end, time_start));
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
 * cpuidle_idle_call - the main idle loop
 *
 * NOTE: no locks or semaphores should be used here
 * return non-zero on failure
 */
int cpuidle_idle_call(void)
{
	struct cpuidle_device *dev = __this_cpu_read(cpuidle_devices);
	struct cpuidle_driver *drv;
	int next_state, entered_state;

	if (off)
		return -ENODEV;

	if (!initialized)
		return -ENODEV;

	/* check if the device is ready */
	if (!dev || !dev->enabled)
		return -EBUSY;

	drv = cpuidle_get_cpu_driver(dev);

	/* ask the governor for the next state */
	next_state = cpuidle_curr_governor->select(drv, dev);
	if (need_resched()) {
		dev->last_residency = 0;
		/* give the governor an opportunity to reflect on the outcome */
		if (cpuidle_curr_governor->reflect)
			cpuidle_curr_governor->reflect(dev, next_state);
		local_irq_enable();
		return 0;
	}

	trace_cpu_idle_rcuidle(next_state, dev->cpu);

	if (drv->states[next_state].flags & CPUIDLE_FLAG_TIMER_STOP)
		clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER,
				   &dev->cpu);

	if (cpuidle_state_is_coupled(dev, drv, next_state))
		entered_state = cpuidle_enter_state_coupled(dev, drv,
							    next_state);
	else
		entered_state = cpuidle_enter_state(dev, drv, next_state);

	if (drv->states[next_state].flags & CPUIDLE_FLAG_TIMER_STOP)
		clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT,
				   &dev->cpu);

	trace_cpu_idle_rcuidle(PWR_EVENT_EXIT, dev->cpu);

	/* give the governor an opportunity to reflect on the outcome */
	if (cpuidle_curr_governor->reflect)
		cpuidle_curr_governor->reflect(dev, entered_state);

	return 0;
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
		kick_all_cpus_sync();
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

#ifdef CONFIG_ARCH_HAS_CPU_RELAX
static int poll_idle(struct cpuidle_device *dev,
		struct cpuidle_driver *drv, int index)
{
	ktime_t	t1, t2;
	s64 diff;

	t1 = ktime_get();
	local_irq_enable();
	while (!need_resched())
		cpu_relax();

	t2 = ktime_get();
	diff = ktime_to_us(ktime_sub(t2, t1));
	if (diff > INT_MAX)
		diff = INT_MAX;

	dev->last_residency = (int) diff;

	return index;
}

static void poll_idle_init(struct cpuidle_driver *drv)
{
	struct cpuidle_state *state = &drv->states[0];

	snprintf(state->name, CPUIDLE_NAME_LEN, "POLL");
	snprintf(state->desc, CPUIDLE_DESC_LEN, "CPUIDLE CORE POLL IDLE");
	state->exit_latency = 0;
	state->target_residency = 0;
	state->power_usage = -1;
	state->flags = 0;
	state->enter = poll_idle;
	state->disabled = false;
}
#else
static void poll_idle_init(struct cpuidle_driver *drv) {}
#endif /* CONFIG_ARCH_HAS_CPU_RELAX */

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
	struct cpuidle_driver *drv;

	if (!dev)
		return -EINVAL;

	if (dev->enabled)
		return 0;

	drv = cpuidle_get_cpu_driver(dev);

	if (!drv || !cpuidle_curr_governor)
		return -EIO;

	if (!dev->state_count)
		dev->state_count = drv->state_count;

	if (dev->registered == 0) {
		ret = __cpuidle_register_device(dev);
		if (ret)
			return ret;
	}

	poll_idle_init(drv);

	ret = cpuidle_add_device_sysfs(dev);
	if (ret)
		return ret;

	if (cpuidle_curr_governor->enable &&
	    (ret = cpuidle_curr_governor->enable(drv, dev)))
		goto fail_sysfs;

	for (i = 0; i < dev->state_count; i++) {
		dev->states_usage[i].usage = 0;
		dev->states_usage[i].time = 0;
	}
	dev->last_residency = 0;

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
	ret = cpuidle_add_sysfs(dev);
	if (ret)
		goto err_sysfs;

	ret = cpuidle_coupled_register_device(dev);
	if (ret)
		goto err_coupled;

	dev->registered = 1;
	return 0;

err_coupled:
	cpuidle_remove_sysfs(dev);
err_sysfs:
	list_del(&dev->device_list);
	per_cpu(cpuidle_devices, dev->cpu) = NULL;
	module_put(drv->owner);
	return ret;
}

/**
 * cpuidle_register_device - registers a CPU's idle PM feature
 * @dev: the cpu
 */
int cpuidle_register_device(struct cpuidle_device *dev)
{
	int ret;

	if (!dev)
		return -EINVAL;

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
	struct cpuidle_driver *drv = cpuidle_get_cpu_driver(dev);

	if (dev->registered == 0)
		return;

	cpuidle_pause_and_lock();

	cpuidle_disable_device(dev);

	cpuidle_remove_sysfs(dev);
	list_del(&dev->device_list);
	per_cpu(cpuidle_devices, dev->cpu) = NULL;

	cpuidle_coupled_unregister_device(dev);

	cpuidle_resume_and_unlock();

	module_put(drv->owner);
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

	for_each_possible_cpu(cpu) {
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

	for_each_possible_cpu(cpu) {
		device = &per_cpu(cpuidle_dev, cpu);
		device->cpu = cpu;

#ifdef CONFIG_ARCH_NEEDS_CPU_IDLE_COUPLED
		/*
		 * On multiplatform for ARM, the coupled idle states could
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
