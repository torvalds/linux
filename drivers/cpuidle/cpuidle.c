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
#include <linux/latency.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>

#include "cpuidle.h"

DEFINE_PER_CPU(struct cpuidle_device *, cpuidle_devices);

DEFINE_MUTEX(cpuidle_lock);
LIST_HEAD(cpuidle_detected_devices);
static void (*pm_idle_old)(void);

static int enabled_devices;

/**
 * cpuidle_idle_call - the main idle loop
 *
 * NOTE: no locks or semaphores should be used here
 */
static void cpuidle_idle_call(void)
{
	struct cpuidle_device *dev = __get_cpu_var(cpuidle_devices);
	struct cpuidle_state *target_state;
	int next_state;

	/* check if the device is ready */
	if (!dev || !dev->enabled) {
		if (pm_idle_old)
			pm_idle_old();
		else
			local_irq_enable();
		return;
	}

	/* ask the governor for the next state */
	next_state = cpuidle_curr_governor->select(dev);
	if (need_resched())
		return;
	target_state = &dev->states[next_state];

	/* enter the state and update stats */
	dev->last_residency = target_state->enter(dev, target_state);
	dev->last_state = target_state;
	target_state->time += dev->last_residency;
	target_state->usage++;

	/* give the governor an opportunity to reflect on the outcome */
	if (cpuidle_curr_governor->reflect)
		cpuidle_curr_governor->reflect(dev);
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
	if (enabled_devices && (pm_idle != pm_idle_old)) {
		pm_idle = pm_idle_old;
		cpu_idle_wait();
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
	if (!cpuidle_curr_driver || !cpuidle_curr_governor)
		return -EIO;
	if (!dev->state_count)
		return -EINVAL;

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
	if (!cpuidle_curr_driver || !cpuidle_curr_governor)
		return;

	dev->enabled = 0;

	if (cpuidle_curr_governor->disable)
		cpuidle_curr_governor->disable(dev);

	cpuidle_remove_state_sysfs(dev);
	enabled_devices--;
}

EXPORT_SYMBOL_GPL(cpuidle_disable_device);

/**
 * cpuidle_register_device - registers a CPU's idle PM feature
 * @dev: the cpu
 */
int cpuidle_register_device(struct cpuidle_device *dev)
{
	int ret;
	struct sys_device *sys_dev = get_cpu_sysdev((unsigned long)dev->cpu);

	if (!sys_dev)
		return -EINVAL;
	if (!try_module_get(cpuidle_curr_driver->owner))
		return -EINVAL;

	init_completion(&dev->kobj_unregister);

	mutex_lock(&cpuidle_lock);

	per_cpu(cpuidle_devices, dev->cpu) = dev;
	list_add(&dev->device_list, &cpuidle_detected_devices);
	if ((ret = cpuidle_add_sysfs(sys_dev))) {
		mutex_unlock(&cpuidle_lock);
		module_put(cpuidle_curr_driver->owner);
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

	cpuidle_pause_and_lock();

	cpuidle_disable_device(dev);

	cpuidle_remove_sysfs(sys_dev);
	list_del(&dev->device_list);
	wait_for_completion(&dev->kobj_unregister);
	per_cpu(cpuidle_devices, dev->cpu) = NULL;

	cpuidle_resume_and_unlock();

	module_put(cpuidle_curr_driver->owner);
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
	smp_call_function(smp_callback, NULL, 0, 1);
	return NOTIFY_OK;
}

static struct notifier_block cpuidle_latency_notifier = {
	.notifier_call = cpuidle_latency_notify,
};

#define latency_notifier_init(x) do { register_latency_notifier(x); } while (0)

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
