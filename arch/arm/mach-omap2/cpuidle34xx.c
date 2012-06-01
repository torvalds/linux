/*
 * linux/arch/arm/mach-omap2/cpuidle34xx.c
 *
 * OMAP3 CPU IDLE Routines
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Karthik Dasu <karthik-dp@ti.com>
 *
 * Copyright (C) 2006 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 *
 * Copyright (C) 2005 Texas Instruments, Inc.
 * Richard Woodruff <r-woodruff2@ti.com>
 *
 * Based on pm.c for omap2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/sched.h>
#include <linux/cpuidle.h>
#include <linux/export.h>
#include <linux/cpu_pm.h>

#include <plat/prcm.h>
#include <plat/irqs.h>
#include "powerdomain.h"
#include "clockdomain.h"

#include "pm.h"
#include "control.h"
#include "common.h"

#ifdef CONFIG_CPU_IDLE

/* Mach specific information to be recorded in the C-state driver_data */
struct omap3_idle_statedata {
	u32 mpu_state;
	u32 core_state;
};

static struct omap3_idle_statedata omap3_idle_data[] = {
	{
		.mpu_state = PWRDM_POWER_ON,
		.core_state = PWRDM_POWER_ON,
	},
	{
		.mpu_state = PWRDM_POWER_ON,
		.core_state = PWRDM_POWER_ON,
	},
	{
		.mpu_state = PWRDM_POWER_RET,
		.core_state = PWRDM_POWER_ON,
	},
	{
		.mpu_state = PWRDM_POWER_OFF,
		.core_state = PWRDM_POWER_ON,
	},
	{
		.mpu_state = PWRDM_POWER_RET,
		.core_state = PWRDM_POWER_RET,
	},
	{
		.mpu_state = PWRDM_POWER_OFF,
		.core_state = PWRDM_POWER_RET,
	},
	{
		.mpu_state = PWRDM_POWER_OFF,
		.core_state = PWRDM_POWER_OFF,
	},
};

static struct powerdomain *mpu_pd, *core_pd, *per_pd, *cam_pd;

static int __omap3_enter_idle(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	struct omap3_idle_statedata *cx = &omap3_idle_data[index];
	u32 mpu_state = cx->mpu_state, core_state = cx->core_state;

	local_fiq_disable();

	pwrdm_set_next_pwrst(mpu_pd, mpu_state);
	pwrdm_set_next_pwrst(core_pd, core_state);

	if (omap_irq_pending() || need_resched())
		goto return_sleep_time;

	/* Deny idle for C1 */
	if (index == 0) {
		clkdm_deny_idle(mpu_pd->pwrdm_clkdms[0]);
		clkdm_deny_idle(core_pd->pwrdm_clkdms[0]);
	}

	/*
	 * Call idle CPU PM enter notifier chain so that
	 * VFP context is saved.
	 */
	if (mpu_state == PWRDM_POWER_OFF)
		cpu_pm_enter();

	/* Execute ARM wfi */
	omap_sram_idle();

	/*
	 * Call idle CPU PM enter notifier chain to restore
	 * VFP context.
	 */
	if (pwrdm_read_prev_pwrst(mpu_pd) == PWRDM_POWER_OFF)
		cpu_pm_exit();

	/* Re-allow idle for C1 */
	if (index == 0) {
		clkdm_allow_idle(mpu_pd->pwrdm_clkdms[0]);
		clkdm_allow_idle(core_pd->pwrdm_clkdms[0]);
	}

return_sleep_time:

	local_fiq_enable();

	return index;
}

/**
 * omap3_enter_idle - Programs OMAP3 to enter the specified state
 * @dev: cpuidle device
 * @drv: cpuidle driver
 * @index: the index of state to be entered
 *
 * Called from the CPUidle framework to program the device to the
 * specified target state selected by the governor.
 */
static inline int omap3_enter_idle(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	return cpuidle_wrap_enter(dev, drv, index, __omap3_enter_idle);
}

