/*
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clockchips.h>
#include <linux/cpuidle.h>
#include <linux/module.h>
#include <asm/cpuidle.h>
#include <asm/proc-fns.h>

#include "common.h"
#include "cpuidle.h"

static atomic_t master = ATOMIC_INIT(0);
static DEFINE_SPINLOCK(master_lock);

static int imx6q_enter_wait(struct cpuidle_device *dev,
			    struct cpuidle_driver *drv, int index)
{
	int cpu = dev->cpu;

	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER, &cpu);

	if (atomic_inc_return(&master) == num_online_cpus()) {
		/*
		 * With this lock, we prevent other cpu to exit and enter
		 * this function again and become the master.
		 */
		if (!spin_trylock(&master_lock))
			goto idle;
		imx6q_set_lpm(WAIT_UNCLOCKED);
		cpu_do_idle();
		imx6q_set_lpm(WAIT_CLOCKED);
		spin_unlock(&master_lock);
		goto done;
	}

idle:
	cpu_do_idle();
done:
	atomic_dec(&master);
	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT, &cpu);

	return index;
}

/*
 * For each cpu, setup the broadcast timer because local timer
 * stops for the states other than WFI.
 */
static void imx6q_setup_broadcast_timer(void *arg)
{
	int cpu = smp_processor_id();

	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ON, &cpu);
}

static struct cpuidle_driver imx6q_cpuidle_driver = {
	.name = "imx6q_cpuidle",
	.owner = THIS_MODULE,
	.en_core_tk_irqen = 1,
	.states = {
		/* WFI */
		ARM_CPUIDLE_WFI_STATE,
		/* WAIT */
		{
			.exit_latency = 50,
			.target_residency = 75,
			.flags = CPUIDLE_FLAG_TIME_VALID,
			.enter = imx6q_enter_wait,
			.name = "WAIT",
			.desc = "Clock off",
		},
	},
	.state_count = 2,
	.safe_state_index = 0,
};

int __init imx6q_cpuidle_init(void)
{
	/* Need to enable SCU standby for entering WAIT modes */
	imx_scu_standby_enable();

	/* Set chicken bit to get a reliable WAIT mode support */
	imx6q_set_chicken_bit();

	/* Configure the broadcast timer on each cpu */
	on_each_cpu(imx6q_setup_broadcast_timer, NULL, 1);

	return imx_cpuidle_init(&imx6q_cpuidle_driver);
}
