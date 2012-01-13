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

#include <plat/prcm.h>
#include <plat/irqs.h>
#include "powerdomain.h"
#include "clockdomain.h"
#include <plat/serial.h>

#include "pm.h"
#include "control.h"
#include "common.h"

#ifdef CONFIG_CPU_IDLE

/*
 * The latencies/thresholds for various C states have
 * to be configured from the respective board files.
 * These are some default values (which might not provide
 * the best power savings) used on boards which do not
 * pass these details from the board file.
 */
static struct cpuidle_params cpuidle_params_table[] = {
	/* C1 */
	{2 + 2, 5, 1},
	/* C2 */
	{10 + 10, 30, 1},
	/* C3 */
	{50 + 50, 300, 1},
	/* C4 */
	{1500 + 1800, 4000, 1},
	/* C5 */
	{2500 + 7500, 12000, 1},
	/* C6 */
	{3000 + 8500, 15000, 1},
	/* C7 */
	{10000 + 30000, 300000, 1},
};
#define OMAP3_NUM_STATES ARRAY_SIZE(cpuidle_params_table)

/* Mach specific information to be recorded in the C-state driver_data */
struct omap3_idle_statedata {
	u32 mpu_state;
	u32 core_state;
	u8 valid;
};
struct omap3_idle_statedata omap3_idle_data[OMAP3_NUM_STATES];

struct powerdomain *mpu_pd, *core_pd, *per_pd, *cam_pd;

static int _cpuidle_allow_idle(struct powerdomain *pwrdm,
				struct clockdomain *clkdm)
{
	clkdm_allow_idle(clkdm);
	return 0;
}

