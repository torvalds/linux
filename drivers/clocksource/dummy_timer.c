// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/clocksource/dummy_timer.c
 *
 *  Copyright (C) 2013 ARM Ltd.
 *  All Rights Reserved
 */
#include <linux/clockchips.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>

static DEFINE_PER_CPU(struct clock_event_device, dummy_timer_evt);

static int dummy_timer_starting_cpu(unsigned int cpu)
{
	struct clock_event_device *evt = per_cpu_ptr(&dummy_timer_evt, cpu);

	evt->name	= "dummy_timer";
	evt->features	= CLOCK_EVT_FEAT_PERIODIC |
			  CLOCK_EVT_FEAT_ONESHOT |
			  CLOCK_EVT_FEAT_DUMMY;
	evt->rating	= 100;
	evt->cpumask	= cpumask_of(cpu);

	clockevents_register_device(evt);
	return 0;
}

static int __init dummy_timer_register(void)
{
	return cpuhp_setup_state(CPUHP_AP_DUMMY_TIMER_STARTING,
				 "clockevents/dummy_timer:starting",
				 dummy_timer_starting_cpu, NULL);
}
early_initcall(dummy_timer_register);
