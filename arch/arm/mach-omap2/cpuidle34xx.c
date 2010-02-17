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

#include <plat/prcm.h>
#include <plat/irqs.h>
#include <plat/powerdomain.h>
#include <plat/clockdomain.h>
#include <plat/control.h>
#include <plat/serial.h>

#include "pm.h"

#ifdef CONFIG_CPU_IDLE

#define OMAP3_MAX_STATES 7
#define OMAP3_STATE_C1 0 /* C1 - MPU WFI + Core active */
#define OMAP3_STATE_C2 1 /* C2 - MPU WFI + Core inactive */
#define OMAP3_STATE_C3 2 /* C3 - MPU CSWR + Core inactive */
#define OMAP3_STATE_C4 3 /* C4 - MPU OFF + Core iactive */
#define OMAP3_STATE_C5 4 /* C5 - MPU RET + Core RET */
#define OMAP3_STATE_C6 5 /* C6 - MPU OFF + Core RET */
#define OMAP3_STATE_C7 6 /* C7 - MPU OFF + Core OFF */

struct omap3_processor_cx {
	u8 valid;
	u8 type;
	u32 sleep_latency;
	u32 wakeup_latency;
	u32 mpu_state;
	u32 core_state;
	u32 threshold;
	u32 flags;
};

struct omap3_processor_cx omap3_power_states[OMAP3_MAX_STATES];
struct omap3_processor_cx current_cx_state;
struct powerdomain *mpu_pd, *core_pd;

static int omap3_idle_bm_check(void)
{
	if (!omap3_can_sleep())
		return 1;
	return 0;
}

static int _cpuidle_allow_idle(struct powerdomain *pwrdm,
				struct clockdomain *clkdm)
{
	omap2_clkdm_allow_idle(clkdm);
	return 0;
}

static int _cpuidle_deny_idle(struct powerdomain *pwrdm,
				struct clockdomain *clkdm)
{
	omap2_clkdm_deny_idle(clkdm);
	return 0;
}

/**
 * omap3_enter_idle - Programs OMAP3 to enter the specified state
 * @dev: cpuidle device
 * @state: The target state to be programmed
 *
 * Called from the CPUidle framework to program the device to the
 * specified target state selected by the governor.
 */
static int omap3_enter_idle(struct cpuidle_device *dev,
			struct cpuidle_state *state)
{
	struct omap3_processor_cx *cx = cpuidle_get_statedata(state);
	struct timespec ts_preidle, ts_postidle, ts_idle;
	u32 mpu_state = cx->mpu_state, core_state = cx->core_state;

	current_cx_state = *cx;

	/* Used to keep track of the total time in idle */
	getnstimeofday(&ts_preidle);

	local_irq_disable();
	local_fiq_disable();

	if (!enable_off_mode) {
		if (mpu_state < PWRDM_POWER_RET)
			mpu_state = PWRDM_POWER_RET;
		if (core_state < PWRDM_POWER_RET)
			core_state = PWRDM_POWER_RET;
	}

	pwrdm_set_next_pwrst(mpu_pd, mpu_state);
	pwrdm_set_next_pwrst(core_pd, core_state);

	if (omap_irq_pending() || need_resched())
		goto return_sleep_time;

	if (cx->type == OMAP3_STATE_C1) {
		pwrdm_for_each_clkdm(mpu_pd, _cpuidle_deny_idle);
		pwrdm_for_each_clkdm(core_pd, _cpuidle_deny_idle);
	}

	/* Execute ARM wfi */
	omap_sram_idle();

	if (cx->type == OMAP3_STATE_C1) {
		pwrdm_for_each_clkdm(mpu_pd, _cpuidle_allow_idle);
		pwrdm_for_each_clkdm(core_pd, _cpuidle_allow_idle);
	}

return_sleep_time:
	getnstimeofday(&ts_postidle);
	ts_idle = timespec_sub(ts_postidle, ts_preidle);

	local_irq_enable();
	local_fiq_enable();

	return ts_idle.tv_nsec / NSEC_PER_USEC + ts_idle.tv_sec * USEC_PER_SEC;
}

/**
 * omap3_enter_idle_bm - Checks for any bus activity
 * @dev: cpuidle device
 * @state: The target state to be programmed
 *
 * Used for C states with CPUIDLE_FLAG_CHECK_BM flag set. This
 * function checks for any pending activity and then programs the
 * device to the specified or a safer state.
 */
