// SPDX-License-Identifier: GPL-2.0
/*
 * haltpoll.c - haltpoll idle governor
 *
 * Copyright 2019 Red Hat, Inc. and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Authors: Marcelo Tosatti <mtosatti@redhat.com>
 */

#include <linux/kernel.h>
#include <linux/cpuidle.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/kvm_para.h>

static unsigned int guest_halt_poll_ns __read_mostly = 200000;
module_param(guest_halt_poll_ns, uint, 0644);

/* division factor to shrink halt_poll_ns */
static unsigned int guest_halt_poll_shrink __read_mostly = 2;
module_param(guest_halt_poll_shrink, uint, 0644);

/* multiplication factor to grow per-cpu poll_limit_ns */
static unsigned int guest_halt_poll_grow __read_mostly = 2;
module_param(guest_halt_poll_grow, uint, 0644);

/* value in us to start growing per-cpu halt_poll_ns */
static unsigned int guest_halt_poll_grow_start __read_mostly = 50000;
module_param(guest_halt_poll_grow_start, uint, 0644);

/* allow shrinking guest halt poll */
static bool guest_halt_poll_allow_shrink __read_mostly = true;
module_param(guest_halt_poll_allow_shrink, bool, 0644);

/**
 * haltpoll_select - selects the next idle state to enter
 * @drv: cpuidle driver containing state data
 * @dev: the CPU
 * @stop_tick: indication on whether or not to stop the tick
 */
static int haltpoll_select(struct cpuidle_driver *drv,
			   struct cpuidle_device *dev,
			   bool *stop_tick)
{
	int latency_req = cpuidle_governor_latency_req(dev->cpu);

	if (!drv->state_count || latency_req == 0) {
		*stop_tick = false;
		return 0;
	}

	if (dev->poll_limit_ns == 0)
		return 1;

	/* Last state was poll? */
	if (dev->last_state_idx == 0) {
		/* Halt if no event occurred on poll window */
		if (dev->poll_time_limit == true)
			return 1;

		*stop_tick = false;
		/* Otherwise, poll again */
		return 0;
	}

	*stop_tick = false;
	/* Last state was halt: poll */
	return 0;
}

static void adjust_poll_limit(struct cpuidle_device *dev, unsigned int block_us)
{
	unsigned int val;
	u64 block_ns = block_us*NSEC_PER_USEC;

	/* Grow cpu_halt_poll_us if
	 * cpu_halt_poll_us < block_ns < guest_halt_poll_us
	 */
	if (block_ns > dev->poll_limit_ns && block_ns <= guest_halt_poll_ns) {
		val = dev->poll_limit_ns * guest_halt_poll_grow;

		if (val < guest_halt_poll_grow_start)
			val = guest_halt_poll_grow_start;
		if (val > guest_halt_poll_ns)
			val = guest_halt_poll_ns;

		dev->poll_limit_ns = val;
	} else if (block_ns > guest_halt_poll_ns &&
		   guest_halt_poll_allow_shrink) {
		unsigned int shrink = guest_halt_poll_shrink;

		val = dev->poll_limit_ns;
		if (shrink == 0)
			val = 0;
		else
			val /= shrink;
		dev->poll_limit_ns = val;
	}
}

/**
 * haltpoll_reflect - update variables and update poll time
 * @dev: the CPU
 * @index: the index of actual entered state
 */
static void haltpoll_reflect(struct cpuidle_device *dev, int index)
{
	dev->last_state_idx = index;

	if (index != 0)
		adjust_poll_limit(dev, dev->last_residency);
}

/**
 * haltpoll_enable_device - scans a CPU's states and does setup
 * @drv: cpuidle driver
 * @dev: the CPU
 */
static int haltpoll_enable_device(struct cpuidle_driver *drv,
				  struct cpuidle_device *dev)
{
	dev->poll_limit_ns = 0;

	return 0;
}

static struct cpuidle_governor haltpoll_governor = {
	.name =			"haltpoll",
	.rating =		9,
	.enable =		haltpoll_enable_device,
	.select =		haltpoll_select,
	.reflect =		haltpoll_reflect,
};

static int __init init_haltpoll(void)
{
	if (kvm_para_available())
		return cpuidle_register_governor(&haltpoll_governor);

	return 0;
}

postcore_initcall(init_haltpoll);
