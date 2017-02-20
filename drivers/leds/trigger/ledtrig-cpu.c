/*
 * ledtrig-cpu.c - LED trigger based on CPU activity
 *
 * This LED trigger will be registered for each possible CPU and named as
 * cpu0, cpu1, cpu2, cpu3, etc.
 *
 * It can be bound to any LED just like other triggers using either a
 * board file or via sysfs interface.
 *
 * An API named ledtrig_cpu is exported for any user, who want to add CPU
 * activity indication in their code
 *
 * Copyright 2011 Linus Walleij <linus.walleij@linaro.org>
 * Copyright 2011 - 2012 Bryan Wu <bryan.wu@canonical.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/percpu.h>
#include <linux/syscore_ops.h>
#include <linux/rwsem.h>
#include <linux/cpu.h>
#include "../leds.h"

#define MAX_NAME_LEN	8

struct led_trigger_cpu {
	char name[MAX_NAME_LEN];
	struct led_trigger *_trig;
};

static DEFINE_PER_CPU(struct led_trigger_cpu, cpu_trig);

/**
 * ledtrig_cpu - emit a CPU event as a trigger
 * @evt: CPU event to be emitted
 *
 * Emit a CPU event on a CPU core, which will trigger a
 * bound LED to turn on or turn off.
 */
void ledtrig_cpu(enum cpu_led_event ledevt)
{
	struct led_trigger_cpu *trig = this_cpu_ptr(&cpu_trig);

	/* Locate the correct CPU LED */
	switch (ledevt) {
	case CPU_LED_IDLE_END:
	case CPU_LED_START:
		/* Will turn the LED on, max brightness */
		led_trigger_event(trig->_trig, LED_FULL);
		break;

	case CPU_LED_IDLE_START:
	case CPU_LED_STOP:
	case CPU_LED_HALTED:
		/* Will turn the LED off */
		led_trigger_event(trig->_trig, LED_OFF);
		break;

	default:
		/* Will leave the LED as it is */
		break;
	}
}
EXPORT_SYMBOL(ledtrig_cpu);

static int ledtrig_cpu_syscore_suspend(void)
{
	ledtrig_cpu(CPU_LED_STOP);
	return 0;
}

static void ledtrig_cpu_syscore_resume(void)
{
	ledtrig_cpu(CPU_LED_START);
}

static void ledtrig_cpu_syscore_shutdown(void)
{
	ledtrig_cpu(CPU_LED_HALTED);
}

static struct syscore_ops ledtrig_cpu_syscore_ops = {
	.shutdown	= ledtrig_cpu_syscore_shutdown,
	.suspend	= ledtrig_cpu_syscore_suspend,
	.resume		= ledtrig_cpu_syscore_resume,
};

static int ledtrig_online_cpu(unsigned int cpu)
{
	ledtrig_cpu(CPU_LED_START);
	return 0;
}

static int ledtrig_prepare_down_cpu(unsigned int cpu)
{
	ledtrig_cpu(CPU_LED_STOP);
	return 0;
}

static int __init ledtrig_cpu_init(void)
{
	int cpu;
	int ret;

	/* Supports up to 9999 cpu cores */
	BUILD_BUG_ON(CONFIG_NR_CPUS > 9999);

	/*
	 * Registering CPU led trigger for each CPU core here
	 * ignores CPU hotplug, but after this CPU hotplug works
	 * fine with this trigger.
	 */
	for_each_possible_cpu(cpu) {
		struct led_trigger_cpu *trig = &per_cpu(cpu_trig, cpu);

		snprintf(trig->name, MAX_NAME_LEN, "cpu%d", cpu);

		led_trigger_register_simple(trig->name, &trig->_trig);
	}

	register_syscore_ops(&ledtrig_cpu_syscore_ops);

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "leds/trigger:starting",
				ledtrig_online_cpu, ledtrig_prepare_down_cpu);
	if (ret < 0)
		pr_err("CPU hotplug notifier for ledtrig-cpu could not be registered: %d\n",
		       ret);

	pr_info("ledtrig-cpu: registered to indicate activity on CPUs\n");

	return 0;
}
device_initcall(ledtrig_cpu_init);