static int omap3_enter_idle_bm(struct cpuidle_device *dev,
			       struct cpuidle_state *state)
{
	struct cpuidle_state *new_state = state;

	if ((state->flags & CPUIDLE_FLAG_CHECK_BM) && omap3_idle_bm_check()) {
		BUG_ON(!dev->safe_state);
		new_state = dev->safe_state;
	}

	dev->last_state = new_state;
	return omap3_enter_idle(dev, new_state);
}

DEFINE_PER_CPU(struct cpuidle_device, omap3_idle_dev);

/* omap3_init_power_states - Initialises the OMAP3 specific C states.
 *
 * Below is the desciption of each C state.
 * 	C1 . MPU WFI + Core active
 *	C2 . MPU WFI + Core inactive
 *	C3 . MPU CSWR + Core inactive
 *	C4 . MPU OFF + Core inactive
 *	C5 . MPU CSWR + Core CSWR
 *	C6 . MPU OFF + Core CSWR
 *	C7 . MPU OFF + Core OFF
 */
void omap_init_power_states(void)
{
	/* C1 . MPU WFI + Core active */
	omap3_power_states[OMAP3_STATE_C1].valid = 1;
	omap3_power_states[OMAP3_STATE_C1].type = OMAP3_STATE_C1;
	omap3_power_states[OMAP3_STATE_C1].sleep_latency = 2;
	omap3_power_states[OMAP3_STATE_C1].wakeup_latency = 2;
	omap3_power_states[OMAP3_STATE_C1].threshold = 5;
	omap3_power_states[OMAP3_STATE_C1].mpu_state = PWRDM_POWER_ON;
	omap3_power_states[OMAP3_STATE_C1].core_state = PWRDM_POWER_ON;
	omap3_power_states[OMAP3_STATE_C1].flags = CPUIDLE_FLAG_TIME_VALID;

	/* C2 . MPU WFI + Core inactive */
	omap3_power_states[OMAP3_STATE_C2].valid = 1;
	omap3_power_states[OMAP3_STATE_C2].type = OMAP3_STATE_C2;
	omap3_power_states[OMAP3_STATE_C2].sleep_latency = 10;
	omap3_power_states[OMAP3_STATE_C2].wakeup_latency = 10;
	omap3_power_states[OMAP3_STATE_C2].threshold = 30;
	omap3_power_states[OMAP3_STATE_C2].mpu_state = PWRDM_POWER_ON;
	omap3_power_states[OMAP3_STATE_C2].core_state = PWRDM_POWER_ON;
	omap3_power_states[OMAP3_STATE_C2].flags = CPUIDLE_FLAG_TIME_VALID;

	/* C3 . MPU CSWR + Core inactive */
	omap3_power_states[OMAP3_STATE_C3].valid = 1;
	omap3_power_states[OMAP3_STATE_C3].type = OMAP3_STATE_C3;
	omap3_power_states[OMAP3_STATE_C3].sleep_latency = 50;
	omap3_power_states[OMAP3_STATE_C3].wakeup_latency = 50;
	omap3_power_states[OMAP3_STATE_C3].threshold = 300;
	omap3_power_states[OMAP3_STATE_C3].mpu_state = PWRDM_POWER_RET;
	omap3_power_states[OMAP3_STATE_C3].core_state = PWRDM_POWER_ON;
	omap3_power_states[OMAP3_STATE_C3].flags = CPUIDLE_FLAG_TIME_VALID |
				CPUIDLE_FLAG_CHECK_BM;

	/* C4 . MPU OFF + Core inactive */
	omap3_power_states[OMAP3_STATE_C4].valid = 1;
	omap3_power_states[OMAP3_STATE_C4].type = OMAP3_STATE_C4;
	omap3_power_states[OMAP3_STATE_C4].sleep_latency = 1500;
	omap3_power_states[OMAP3_STATE_C4].wakeup_latency = 1800;
	omap3_power_states[OMAP3_STATE_C4].threshold = 4000;
	omap3_power_states[OMAP3_STATE_C4].mpu_state = PWRDM_POWER_OFF;
	omap3_power_states[OMAP3_STATE_C4].core_state = PWRDM_POWER_ON;
	omap3_power_states[OMAP3_STATE_C4].flags = CPUIDLE_FLAG_TIME_VALID |
				CPUIDLE_FLAG_CHECK_BM;

	/* C5 . MPU CSWR + Core CSWR*/
	omap3_power_states[OMAP3_STATE_C5].valid = 1;
	omap3_power_states[OMAP3_STATE_C5].type = OMAP3_STATE_C5;
	omap3_power_states[OMAP3_STATE_C5].sleep_latency = 2500;
	omap3_power_states[OMAP3_STATE_C5].wakeup_latency = 7500;
	omap3_power_states[OMAP3_STATE_C5].threshold = 12000;
	omap3_power_states[OMAP3_STATE_C5].mpu_state = PWRDM_POWER_RET;
	omap3_power_states[OMAP3_STATE_C5].core_state = PWRDM_POWER_RET;
	omap3_power_states[OMAP3_STATE_C5].flags = CPUIDLE_FLAG_TIME_VALID |
				CPUIDLE_FLAG_CHECK_BM;

	/* C6 . MPU OFF + Core CSWR */
	omap3_power_states[OMAP3_STATE_C6].valid = 1;
	omap3_power_states[OMAP3_STATE_C6].type = OMAP3_STATE_C6;
	omap3_power_states[OMAP3_STATE_C6].sleep_latency = 3000;
	omap3_power_states[OMAP3_STATE_C6].wakeup_latency = 8500;
	omap3_power_states[OMAP3_STATE_C6].threshold = 15000;
	omap3_power_states[OMAP3_STATE_C6].mpu_state = PWRDM_POWER_OFF;
	omap3_power_states[OMAP3_STATE_C6].core_state = PWRDM_POWER_RET;
	omap3_power_states[OMAP3_STATE_C6].flags = CPUIDLE_FLAG_TIME_VALID |
				CPUIDLE_FLAG_CHECK_BM;

	/* C7 . MPU OFF + Core OFF */
	omap3_power_states[OMAP3_STATE_C7].valid = 1;
	omap3_power_states[OMAP3_STATE_C7].type = OMAP3_STATE_C7;
	omap3_power_states[OMAP3_STATE_C7].sleep_latency = 10000;
	omap3_power_states[OMAP3_STATE_C7].wakeup_latency = 30000;
	omap3_power_states[OMAP3_STATE_C7].threshold = 300000;
	omap3_power_states[OMAP3_STATE_C7].mpu_state = PWRDM_POWER_OFF;
	omap3_power_states[OMAP3_STATE_C7].core_state = PWRDM_POWER_OFF;
	omap3_power_states[OMAP3_STATE_C7].flags = CPUIDLE_FLAG_TIME_VALID |
				CPUIDLE_FLAG_CHECK_BM;
}