/**
 * next_valid_state - Find next valid C-state
 * @dev: cpuidle device
 * @drv: cpuidle driver
 * @index: Index of currently selected c-state
 *
 * If the state corresponding to index is valid, index is returned back
 * to the caller. Else, this function searches for a lower c-state which is
 * still valid (as defined in omap3_power_states[]) and returns its index.
 *
 * A state is valid if the 'valid' field is enabled and
 * if it satisfies the enable_off_mode condition.
 */
static int next_valid_state(struct cpuidle_device *dev,
			    struct cpuidle_driver *drv, int index)
{
	struct omap3_idle_statedata *cx = &omap3_idle_data[index];
	u32 mpu_deepest_state = PWRDM_POWER_RET;
	u32 core_deepest_state = PWRDM_POWER_RET;
	int idx;
	int next_index = 0; /* C1 is the default value */

	if (enable_off_mode) {
		mpu_deepest_state = PWRDM_POWER_OFF;
		/*
		 * Erratum i583: valable for ES rev < Es1.2 on 3630.
		 * CORE OFF mode is not supported in a stable form, restrict
		 * instead the CORE state to RET.
		 */
		if (!IS_PM34XX_ERRATUM(PM_SDRC_WAKEUP_ERRATUM_i583))
			core_deepest_state = PWRDM_POWER_OFF;
	}

	/* Check if current state is valid */
	if ((cx->mpu_state >= mpu_deepest_state) &&
	    (cx->core_state >= core_deepest_state))
		return index;

	/*
	 * Drop to next valid state.
	 * Start search from the next (lower) state.
	 */
	for (idx = index - 1; idx >= 0; idx--) {
		cx =  &omap3_idle_data[idx];
		if ((cx->mpu_state >= mpu_deepest_state) &&
		    (cx->core_state >= core_deepest_state)) {
			next_index = idx;
			break;
		}
	}

	return next_index;
}

/**
 * omap3_enter_idle_bm - Checks for any bus activity
 * @dev: cpuidle device
 * @drv: cpuidle driver
 * @index: array index of target state to be programmed
 *
 * This function checks for any pending activity and then programs
 * the device to the specified or a safer state.
 */
static int omap3_enter_idle_bm(struct cpuidle_device *dev,
			       struct cpuidle_driver *drv,
			       int index)
{
	int new_state_idx;
	u32 core_next_state, per_next_state = 0, per_saved_state = 0;
	struct omap3_idle_statedata *cx;
	int ret;

	/*
	 * Use only C1 if CAM is active.
	 * CAM does not have wakeup capability in OMAP3.
	 */
	if (pwrdm_read_pwrst(cam_pd) == PWRDM_POWER_ON)
		new_state_idx = drv->safe_state_index;
	else
		new_state_idx = next_valid_state(dev, drv, index);

	/*
	 * FIXME: we currently manage device-specific idle states
	 *        for PER and CORE in combination with CPU-specific
	 *        idle states.  This is wrong, and device-specific
	 *        idle management needs to be separated out into
	 *        its own code.
	 */

	/* Program PER state */
	cx = &omap3_idle_data[new_state_idx];
	core_next_state = cx->core_state;
	per_next_state = per_saved_state = pwrdm_read_next_pwrst(per_pd);
	if (new_state_idx == 0) {
		/* In C1 do not allow PER state lower than CORE state */
		if (per_next_state < core_next_state)
			per_next_state = core_next_state;
	} else {
		/*
		 * Prevent PER OFF if CORE is not in RETention or OFF as this
		 * would disable PER wakeups completely.
		 */
		if ((per_next_state == PWRDM_POWER_OFF) &&
		    (core_next_state > PWRDM_POWER_RET))
			per_next_state = PWRDM_POWER_RET;
	}

	/* Are we changing PER target state? */
	if (per_next_state != per_saved_state)
		pwrdm_set_next_pwrst(per_pd, per_next_state);

	ret = omap3_enter_idle(dev, drv, new_state_idx);

	/* Restore original PER state if it was modified */
	if (per_next_state != per_saved_state)
		pwrdm_set_next_pwrst(per_pd, per_saved_state);

	return ret;
}

