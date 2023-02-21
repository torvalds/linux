// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARM/ARM64 generic CPU idle driver.
 *
 * Copyright (C) 2014 ARM Ltd.
 * Author: Lorenzo Pieralisi <lorenzo.pieralisi@arm.com>
 */

#define pr_fmt(fmt) "CPUidle arm: " fmt

#include <linux/cpu_cooling.h>
#include <linux/cpuidle.h>
#include <linux/cpumask.h>
#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <asm/cpuidle.h>

#include "dt_idle_states.h"

/*
 * arm_enter_idle_state - Programs CPU to enter the specified state
 *
 * dev: cpuidle device
 * drv: cpuidle driver
 * idx: state index
 *
 * Called from the CPUidle framework to program the device to the
 * specified target state selected by the governor.
 */
static __cpuidle int arm_enter_idle_state(struct cpuidle_device *dev,
					  struct cpuidle_driver *drv, int idx)
{
	/*
	 * Pass idle state index to arm_cpuidle_suspend which in turn
	 * will call the CPU ops suspend protocol with idle index as a
	 * parameter.
	 */
	return CPU_PM_CPU_IDLE_ENTER(arm_cpuidle_suspend, idx);
}

static struct cpuidle_driver arm_idle_driver __initdata = {
	.name = "arm_idle",
	.owner = THIS_MODULE,
	/*
	 * State at index 0 is standby wfi and considered standard
	 * on all ARM platforms. If in some platforms simple wfi
	 * can't be used as "state 0", DT bindings must be implemented
	 * to work around this issue and allow installing a special
	 * handler for idle state index 0.
	 */
	.states[0] = {
		.enter                  = arm_enter_idle_state,
		.exit_latency           = 1,
		.target_residency       = 1,
		.power_usage		= UINT_MAX,
		.name                   = "WFI",
		.desc                   = "ARM WFI",
	}
};

static const struct of_device_id arm_idle_state_match[] __initconst = {
	{ .compatible = "arm,idle-state",
	  .data = arm_enter_idle_state },
	{ },
};

/*
 * arm_idle_init_cpu
 *
 * Registers the arm specific cpuidle driver with the cpuidle
 * framework. It relies on core code to parse the idle states
 * and initialize them using driver data structures accordingly.
 */
static int __init arm_idle_init_cpu(int cpu)
{
	int ret;
	struct cpuidle_driver *drv;

	drv = kmemdup(&arm_idle_driver, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	drv->cpumask = (struct cpumask *)cpumask_of(cpu);

	/*
	 * Initialize idle states data, starting at index 1.  This
	 * driver is DT only, if no DT idle states are detected (ret
	 * == 0) let the driver initialization fail accordingly since
	 * there is no reason to initialize the idle driver if only
	 * wfi is supported.
	 */
	ret = dt_init_idle_driver(drv, arm_idle_state_match, 1);
	if (ret <= 0) {
		ret = ret ? : -ENODEV;
		goto out_kfree_drv;
	}

	/*
	 * Call arch CPU operations in order to initialize
	 * idle states suspend back-end specific data
	 */
	ret = arm_cpuidle_init(cpu);

	/*
	 * Allow the initialization to continue for other CPUs, if the
	 * reported failure is a HW misconfiguration/breakage (-ENXIO).
	 *
	 * Some platforms do not support idle operations
	 * (arm_cpuidle_init() returning -EOPNOTSUPP), we should
	 * not flag this case as an error, it is a valid
	 * configuration.
	 */
	if (ret) {
		if (ret != -EOPNOTSUPP)
			pr_err("CPU %d failed to init idle CPU ops\n", cpu);
		ret = ret == -ENXIO ? 0 : ret;
		goto out_kfree_drv;
	}

	ret = cpuidle_register(drv, NULL);
	if (ret)
		goto out_kfree_drv;

	cpuidle_cooling_register(drv);

	return 0;

out_kfree_drv:
	kfree(drv);
	return ret;
}

/*
 * arm_idle_init - Initializes arm cpuidle driver
 *
 * Initializes arm cpuidle driver for all CPUs, if any CPU fails
 * to register cpuidle driver then rollback to cancel all CPUs
 * registeration.
 */
static int __init arm_idle_init(void)
{
	int cpu, ret;
	struct cpuidle_driver *drv;
	struct cpuidle_device *dev;

	for_each_possible_cpu(cpu) {
		ret = arm_idle_init_cpu(cpu);
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
device_initcall(arm_idle_init);
