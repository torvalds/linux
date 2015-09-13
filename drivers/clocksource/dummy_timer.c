/*
 *  linux/drivers/clocksource/dummy_timer.c
 *
 *  Copyright (C) 2013 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clockchips.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>

static DEFINE_PER_CPU(struct clock_event_device, dummy_timer_evt);

static void dummy_timer_setup(void)
{
	int cpu = smp_processor_id();
	struct clock_event_device *evt = raw_cpu_ptr(&dummy_timer_evt);

	evt->name	= "dummy_timer";
	evt->features	= CLOCK_EVT_FEAT_PERIODIC |
			  CLOCK_EVT_FEAT_ONESHOT |
			  CLOCK_EVT_FEAT_DUMMY;
	evt->rating	= 100;
	evt->cpumask	= cpumask_of(cpu);

	clockevents_register_device(evt);
}

static int dummy_timer_cpu_notify(struct notifier_block *self,
				      unsigned long action, void *hcpu)
{
	if ((action & ~CPU_TASKS_FROZEN) == CPU_STARTING)
		dummy_timer_setup();

	return NOTIFY_OK;
}

static struct notifier_block dummy_timer_cpu_nb = {
	.notifier_call = dummy_timer_cpu_notify,
};

static int __init dummy_timer_register(void)
{
	int err = 0;

	cpu_notifier_register_begin();
	err = __register_cpu_notifier(&dummy_timer_cpu_nb);
	if (err)
		goto out;

	/* We won't get a call on the boot CPU, so register immediately */
	if (num_possible_cpus() > 1)
		dummy_timer_setup();

out:
	cpu_notifier_register_done();
	return err;
}
early_initcall(dummy_timer_register);
