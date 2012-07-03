/*
 * OMAP4 CPU idle Routines
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Santosh Shilimkar <santosh.shilimkar@ti.com>
 * Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/sched.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/export.h>
#include <linux/clockchips.h>

#include <asm/proc-fns.h>

#include "common.h"
#include "pm.h"
#include "prm.h"

/* Machine specific information */
struct omap4_idle_statedata {
	u32 cpu_state;
	u32 mpu_logic_state;
	u32 mpu_state;
};

static struct omap4_idle_statedata omap4_idle_data[] = {
	{
		.cpu_state = PWRDM_POWER_ON,
		.mpu_state = PWRDM_POWER_ON,
		.mpu_logic_state = PWRDM_POWER_RET,
	},
	{
		.cpu_state = PWRDM_POWER_OFF,
		.mpu_state = PWRDM_POWER_RET,
		.mpu_logic_state = PWRDM_POWER_RET,
	},
	{
		.cpu_state = PWRDM_POWER_OFF,
		.mpu_state = PWRDM_POWER_RET,
		.mpu_logic_state = PWRDM_POWER_OFF,
	},
};

static struct powerdomain *mpu_pd, *cpu0_pd, *cpu1_pd;

/**
 * omap4_enter_idle - Programs OMAP4 to enter the specified state
 * @dev: cpuidle device
 * @drv: cpuidle driver
 * @index: the index of state to be entered
 *
 * Called from the CPUidle framework to program the device to the
 * specified low power state selected by the governor.
 * Returns the amount of time spent in the low power state.
 */
static int omap4_enter_idle(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{
	struct omap4_idle_statedata *cx = &omap4_idle_data[index];
	u32 cpu1_state;
	int cpu_id = smp_processor_id();

	local_fiq_disable();

	/*
	 * CPU0 has to stay ON (i.e in C1) until CPU1 is OFF state.
	 * This is necessary to honour hardware recommondation
	 * of triggeing all the possible low power modes once CPU1 is
	 * out of coherency and in OFF mode.
	 * Update dev->last_state so that governor stats reflects right
	 * data.
	 */
	cpu1_state = pwrdm_read_pwrst(cpu1_pd);
	if (cpu1_state != PWRDM_POWER_OFF) {
		index = drv->safe_state_index;
		cx = &omap4_idle_data[index];
	}

	if (index > 0)
		clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER, &cpu_id);

	/*
	 * Call idle CPU PM enter notifier chain so that
	 * VFP and per CPU interrupt context is saved.
	 */
	if (cx->cpu_state == PWRDM_POWER_OFF)
		cpu_pm_enter();

	pwrdm_set_logic_retst(mpu_pd, cx->mpu_logic_state);
	omap_set_pwrdm_state(mpu_pd, cx->mpu_state);

	/*
	 * Call idle CPU cluster PM enter notifier chain
	 * to save GIC and wakeupgen context.
	 */
	if ((cx->mpu_state == PWRDM_POWER_RET) &&
		(cx->mpu_logic_state == PWRDM_POWER_OFF))
			cpu_cluster_pm_enter();

	omap4_enter_lowpower(dev->cpu, cx->cpu_state);

	/*
	 * Call idle CPU PM exit notifier chain to restore
	 * VFP and per CPU IRQ context. Only CPU0 state is
	 * considered since CPU1 is managed by CPU hotplug.
	 */
	if (pwrdm_read_prev_pwrst(cpu0_pd) == PWRDM_POWER_OFF)
		cpu_pm_exit();

	/*
	 * Call idle CPU cluster PM exit notifier chain
	 * to restore GIC and wakeupgen context.
	 */
	if (omap4_mpuss_read_prev_context_state())
		cpu_cluster_pm_exit();

	if (index > 0)
		clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT, &cpu_id);

	local_fiq_enable();

	return index;
}

DEFINE_PER_CPU(struct cpuidle_device, omap4_idle_dev);

struct cpuidle_driver omap4_idle_driver = {
	.name				= "omap4_idle",
	.owner				= THIS_MODULE,
	.en_core_tk_irqen		= 1,
	.states = {
		{
			/* C1 - CPU0 ON + CPU1 ON + MPU ON */
			.exit_latency = 2 + 2,
			.target_residency = 5,
			.flags = CPUIDLE_FLAG_TIME_VALID,
			.enter = omap4_enter_idle,
			.name = "C1",
			.desc = "MPUSS ON"
		},
		{
                        /* C2 - CPU0 OFF + CPU1 OFF + MPU CSWR */
			.exit_latency = 328 + 440,
			.target_residency = 960,
			.flags = CPUIDLE_FLAG_TIME_VALID,
			.enter = omap4_enter_idle,
			.name = "C2",
			.desc = "MPUSS CSWR",
		},
		{
			/* C3 - CPU0 OFF + CPU1 OFF + MPU OSWR */
			.exit_latency = 460 + 518,
			.target_residency = 1100,
			.flags = CPUIDLE_FLAG_TIME_VALID,
			.enter = omap4_enter_idle,
			.name = "C3",
			.desc = "MPUSS OSWR",
		},
	},
	.state_count = ARRAY_SIZE(omap4_idle_data),
	.safe_state_index = 0,
};

/**
 * omap4_idle_init - Init routine for OMAP4 idle
 *
 * Registers the OMAP4 specific cpuidle driver to the cpuidle
 * framework with the valid set of states.
 */
int __init omap4_idle_init(void)
{
	struct cpuidle_device *dev;
	unsigned int cpu_id = 0;

	mpu_pd = pwrdm_lookup("mpu_pwrdm");
	cpu0_pd = pwrdm_lookup("cpu0_pwrdm");
	cpu1_pd = pwrdm_lookup("cpu1_pwrdm");
	if ((!mpu_pd) || (!cpu0_pd) || (!cpu1_pd))
		return -ENODEV;

	dev = &per_cpu(omap4_idle_dev, cpu_id);
	dev->cpu = cpu_id;

	cpuidle_register_driver(&omap4_idle_driver);

	if (cpuidle_register_device(dev)) {
		pr_err("%s: CPUidle register device failed\n", __func__);
		return -EIO;
	}

	return 0;
}