DEFINE_PER_CPU(struct cpuidle_device, omap3_idle_dev);

struct cpuidle_driver omap3_idle_driver = {
	.name = 	"omap3_idle",
	.owner = 	THIS_MODULE,
	.states = {
		{
			.enter		  = omap3_enter_idle_bm,
			.exit_latency	  = 2 + 2,
			.target_residency = 5,
			.flags		  = CPUIDLE_FLAG_TIME_VALID,
			.name		  = "C1",
			.desc		  = "MPU ON + CORE ON",
		},
		{
			.enter		  = omap3_enter_idle_bm,
			.exit_latency	  = 10 + 10,
			.target_residency = 30,
			.flags		  = CPUIDLE_FLAG_TIME_VALID,
			.name		  = "C2",
			.desc		  = "MPU ON + CORE ON",
		},
		{
			.enter		  = omap3_enter_idle_bm,
			.exit_latency	  = 50 + 50,
			.target_residency = 300,
			.flags		  = CPUIDLE_FLAG_TIME_VALID,
			.name		  = "C3",
			.desc		  = "MPU RET + CORE ON",
		},
		{
			.enter		  = omap3_enter_idle_bm,
			.exit_latency	  = 1500 + 1800,
			.target_residency = 4000,
			.flags		  = CPUIDLE_FLAG_TIME_VALID,
			.name		  = "C4",
			.desc		  = "MPU OFF + CORE ON",
		},
		{
			.enter		  = omap3_enter_idle_bm,
			.exit_latency	  = 2500 + 7500,
			.target_residency = 12000,
			.flags		  = CPUIDLE_FLAG_TIME_VALID,
			.name		  = "C5",
			.desc		  = "MPU RET + CORE RET",
		},
		{
			.enter		  = omap3_enter_idle_bm,
			.exit_latency	  = 3000 + 8500,
			.target_residency = 15000,
			.flags		  = CPUIDLE_FLAG_TIME_VALID,
			.name		  = "C6",
			.desc		  = "MPU OFF + CORE RET",
		},
		{
			.enter		  = omap3_enter_idle_bm,
			.exit_latency	  = 10000 + 30000,
			.target_residency = 30000,
			.flags		  = CPUIDLE_FLAG_TIME_VALID,
			.name		  = "C7",
			.desc		  = "MPU OFF + CORE OFF",
		},
	},
	.state_count = ARRAY_SIZE(omap3_idle_data),
	.safe_state_index = 0,
};

/**
 * omap3_idle_init - Init routine for OMAP3 idle
 *
 * Registers the OMAP3 specific cpuidle driver to the cpuidle
 * framework with the valid set of states.
 */
int __init omap3_idle_init(void)
{
	struct cpuidle_device *dev;

	mpu_pd = pwrdm_lookup("mpu_pwrdm");
	core_pd = pwrdm_lookup("core_pwrdm");
	per_pd = pwrdm_lookup("per_pwrdm");
	cam_pd = pwrdm_lookup("cam_pwrdm");

	if (!mpu_pd || !core_pd || !per_pd || !cam_pd)
		return -ENODEV;

	cpuidle_register_driver(&omap3_idle_driver);

	dev = &per_cpu(omap3_idle_dev, smp_processor_id());
	dev->cpu = 0;

	if (cpuidle_register_device(dev)) {
		printk(KERN_ERR "%s: CPUidle register device failed\n",
		       __func__);
		return -EIO;
	}

	return 0;
}
#else
int __init omap3_idle_init(void)
{
	return 0;
}
#endif /* CONFIG_CPU_IDLE */
