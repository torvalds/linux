// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP4+ CPU idle Routines
 *
 * Copyright (C) 2011-2013 Texas Instruments, Inc.
 * Santosh Shilimkar <santosh.shilimkar@ti.com>
 * Rajendra Nayak <rnayak@ti.com>
 */

#include <linux/sched.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/export.h>
#include <linux/tick.h>

#include <asm/cpuidle.h>

#include "common.h"
#include "pm.h"
#include "prm.h"
#include "soc.h"
#include "clockdomain.h"

#define MAX_CPUS	2

/* Machine specific information */
struct idle_statedata {
	u32 cpu_state;
	u32 mpu_logic_state;
	u32 mpu_state;
	u32 mpu_state_vote;
};

static struct idle_statedata omap4_idle_data[] = {
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

static struct idle_statedata omap5_idle_data[] = {
	{
		.cpu_state = PWRDM_POWER_ON,
		.mpu_state = PWRDM_POWER_ON,
		.mpu_logic_state = PWRDM_POWER_ON,
	},
	{
		.cpu_state = PWRDM_POWER_RET,
		.mpu_state = PWRDM_POWER_RET,
		.mpu_logic_state = PWRDM_POWER_RET,
	},
};

static struct powerdomain *mpu_pd, *cpu_pd[MAX_CPUS];
static struct clockdomain *cpu_clkdm[MAX_CPUS];

static atomic_t abort_barrier;
static bool cpu_done[MAX_CPUS];
static struct idle_statedata *state_ptr = &omap4_idle_data[0];
static DEFINE_RAW_SPINLOCK(mpu_lock);

/* Private functions */

/**
 * omap_enter_idle_[simple/coupled] - OMAP4PLUS cpuidle entry functions
 * @dev: cpuidle device
 * @drv: cpuidle driver
 * @index: the index of state to be entered
 *
 * Called from the CPUidle framework to program the device to the
 * specified low power state selected by the governor.
 * Returns the amount of time spent in the low power state.
 */
static int omap_enter_idle_simple(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{
	omap_do_wfi();
	return index;
}

static int omap_enter_idle_smp(struct cpuidle_device *dev,
			       struct cpuidle_driver *drv,
			       int index)
{
	struct idle_statedata *cx = state_ptr + index;
	unsigned long flag;

	raw_spin_lock_irqsave(&mpu_lock, flag);
	cx->mpu_state_vote++;
	if (cx->mpu_state_vote == num_online_cpus()) {
		pwrdm_set_logic_retst(mpu_pd, cx->mpu_logic_state);
		omap_set_pwrdm_state(mpu_pd, cx->mpu_state);
	}
	raw_spin_unlock_irqrestore(&mpu_lock, flag);

	omap4_enter_lowpower(dev->cpu, cx->cpu_state, true);

	raw_spin_lock_irqsave(&mpu_lock, flag);
	if (cx->mpu_state_vote == num_online_cpus())
		omap_set_pwrdm_state(mpu_pd, PWRDM_POWER_ON);
	cx->mpu_state_vote--;
	raw_spin_unlock_irqrestore(&mpu_lock, flag);

	return index;
}

static int omap_enter_idle_coupled(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{
	struct idle_statedata *cx = state_ptr + index;
	u32 mpuss_can_lose_context = 0;
	int error;

	/*
	 * CPU0 has to wait and stay ON until CPU1 is OFF state.
	 * This is necessary to honour hardware recommondation
	 * of triggeing all the possible low power modes once CPU1 is
	 * out of coherency and in OFF mode.
	 */
	if (dev->cpu == 0 && cpumask_test_cpu(1, cpu_online_mask)) {
		while (pwrdm_read_pwrst(cpu_pd[1]) != PWRDM_POWER_OFF) {
			cpu_relax();

			/*
			 * CPU1 could have already entered & exited idle
			 * without hitting off because of a wakeup
			 * or a failed attempt to hit off mode.  Check for
			 * that here, otherwise we could spin forever
			 * waiting for CPU1 off.
			 */
			if (cpu_done[1])
			    goto fail;

		}
	}

	mpuss_can_lose_context = (cx->mpu_state == PWRDM_POWER_RET) &&
				 (cx->mpu_logic_state == PWRDM_POWER_OFF);

	/* Enter broadcast mode for periodic timers */
	tick_broadcast_enable();

	/* Enter broadcast mode for one-shot timers */
	tick_broadcast_enter();

	/*
	 * Call idle CPU PM enter notifier chain so that
	 * VFP and per CPU interrupt context is saved.
	 */
	error = cpu_pm_enter();
	if (error)
		goto cpu_pm_out;

	if (dev->cpu == 0) {
		pwrdm_set_logic_retst(mpu_pd, cx->mpu_logic_state);
		omap_set_pwrdm_state(mpu_pd, cx->mpu_state);

		/*
		 * Call idle CPU cluster PM enter notifier chain
		 * to save GIC and wakeupgen context.
		 */
		if (mpuss_can_lose_context) {
			error = cpu_cluster_pm_enter();
			if (error) {
				index = 0;
				cx = state_ptr + index;
				pwrdm_set_logic_retst(mpu_pd, cx->mpu_logic_state);
				omap_set_pwrdm_state(mpu_pd, cx->mpu_state);
				mpuss_can_lose_context = 0;
			}
		}
	}

	omap4_enter_lowpower(dev->cpu, cx->cpu_state, true);
	cpu_done[dev->cpu] = true;

	/* Wakeup CPU1 only if it is not offlined */
	if (dev->cpu == 0 && cpumask_test_cpu(1, cpu_online_mask)) {

		if (IS_PM44XX_ERRATUM(PM_OMAP4_ROM_SMP_BOOT_ERRATUM_GICD) &&
		    mpuss_can_lose_context)
			gic_dist_disable();

		clkdm_deny_idle(cpu_clkdm[1]);
		omap_set_pwrdm_state(cpu_pd[1], PWRDM_POWER_ON);
		clkdm_allow_idle(cpu_clkdm[1]);

		if (IS_PM44XX_ERRATUM(PM_OMAP4_ROM_SMP_BOOT_ERRATUM_GICD) &&
		    mpuss_can_lose_context) {
			while (gic_dist_disabled()) {
				udelay(1);
				cpu_relax();
			}
			gic_timer_retrigger();
		}
	}

	/*
	 * Call idle CPU cluster PM exit notifier chain
	 * to restore GIC and wakeupgen context.
	 */
	if (dev->cpu == 0 && mpuss_can_lose_context)
		cpu_cluster_pm_exit();

	/*
	 * Call idle CPU PM exit notifier chain to restore
	 * VFP and per CPU IRQ context.
	 */
	cpu_pm_exit();

cpu_pm_out:
	tick_broadcast_exit();

fail:
	cpuidle_coupled_parallel_barrier(dev, &abort_barrier);
	cpu_done[dev->cpu] = false;

	return index;
}

static struct cpuidle_driver omap4_idle_driver = {
	.name				= "omap4_idle",
	.owner				= THIS_MODULE,
	.states = {
		{
			/* C1 - CPU0 ON + CPU1 ON + MPU ON */
			.exit_latency = 2 + 2,
			.target_residency = 5,
			.enter = omap_enter_idle_simple,
			.name = "C1",
			.desc = "CPUx ON, MPUSS ON"
		},
		{
			/* C2 - CPU0 OFF + CPU1 OFF + MPU CSWR */
			.exit_latency = 328 + 440,
			.target_residency = 960,
			.flags = CPUIDLE_FLAG_COUPLED |
				 CPUIDLE_FLAG_RCU_IDLE,
			.enter = omap_enter_idle_coupled,
			.name = "C2",
			.desc = "CPUx OFF, MPUSS CSWR",
		},
		{
			/* C3 - CPU0 OFF + CPU1 OFF + MPU OSWR */
			.exit_latency = 460 + 518,
			.target_residency = 1100,
			.flags = CPUIDLE_FLAG_COUPLED |
				 CPUIDLE_FLAG_RCU_IDLE,
			.enter = omap_enter_idle_coupled,
			.name = "C3",
			.desc = "CPUx OFF, MPUSS OSWR",
		},
	},
	.state_count = ARRAY_SIZE(omap4_idle_data),
	.safe_state_index = 0,
};

static struct cpuidle_driver omap5_idle_driver = {
	.name				= "omap5_idle",
	.owner				= THIS_MODULE,
	.states = {
		{
			/* C1 - CPU0 ON + CPU1 ON + MPU ON */
			.exit_latency = 2 + 2,
			.target_residency = 5,
			.enter = omap_enter_idle_simple,
			.name = "C1",
			.desc = "CPUx WFI, MPUSS ON"
		},
		{
			/* C2 - CPU0 RET + CPU1 RET + MPU CSWR */
			.exit_latency = 48 + 60,
			.target_residency = 100,
			.flags = CPUIDLE_FLAG_TIMER_STOP |
				 CPUIDLE_FLAG_RCU_IDLE,
			.enter = omap_enter_idle_smp,
			.name = "C2",
			.desc = "CPUx CSWR, MPUSS CSWR",
		},
	},
	.state_count = ARRAY_SIZE(omap5_idle_data),
	.safe_state_index = 0,
};

/* Public functions */

/**
 * omap4_idle_init - Init routine for OMAP4+ idle
 *
 * Registers the OMAP4+ specific cpuidle driver to the cpuidle
 * framework with the valid set of states.
 */
int __init omap4_idle_init(void)
{
	struct cpuidle_driver *idle_driver;

	if (soc_is_omap54xx()) {
		state_ptr = &omap5_idle_data[0];
		idle_driver = &omap5_idle_driver;
	} else {
		state_ptr = &omap4_idle_data[0];
		idle_driver = &omap4_idle_driver;
	}

	mpu_pd = pwrdm_lookup("mpu_pwrdm");
	cpu_pd[0] = pwrdm_lookup("cpu0_pwrdm");
	cpu_pd[1] = pwrdm_lookup("cpu1_pwrdm");
	if ((!mpu_pd) || (!cpu_pd[0]) || (!cpu_pd[1]))
		return -ENODEV;

	cpu_clkdm[0] = clkdm_lookup("mpu0_clkdm");
	cpu_clkdm[1] = clkdm_lookup("mpu1_clkdm");
	if (!cpu_clkdm[0] || !cpu_clkdm[1])
		return -ENODEV;

	return cpuidle_register(idle_driver, cpu_online_mask);
}
