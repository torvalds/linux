// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2019 Linaro Limited.
 *
 *  Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 */
#define pr_fmt(fmt) "cpuidle cooling: " fmt

#include <linux/cpu_cooling.h>
#include <linux/cpuidle.h>
#include <linux/err.h>
#include <linux/idle_inject.h>
#include <linux/idr.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/thermal.h>

/**
 * struct cpuidle_cooling_device - data for the idle cooling device
 * @ii_dev: an atomic to keep track of the last task exiting the idle cycle
 * @state: a normalized integer giving the state of the cooling device
 */
struct cpuidle_cooling_device {
	struct idle_inject_device *ii_dev;
	unsigned long state;
};

static DEFINE_IDA(cpuidle_ida);

/**
 * cpuidle_cooling_runtime - Running time computation
 * @idle_duration_us: the idle cooling device
 * @state: a percentile based number
 *
 * The running duration is computed from the idle injection duration
 * which is fixed. If we reach 100% of idle injection ratio, that
 * means the running duration is zero. If we have a 50% ratio
 * injection, that means we have equal duration for idle and for
 * running duration.
 *
 * The formula is deduced as follows:
 *
 *  running = idle x ((100 / ratio) - 1)
 *
 * For precision purpose for integer math, we use the following:
 *
 *  running = (idle x 100) / ratio - idle
 *
 * For example, if we have an injected duration of 50%, then we end up
 * with 10ms of idle injection and 10ms of running duration.
 *
 * Return: An unsigned int for a usec based runtime duration.
 */
static unsigned int cpuidle_cooling_runtime(unsigned int idle_duration_us,
					    unsigned long state)
{
	if (!state)
		return 0;

	return ((idle_duration_us * 100) / state) - idle_duration_us;
}

/**
 * cpuidle_cooling_get_max_state - Get the maximum state
 * @cdev  : the thermal cooling device
 * @state : a pointer to the state variable to be filled
 *
 * The function always returns 100 as the injection ratio. It is
 * percentile based for consistency accross different platforms.
 *
 * Return: The function can not fail, it is always zero
 */
static int cpuidle_cooling_get_max_state(struct thermal_cooling_device *cdev,
					 unsigned long *state)
{
	/*
	 * Depending on the configuration or the hardware, the running
	 * cycle and the idle cycle could be different. We want to
	 * unify that to an 0..100 interval, so the set state
	 * interface will be the same whatever the platform is.
	 *
	 * The state 100% will make the cluster 100% ... idle. A 0%
	 * injection ratio means no idle injection at all and 50%
	 * means for 10ms of idle injection, we have 10ms of running
	 * time.
	 */
	*state = 100;

	return 0;
}

/**
 * cpuidle_cooling_get_cur_state - Get the current cooling state
 * @cdev: the thermal cooling device
 * @state: a pointer to the state
 *
 * The function just copies  the state value from the private thermal
 * cooling device structure, the mapping is 1 <-> 1.
 *
 * Return: The function can not fail, it is always zero
 */
static int cpuidle_cooling_get_cur_state(struct thermal_cooling_device *cdev,
					 unsigned long *state)
{
	struct cpuidle_cooling_device *idle_cdev = cdev->devdata;

	*state = idle_cdev->state;

	return 0;
}

/**
 * cpuidle_cooling_set_cur_state - Set the current cooling state
 * @cdev: the thermal cooling device
 * @state: the target state
 *
 * The function checks first if we are initiating the mitigation which
 * in turn wakes up all the idle injection tasks belonging to the idle
 * cooling device. In any case, it updates the internal state for the
 * cooling device.
 *
 * Return: The function can not fail, it is always zero
 */
static int cpuidle_cooling_set_cur_state(struct thermal_cooling_device *cdev,
					 unsigned long state)
{
	struct cpuidle_cooling_device *idle_cdev = cdev->devdata;
	struct idle_inject_device *ii_dev = idle_cdev->ii_dev;
	unsigned long current_state = idle_cdev->state;
	unsigned int runtime_us, idle_duration_us;

	idle_cdev->state = state;

	idle_inject_get_duration(ii_dev, &runtime_us, &idle_duration_us);

	runtime_us = cpuidle_cooling_runtime(idle_duration_us, state);

	idle_inject_set_duration(ii_dev, runtime_us, idle_duration_us);

	if (current_state == 0 && state > 0) {
		idle_inject_start(ii_dev);
	} else if (current_state > 0 && !state)  {
		idle_inject_stop(ii_dev);
	}

	return 0;
}

