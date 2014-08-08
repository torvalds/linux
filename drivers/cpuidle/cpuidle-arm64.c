/*
 * ARM64 generic CPU idle driver.
 *
 * Copyright (C) 2014 ARM Ltd.
 * Author: Lorenzo Pieralisi <lorenzo.pieralisi@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpuidle.h>
#include <linux/cpumask.h>
#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>

#include <asm/psci.h>
#include <asm/suspend.h>

#include "of_idle_states.h"

typedef int (*suspend_init_fn)(struct cpuidle_driver *,
			       struct device_node *[]);

struct cpu_suspend_ops {
	const char *id;
	suspend_init_fn init_fn;
};

static const struct cpu_suspend_ops suspend_operations[] __initconst = {
	{"arm,psci", psci_dt_register_idle_states},
	{}
};

static __init const struct cpu_suspend_ops *get_suspend_ops(const char *str)
{
	int i;

	if (!str)
		return NULL;

	for (i = 0; suspend_operations[i].id; i++)
		if (!strcmp(suspend_operations[i].id, str))
			return &suspend_operations[i];

	return NULL;
}

/*
 * arm_enter_idle_state - Programs CPU to enter the specified state
 *
 * @dev: cpuidle device
 * @drv: cpuidle driver
 * @idx: state index
 *
 * Called from the CPUidle framework to program the device to the
 * specified target state selected by the governor.
 */
static int arm_enter_idle_state(struct cpuidle_device *dev,
				struct cpuidle_driver *drv, int idx)
{
	int ret;

	if (!idx) {
		cpu_do_idle();
		return idx;
	}

	cpu_pm_enter();
	/*
	 * Pass idle state index to cpu_suspend which in turn will call
	 * the CPU ops suspend protocol with idle index as a parameter.
	 *
	 * Some states would not require context to be saved and flushed
	 * to DRAM, so calling cpu_suspend would not be stricly necessary.
	 * When power domains specifications for ARM CPUs are finalized then
	 * this code can be optimized to prevent saving registers if not
	 * needed.
	 */
	ret = cpu_suspend(idx);

	cpu_pm_exit();

	return ret ? -1 : idx;
}

struct cpuidle_driver arm64_idle_driver = {
	.name = "arm64_idle",
	.owner = THIS_MODULE,
};

static struct device_node *state_nodes[CPUIDLE_STATE_MAX] __initdata;

/*
 * arm64_idle_init
 *
 * Registers the arm64 specific cpuidle driver with the cpuidle
 * framework. It relies on core code to parse the idle states
 * and initialize them using driver data structures accordingly.
 */
static int __init arm64_idle_init(void)
{
	int i, ret;
	const char *entry_method;
	struct device_node *idle_states_node;
	const struct cpu_suspend_ops *suspend_init;
	struct cpuidle_driver *drv = &arm64_idle_driver;

	idle_states_node = of_find_node_by_path("/cpus/idle-states");
	if (!idle_states_node)
		return -ENOENT;

	if (of_property_read_string(idle_states_node, "entry-method",
				    &entry_method)) {
		pr_warn(" * %s missing entry-method property\n",
			    idle_states_node->full_name);
		of_node_put(idle_states_node);
		return -EOPNOTSUPP;
	}

	suspend_init = get_suspend_ops(entry_method);
	if (!suspend_init) {
		pr_warn("Missing suspend initializer\n");
		of_node_put(idle_states_node);
		return -EOPNOTSUPP;
	}

	/*
	 * State at index 0 is standby wfi and considered standard
	 * on all ARM platforms. If in some platforms simple wfi
	 * can't be used as "state 0", DT bindings must be implemented
	 * to work around this issue and allow installing a special
	 * handler for idle state index 0.
	 */
	drv->states[0].exit_latency = 1;
	drv->states[0].target_residency = 1;
	drv->states[0].flags = CPUIDLE_FLAG_TIME_VALID;
	strncpy(drv->states[0].name, "ARM WFI", CPUIDLE_NAME_LEN);
	strncpy(drv->states[0].desc, "ARM WFI", CPUIDLE_DESC_LEN);

	drv->cpumask = (struct cpumask *) cpu_possible_mask;
	/*
	 * Start at index 1, request idle state nodes to be filled
	 */
	ret = of_init_idle_driver(drv, state_nodes, 1, true);
	if (ret)
		return ret;

	if (suspend_init->init_fn(drv, state_nodes))
		return -EOPNOTSUPP;

	for (i = 0; i < drv->state_count; i++)
		drv->states[i].enter = arm_enter_idle_state;

	return cpuidle_register(drv, NULL);
}
device_initcall(arm64_idle_init);
