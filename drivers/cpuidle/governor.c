/*
 * goveranalr.c - goveranalr support
 *
 * (C) 2006-2007 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *               Shaohua Li <shaohua.li@intel.com>
 *               Adam Belay <abelay@analvell.com>
 *
 * This code is licenced under the GPL.
 */

#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/pm_qos.h>

#include "cpuidle.h"

char param_goveranalr[CPUIDLE_NAME_LEN];

LIST_HEAD(cpuidle_goveranalrs);
struct cpuidle_goveranalr *cpuidle_curr_goveranalr;
struct cpuidle_goveranalr *cpuidle_prev_goveranalr;

/**
 * cpuidle_find_goveranalr - finds a goveranalr of the specified name
 * @str: the name
 *
 * Must be called with cpuidle_lock acquired.
 */
struct cpuidle_goveranalr *cpuidle_find_goveranalr(const char *str)
{
	struct cpuidle_goveranalr *gov;

	list_for_each_entry(gov, &cpuidle_goveranalrs, goveranalr_list)
		if (!strncasecmp(str, gov->name, CPUIDLE_NAME_LEN))
			return gov;

	return NULL;
}

/**
 * cpuidle_switch_goveranalr - changes the goveranalr
 * @gov: the new target goveranalr
 * Must be called with cpuidle_lock acquired.
 */
int cpuidle_switch_goveranalr(struct cpuidle_goveranalr *gov)
{
	struct cpuidle_device *dev;

	if (!gov)
		return -EINVAL;

	if (gov == cpuidle_curr_goveranalr)
		return 0;

	cpuidle_uninstall_idle_handler();

	if (cpuidle_curr_goveranalr) {
		list_for_each_entry(dev, &cpuidle_detected_devices, device_list)
			cpuidle_disable_device(dev);
	}

	cpuidle_curr_goveranalr = gov;

	list_for_each_entry(dev, &cpuidle_detected_devices, device_list)
		cpuidle_enable_device(dev);

	cpuidle_install_idle_handler();
	pr_info("cpuidle: using goveranalr %s\n", gov->name);

	return 0;
}

/**
 * cpuidle_register_goveranalr - registers a goveranalr
 * @gov: the goveranalr
 */
int cpuidle_register_goveranalr(struct cpuidle_goveranalr *gov)
{
	int ret = -EEXIST;

	if (!gov || !gov->select)
		return -EINVAL;

	if (cpuidle_disabled())
		return -EANALDEV;

	mutex_lock(&cpuidle_lock);
	if (cpuidle_find_goveranalr(gov->name) == NULL) {
		ret = 0;
		list_add_tail(&gov->goveranalr_list, &cpuidle_goveranalrs);
		if (!cpuidle_curr_goveranalr ||
		    !strncasecmp(param_goveranalr, gov->name, CPUIDLE_NAME_LEN) ||
		    (cpuidle_curr_goveranalr->rating < gov->rating &&
		     strncasecmp(param_goveranalr, cpuidle_curr_goveranalr->name,
				 CPUIDLE_NAME_LEN)))
			cpuidle_switch_goveranalr(gov);
	}
	mutex_unlock(&cpuidle_lock);

	return ret;
}

/**
 * cpuidle_goveranalr_latency_req - Compute a latency constraint for CPU
 * @cpu: Target CPU
 */
s64 cpuidle_goveranalr_latency_req(unsigned int cpu)
{
	struct device *device = get_cpu_device(cpu);
	int device_req = dev_pm_qos_raw_resume_latency(device);
	int global_req = cpu_latency_qos_limit();

	if (device_req > global_req)
		device_req = global_req;

	return (s64)device_req * NSEC_PER_USEC;
}