struct cpuidle_driver omap3_idle_driver = {
	.name = 	"omap3_idle",
	.owner = 	THIS_MODULE,
};

/**
 * omap3_idle_init - Init routine for OMAP3 idle
 *
 * Registers the OMAP3 specific cpuidle driver with the cpuidle
 * framework with the valid set of states.
 */
int __init omap3_idle_init(void)
{
	int i, count = 0;
	struct omap3_processor_cx *cx;
	struct cpuidle_state *state;
	struct cpuidle_device *dev;

	mpu_pd = pwrdm_lookup("mpu_pwrdm");
	core_pd = pwrdm_lookup("core_pwrdm");

	omap_init_power_states();
	cpuidle_register_driver(&omap3_idle_driver);

	dev = &per_cpu(omap3_idle_dev, smp_processor_id());

	for (i = OMAP3_STATE_C1; i < OMAP3_MAX_STATES; i++) {
		cx = &omap3_power_states[i];
		state = &dev->states[count];

		if (!cx->valid)
			continue;
		cpuidle_set_statedata(state, cx);
		state->exit_latency = cx->sleep_latency + cx->wakeup_latency;
		state->target_residency = cx->threshold;
		state->flags = cx->flags;
		state->enter = (state->flags & CPUIDLE_FLAG_CHECK_BM) ?
			omap3_enter_idle_bm : omap3_enter_idle;
		if (cx->type == OMAP3_STATE_C1)
			dev->safe_state = state;
		sprintf(state->name, "C%d", count+1);
		count++;
	}

	if (!count)
		return -EINVAL;
	dev->state_count = count;

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
