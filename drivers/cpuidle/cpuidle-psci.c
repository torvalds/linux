// SPDX-License-Identifier: GPL-2.0-only
/*
 * PSCI CPU idle driver.
 *
 * Copyright (C) 2019 ARM Ltd.
 * Author: Lorenzo Pieralisi <lorenzo.pieralisi@arm.com>
 */

#define pr_fmt(fmt) "CPUidle PSCI: " fmt

#include <linux/cpuidle.h>
#include <linux/cpumask.h>
#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/psci.h>
#include <linux/slab.h>

#include <asm/cpuidle.h>

#include "dt_idle_states.h"

static int psci_enter_idle_state(struct cpuidle_device *dev,
				struct cpuidle_driver *drv, int idx)
{
	return CPU_PM_CPU_IDLE_ENTER(psci_cpu_suspend_enter, idx);
}

static struct cpuidle_driver psci_idle_driver __initdata = {
	.name = "psci_idle",
	.owner = THIS_MODULE,
	/*
	 * PSCI idle states relies on architectural WFI to
	 * be represented as state index 0.
	 */
	.states[0] = {
		.enter                  = psci_enter_idle_state,
		.exit_latency           = 1,
		.target_residency       = 1,
		.power_usage		= UINT_MAX,
		.name                   = "WFI",
		.desc                   = "ARM WFI",
	}
};

static const struct of_device_id psci_idle_state_match[] __initconst = {
	{ .compatible = "arm,idle-state",
	  .data = psci_enter_idle_state },
	{ },
};

static int __init psci_idle_init_cpu(int cpu)
{
	struct cpuidle_driver *drv;
	struct device_node *cpu_node;
	const char *enable_method;
	int ret = 0;

	cpu_node = of_cpu_device_node_get(cpu);
	if (!cpu_node)
		return -ENODEV;

	/*
	 * Check whether the enable-method for the cpu is PSCI, fail
	 * if it is not.
	 */
	enable_method = of_get_property(cpu_node, "enable-method", NULL);
	if (!enable_method || (strcmp(enable_method, "psci")))
		ret = -ENODEV;

	of_node_put(cpu_node);
	if (ret)
		return ret;

	drv = kmemdup(&psci_idle_driver, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	drv->cpumask = (struct cpumask *)cpumask_of(cpu);

	/*
	 * Initialize idle states data, starting at index 1, since
	 * by default idle state 0 is the quiescent state reached
	 * by the cpu by executing the wfi instruction.
	 *
	 * If no DT idle states are detected (ret == 0) let the driver
	 * initialization fail accordingly since there is no reason to
	 * initialize the idle driver if only wfi is supported, the
	 * default archictectural back-end already executes wfi
	 * on idle entry.
	 */
	ret = dt_init_idle_driver(drv, psci_idle_state_match, 1);
	if (ret <= 0) {
		ret = ret ? : -ENODEV;
		goto out_kfree_drv;
	}

	/*
	 * Initialize PSCI idle states.
	 */
	ret = psci_cpu_init_idle(cpu);
	if (ret) {
		pr_err("CPU %d failed to PSCI idle\n", cpu);
		goto out_kfree_drv;
	}

	ret = cpuidle_register(drv, NULL);
	if (ret)
		goto out_kfree_drv;

	return 0;

out_kfree_drv:
	kfree(drv);
	return ret;
}

/*
 * psci_idle_init - Initializes PSCI cpuidle driver
 *
 * Initializes PSCI cpuidle driver for all CPUs, if any CPU fails
 * to register cpuidle driver then rollback to cancel all CPUs
 * registration.
 */
static int __init psci_idle_init(void)
{
	int cpu, ret;
	struct cpuidle_driver *drv;
	struct cpuidle_device *dev;

	for_each_possible_cpu(cpu) {
		ret = psci_idle_init_cpu(cpu);
		if (ret)
			goto out_fail;
	}

	return 0;

out_fail:
	while (--cpu >= 0) {
		dev = per_cpu(cpuidle_devices, cpu);
		drv = cpuidle_get_cpu_driver(dev);
		cpuidle_unregister(drv);
		kfree(drv);
	}

	return ret;
}
device_initcall(psci_idle_init);
