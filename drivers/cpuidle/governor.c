/*
 * governor.c - governor support
 *
 * (C) 2006-2007 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *               Shaohua Li <shaohua.li@intel.com>
 *               Adam Belay <abelay@novell.com>
 *
 * This code is licenced under the GPL.
 */

#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/pm_qos.h>

#include "cpuidle.h"

char param_governor[CPUIDLE_NAME_LEN];

LIST_HEAD(cpuidle_governors);
struct cpuidle_governor *cpuidle_curr_governor;
struct cpuidle_governor *cpuidle_prev_governor;

/**
 * cpuidle_find_governor - finds a governor of the specified name
 * @str: the name
 *
 * Must be called with cpuidle_lock acquired.
 */
struct cpuidle_governor *cpuidle_find_governor(const char *str)
{
	struct cpuidle_governor *gov;

	list_for_each_entry(gov, &cpuidle_governors, governor_list)
		if (!strncasecmp(str, gov->name, CPUIDLE_NAME_LEN))
			return gov;

	return NULL;
}

/**
 * cpuidle_switch_governor - changes the governor
 * @gov: the new target governor
 * Must be called with cpuidle_lock acquired.
 */
int cpuidle_switch_governor(struct cpuidle_governor *gov)
{
	struct cpuidle_device *dev;

	if (!gov)
		return -EINVAL;

	if (gov == cpuidle_curr_governor)
		return 0;

	cpuidle_uninstall_idle_handler();

	if (cpuidle_curr_governor) {
		list_for_each_entry(dev, &cpuidle_detected_devices, device_list)
			cpuidle_disable_device(dev);
	}

	cpuidle_curr_governor = gov;

	if (gov) {
		list_for_each_entry(dev, &cpuidle_detected_devices, device_list)
			cpuidle_enable_device(dev);
		cpuidle_install_idle_handler();
		printk(KERN_INFO "cpuidle: using governor %s\n", gov->name);
	}

	return 0;
}

/**
 * cpuidle_register_governor - registers a governor
 * @gov: the governor
 */
int cpuidle_register_governor(struct cpuidle_governor *gov)
{
	int ret = -EEXIST;

	if (!gov || !gov->select)
		return -EINVAL;

	if (cpuidle_disabled())
		return -ENODEV;

	mutex_lock(&cpuidle_lock);
	if (cpuidle_find_governor(gov->name) == NULL) {
		ret = 0;
		list_add_tail(&gov->governor_list, &cpuidle_governors);
		if (!cpuidle_curr_governor ||
		    !strncasecmp(param_governor, gov->name, CPUIDLE_NAME_LEN) ||
		    (cpuidle_curr_governor->rating < gov->rating &&
		     strncasecmp(param_governor, cpuidle_curr_governor->name,
				 CPUIDLE_NAME_LEN)))
			cpuidle_switch_governor(gov);
	}
	mutex_unlock(&cpuidle_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(cpuidle_register_governor);

/**
 * cpuidle_governor_latency_req - Compute a latency constraint for CPU
 * @cpu: Target CPU
 */
s64 cpuidle_governor_latency_req(unsigned int cpu)
{
	struct device *device = get_cpu_device(cpu);
	int device_req = dev_pm_qos_raw_resume_latency(device);
	int global_req = cpu_latency_qos_limit();

	if (device_req > global_req)
		device_req = global_req;

	return (s64)device_req * NSEC_PER_USEC;
}
EXPORT_SYMBOL_GPL(cpuidle_governor_latency_req);