/**
 * cpuidle_cooling_ops - thermal cooling device ops
 */
static struct thermal_cooling_device_ops cpuidle_cooling_ops = {
	.get_max_state = cpuidle_cooling_get_max_state,
	.get_cur_state = cpuidle_cooling_get_cur_state,
	.set_cur_state = cpuidle_cooling_set_cur_state,
};

/**
 * __cpuidle_cooling_register: register the cooling device
 * @drv: a cpuidle driver structure pointer
 * @np: a device node structure pointer used for the thermal binding
 *
 * This function is in charge of allocating the cpuidle cooling device
 * structure, the idle injection, initialize them and register the
 * cooling device to the thermal framework.
 *
 * Return: zero on success, a negative value returned by one of the
 * underlying subsystem in case of error
 */
static int __cpuidle_cooling_register(struct device_node *np,
				      struct cpuidle_driver *drv)
{
	struct idle_inject_device *ii_dev;
	struct cpuidle_cooling_device *idle_cdev;
	struct thermal_cooling_device *cdev;
	unsigned int idle_duration_us = TICK_USEC;
	unsigned int latency_us = UINT_MAX;
	char dev_name[THERMAL_NAME_LENGTH];
	int id, ret;

	idle_cdev = kzalloc(sizeof(*idle_cdev), GFP_KERNEL);
	if (!idle_cdev) {
		ret = -ENOMEM;
		goto out;
	}

	id = ida_simple_get(&cpuidle_ida, 0, 0, GFP_KERNEL);
	if (id < 0) {
		ret = id;
		goto out_kfree;
	}

	ii_dev = idle_inject_register(drv->cpumask);
	if (!ii_dev) {
		ret = -EINVAL;
		goto out_id;
	}

	of_property_read_u32(np, "duration-us", &idle_duration_us);
	of_property_read_u32(np, "exit-latency-us", &latency_us);

	idle_inject_set_duration(ii_dev, TICK_USEC, idle_duration_us);
	idle_inject_set_latency(ii_dev, latency_us);

	idle_cdev->ii_dev = ii_dev;

	snprintf(dev_name, sizeof(dev_name), "thermal-idle-%d", id);

	cdev = thermal_of_cooling_device_register(np, dev_name, idle_cdev,
						  &cpuidle_cooling_ops);
	if (IS_ERR(cdev)) {
		ret = PTR_ERR(cdev);
		goto out_unregister;
	}

	pr_debug("%s: Idle injection set with idle duration=%u, latency=%u\n",
		 dev_name, idle_duration_us, latency_us);

	return 0;

out_unregister:
	idle_inject_unregister(ii_dev);
out_id:
	ida_simple_remove(&cpuidle_ida, id);
out_kfree:
	kfree(idle_cdev);
out:
	return ret;
}

/**
 * cpuidle_cooling_register - Idle cooling device initialization function
 * @drv: a cpuidle driver structure pointer
 *
 * This function is in charge of creating a cooling device per cpuidle
 * driver and register it to the thermal framework.
 *
 * Return: zero on success, or negative value corresponding to the
 * error detected in the underlying subsystems.
 */
void cpuidle_cooling_register(struct cpuidle_driver *drv)
{
	struct device_node *cooling_node;
	struct device_node *cpu_node;
	int cpu, ret;

	for_each_cpu(cpu, drv->cpumask) {

		cpu_node = of_cpu_device_node_get(cpu);

		cooling_node = of_get_child_by_name(cpu_node, "thermal-idle");

		of_node_put(cpu_node);

		if (!cooling_node) {
			pr_debug("'thermal-idle' node not found for cpu%d\n", cpu);
			continue;
		}

		ret = __cpuidle_cooling_register(cooling_node, drv);

		of_node_put(cooling_node);

		if (ret) {
			pr_err("Failed to register the cpuidle cooling device" \
			       "for cpu%d: %d\n", cpu, ret);
			break;
		}
	}
}
