/*
 * Copyright (C) 2014 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/cpu_pm.h>
#include <linux/cpuidle.h>
#include <linux/init.h>

#include <asm/idle.h>
#include <asm/pm-cps.h>

/* Enumeration of the various idle states this driver may enter */
enum cps_idle_state {
	STATE_WAIT = 0,		/* MIPS wait instruction, coherent */
	STATE_NC_WAIT,		/* MIPS wait instruction, non-coherent */
	STATE_CLOCK_GATED,	/* Core clock gated */
	STATE_POWER_GATED,	/* Core power gated */
	STATE_COUNT
};

static int cps_nc_enter(struct cpuidle_device *dev,
			struct cpuidle_driver *drv, int index)
{
	enum cps_pm_state pm_state;
	int err;

	/*
	 * At least one core must remain powered up & clocked in order for the
	 * system to have any hope of functioning.
	 *
	 * TODO: don't treat core 0 specially, just prevent the final core
	 * TODO: remap interrupt affinity temporarily
	 */
	if (!cpu_core(&cpu_data[dev->cpu]) && (index > STATE_NC_WAIT))
		index = STATE_NC_WAIT;

	/* Select the appropriate cps_pm_state */
	switch (index) {
	case STATE_NC_WAIT:
		pm_state = CPS_PM_NC_WAIT;
		break;
	case STATE_CLOCK_GATED:
		pm_state = CPS_PM_CLOCK_GATED;
		break;
	case STATE_POWER_GATED:
		pm_state = CPS_PM_POWER_GATED;
		break;
	default:
		BUG();
		return -EINVAL;
	}

	/* Notify listeners the CPU is about to power down */
	if ((pm_state == CPS_PM_POWER_GATED) && cpu_pm_enter())
		return -EINTR;

	/* Enter that state */
	err = cps_pm_enter_state(pm_state);

	/* Notify listeners the CPU is back up */
	if (pm_state == CPS_PM_POWER_GATED)
		cpu_pm_exit();

	return err ?: index;
}

static struct cpuidle_driver cps_driver = {
	.name			= "cpc_cpuidle",
	.owner			= THIS_MODULE,
	.states = {
		[STATE_WAIT] = MIPS_CPUIDLE_WAIT_STATE,
		[STATE_NC_WAIT] = {
			.enter	= cps_nc_enter,
			.exit_latency		= 200,
			.target_residency	= 450,
			.name	= "nc-wait",
			.desc	= "non-coherent MIPS wait",
		},
		[STATE_CLOCK_GATED] = {
			.enter	= cps_nc_enter,
			.exit_latency		= 300,
			.target_residency	= 700,
			.flags	= CPUIDLE_FLAG_TIMER_STOP,
			.name	= "clock-gated",
			.desc	= "core clock gated",
		},
		[STATE_POWER_GATED] = {
			.enter	= cps_nc_enter,
			.exit_latency		= 600,
			.target_residency	= 1000,
			.flags	= CPUIDLE_FLAG_TIMER_STOP,
			.name	= "power-gated",
			.desc	= "core power gated",
		},
	},
	.state_count		= STATE_COUNT,
	.safe_state_index	= 0,
};

static void __init cps_cpuidle_unregister(void)
{
	int cpu;
	struct cpuidle_device *device;

	for_each_possible_cpu(cpu) {
		device = &per_cpu(cpuidle_dev, cpu);
		cpuidle_unregister_device(device);
	}

	cpuidle_unregister_driver(&cps_driver);
}

static int __init cps_cpuidle_init(void)
{
	int err, cpu, i;
	struct cpuidle_device *device;

	/* Detect supported states */
	if (!cps_pm_support_state(CPS_PM_POWER_GATED))
		cps_driver.state_count = STATE_CLOCK_GATED + 1;
	if (!cps_pm_support_state(CPS_PM_CLOCK_GATED))
		cps_driver.state_count = STATE_NC_WAIT + 1;
	if (!cps_pm_support_state(CPS_PM_NC_WAIT))
		cps_driver.state_count = STATE_WAIT + 1;

	/* Inform the user if some states are unavailable */
	if (cps_driver.state_count < STATE_COUNT) {
		pr_info("cpuidle-cps: limited to ");
		switch (cps_driver.state_count - 1) {
		case STATE_WAIT:
			pr_cont("coherent wait\n");
			break;
		case STATE_NC_WAIT:
			pr_cont("non-coherent wait\n");
			break;
		case STATE_CLOCK_GATED:
			pr_cont("clock gating\n");
			break;
		}
	}

	/*
	 * Set the coupled flag on the appropriate states if this system
	 * requires it.
	 */
	if (coupled_coherence)
		for (i = STATE_NC_WAIT; i < cps_driver.state_count; i++)
			cps_driver.states[i].flags |= CPUIDLE_FLAG_COUPLED;

	err = cpuidle_register_driver(&cps_driver);
	if (err) {
		pr_err("Failed to register CPS cpuidle driver\n");
		return err;
	}

	for_each_possible_cpu(cpu) {
		device = &per_cpu(cpuidle_dev, cpu);
		device->cpu = cpu;
#ifdef CONFIG_ARCH_NEEDS_CPU_IDLE_COUPLED
		cpumask_copy(&device->coupled_cpus, &cpu_sibling_map[cpu]);
#endif

		err = cpuidle_register_device(device);
		if (err) {
			pr_err("Failed to register CPU%d cpuidle device\n",
			       cpu);
			goto err_out;
		}
	}

	return 0;
err_out:
	cps_cpuidle_unregister();
	return err;
}
device_initcall(cps_cpuidle_init);