static int _cpuidle_deny_idle(struct powerdomain *pwrdm,
				struct clockdomain *clkdm)
{
	clkdm_deny_idle(clkdm);
	return 0;
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
static int omap3_enter_idle(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	struct omap3_idle_statedata *cx =
			cpuidle_get_statedata(&dev->states_usage[index]);
	struct timespec ts_preidle, ts_postidle, ts_idle;
	u32 mpu_state = cx->mpu_state, core_state = cx->core_state;
	int idle_time;

	/* Used to keep track of the total time in idle */
	getnstimeofday(&ts_preidle);

	local_irq_disable();
	local_fiq_disable();

	pwrdm_set_next_pwrst(mpu_pd, mpu_state);
	pwrdm_set_next_pwrst(core_pd, core_state);

	if (omap_irq_pending() || need_resched())
		goto return_sleep_time;

	/* Deny idle for C1 */
	if (index == 0) {
		pwrdm_for_each_clkdm(mpu_pd, _cpuidle_deny_idle);
		pwrdm_for_each_clkdm(core_pd, _cpuidle_deny_idle);
	}

	/* Execute ARM wfi */
	omap_sram_idle();

	/* Re-allow idle for C1 */
	if (index == 0) {
		pwrdm_for_each_clkdm(mpu_pd, _cpuidle_allow_idle);
		pwrdm_for_each_clkdm(core_pd, _cpuidle_allow_idle);
	}

return_sleep_time:
	getnstimeofday(&ts_postidle);
	ts_idle = timespec_sub(ts_postidle, ts_preidle);

	local_irq_enable();
	local_fiq_enable();

	idle_time = ts_idle.tv_nsec / NSEC_PER_USEC + ts_idle.tv_sec * \
								USEC_PER_SEC;

	/* Update cpuidle counters */
	dev->last_residency = idle_time;

	return index;
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
			struct cpuidle_driver *drv,
				int index)
{
	struct cpuidle_state_usage *curr_usage = &dev->states_usage[index];
	struct cpuidle_state *curr = &drv->states[index];
	struct omap3_idle_statedata *cx = cpuidle_get_statedata(curr_usage);
	u32 mpu_deepest_state = PWRDM_POWER_RET;
	u32 core_deepest_state = PWRDM_POWER_RET;
	int next_index = -1;

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
	if ((cx->valid) &&
	    (cx->mpu_state >= mpu_deepest_state) &&
	    (cx->core_state >= core_deepest_state)) {
		return index;
	} else {
		int idx = OMAP3_NUM_STATES - 1;

		/* Reach the current state starting at highest C-state */
		for (; idx >= 0; idx--) {
			if (&drv->states[idx] == curr) {
				next_index = idx;
				break;
			}
		}

		/* Should never hit this condition */
		WARN_ON(next_index == -1);

		/*
		 * Drop to next valid state.
		 * Start search from the next (lower) state.
		 */
		idx--;
		for (; idx >= 0; idx--) {
			cx = cpuidle_get_statedata(&dev->states_usage[idx]);
			if ((cx->valid) &&
			    (cx->mpu_state >= mpu_deepest_state) &&
			    (cx->core_state >= core_deepest_state)) {
				next_index = idx;
				break;
			}
		}
		/*
		 * C1 is always valid.
		 * So, no need to check for 'next_index == -1' outside
		 * this loop.
		 */
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
	u32 core_next_state, per_next_state = 0, per_saved_state = 0, cam_state;
	struct omap3_idle_statedata *cx;
	int ret;

	if (!omap3_can_sleep()) {
		new_state_idx = drv->safe_state_index;
		goto select_state;
	}

	/*
	 * Prevent idle completely if CAM is active.
	 * CAM does not have wakeup capability in OMAP3.
	 */
	cam_state = pwrdm_read_pwrst(cam_pd);
	if (cam_state == PWRDM_POWER_ON) {
		new_state_idx = drv->safe_state_index;
		goto select_state;
	}

	/*
	 * FIXME: we currently manage device-specific idle states
	 *        for PER and CORE in combination with CPU-specific
	 *        idle states.  This is wrong, and device-specific
	 *        idle management needs to be separated out into
	 *        its own code.
	 */

	/*
	 * Prevent PER off if CORE is not in retention or off as this
	 * would disable PER wakeups completely.
	 */
	cx = cpuidle_get_statedata(&dev->states_usage[index]);
	core_next_state = cx->core_state;
	per_next_state = per_saved_state = pwrdm_read_next_pwrst(per_pd);
	if ((per_next_state == PWRDM_POWER_OFF) &&
	    (core_next_state > PWRDM_POWER_RET))
		per_next_state = PWRDM_POWER_RET;

	/* Are we changing PER target state? */
	if (per_next_state != per_saved_state)
		pwrdm_set_next_pwrst(per_pd, per_next_state);

	new_state_idx = next_valid_state(dev, drv, index);

select_state:
	ret = omap3_enter_idle(dev, drv, new_state_idx);

	/* Restore original PER state if it was modified */
	if (per_next_state != per_saved_state)
		pwrdm_set_next_pwrst(per_pd, per_saved_state);

	return ret;
}

DEFINE_PER_CPU(struct cpuidle_device, omap3_idle_dev);

void omap3_pm_init_cpuidle(struct cpuidle_params *cpuidle_board_params)
{
	int i;

	if (!cpuidle_board_params)
		return;

	for (i = 0; i < OMAP3_NUM_STATES; i++) {
		cpuidle_params_table[i].valid =	cpuidle_board_params[i].valid;
		cpuidle_params_table[i].exit_latency =
			cpuidle_board_params[i].exit_latency;
		cpuidle_params_table[i].target_residency =
			cpuidle_board_params[i].target_residency;
	}
	return;
}

struct cpuidle_driver omap3_idle_driver = {
	.name = 	"omap3_idle",
	.owner = 	THIS_MODULE,
};

/* Helper to fill the C-state common data*/
static inline void _fill_cstate(struct cpuidle_driver *drv,
					int idx, const char *descr)
{
	struct cpuidle_state *state = &drv->states[idx];

	state->exit_latency	= cpuidle_params_table[idx].exit_latency;
	state->target_residency	= cpuidle_params_table[idx].target_residency;
	state->flags		= CPUIDLE_FLAG_TIME_VALID;
	state->enter		= omap3_enter_idle_bm;
	sprintf(state->name, "C%d", idx + 1);
	strncpy(state->desc, descr, CPUIDLE_DESC_LEN);

}

/* Helper to register the driver_data */
static inline struct omap3_idle_statedata *_fill_cstate_usage(
					struct cpuidle_device *dev,
					int idx)
{
	struct omap3_idle_statedata *cx = &omap3_idle_data[idx];
	struct cpuidle_state_usage *state_usage = &dev->states_usage[idx];

	cx->valid		= cpuidle_params_table[idx].valid;
	cpuidle_set_statedata(state_usage, cx);

	return cx;
}

/**
 * omap3_idle_init - Init routine for OMAP3 idle
 *
 * Registers the OMAP3 specific cpuidle driver to the cpuidle
 * framework with the valid set of states.
 */
int __init omap3_idle_init(void)
{
	struct cpuidle_device *dev;
	struct cpuidle_driver *drv = &omap3_idle_driver;
	struct omap3_idle_statedata *cx;

	mpu_pd = pwrdm_lookup("mpu_pwrdm");
	core_pd = pwrdm_lookup("core_pwrdm");
	per_pd = pwrdm_lookup("per_pwrdm");
	cam_pd = pwrdm_lookup("cam_pwrdm");


	drv->safe_state_index = -1;
	dev = &per_cpu(omap3_idle_dev, smp_processor_id());

	/* C1 . MPU WFI + Core active */
	_fill_cstate(drv, 0, "MPU ON + CORE ON");
	(&drv->states[0])->enter = omap3_enter_idle;
	drv->safe_state_index = 0;
	cx = _fill_cstate_usage(dev, 0);
	cx->valid = 1;	/* C1 is always valid */
	cx->mpu_state = PWRDM_POWER_ON;
	cx->core_state = PWRDM_POWER_ON;

	/* C2 . MPU WFI + Core inactive */
	_fill_cstate(drv, 1, "MPU ON + CORE ON");
	cx = _fill_cstate_usage(dev, 1);
	cx->mpu_state = PWRDM_POWER_ON;
	cx->core_state = PWRDM_POWER_ON;

	/* C3 . MPU CSWR + Core inactive */
	_fill_cstate(drv, 2, "MPU RET + CORE ON");
	cx = _fill_cstate_usage(dev, 2);
	cx->mpu_state = PWRDM_POWER_RET;
	cx->core_state = PWRDM_POWER_ON;

	/* C4 . MPU OFF + Core inactive */
	_fill_cstate(drv, 3, "MPU OFF + CORE ON");
	cx = _fill_cstate_usage(dev, 3);
	cx->mpu_state = PWRDM_POWER_OFF;
	cx->core_state = PWRDM_POWER_ON;

	/* C5 . MPU RET + Core RET */
	_fill_cstate(drv, 4, "MPU RET + CORE RET");
	cx = _fill_cstate_usage(dev, 4);
	cx->mpu_state = PWRDM_POWER_RET;
	cx->core_state = PWRDM_POWER_RET;

	/* C6 . MPU OFF + Core RET */
	_fill_cstate(drv, 5, "MPU OFF + CORE RET");
	cx = _fill_cstate_usage(dev, 5);
	cx->mpu_state = PWRDM_POWER_OFF;
	cx->core_state = PWRDM_POWER_RET;

	/* C7 . MPU OFF + Core OFF */
	_fill_cstate(drv, 6, "MPU OFF + CORE OFF");
	cx = _fill_cstate_usage(dev, 6);
	/*
	 * Erratum i583: implementation for ES rev < Es1.2 on 3630. We cannot
	 * enable OFF mode in a stable form for previous revisions.
	 * We disable C7 state as a result.
	 */
	if (IS_PM34XX_ERRATUM(PM_SDRC_WAKEUP_ERRATUM_i583)) {
		cx->valid = 0;
		pr_warn("%s: core off state C7 disabled due to i583\n",
			__func__);
	}
	cx->mpu_state = PWRDM_POWER_OFF;
	cx->core_state = PWRDM_POWER_OFF;

	drv->state_count = OMAP3_NUM_STATES;
	cpuidle_register_driver(&omap3_idle_driver);

	dev->state_count = OMAP3_NUM_STATES;
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
