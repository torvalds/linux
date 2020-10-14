// SPDX-License-Identifier: GPL-2.0-only
/*
 * ledtrig-cpu.c - LED trigger based on CPU activity
 *
 * This LED trigger will be registered for first 8 CPUs and named
 * as cpu0..cpu7. There's additional trigger called cpu that
 * is on when any CPU is active.
 *
 * If you want support for arbitrary number of CPUs, make it one trigger,
 * with additional sysfs file selecting which CPU to watch.
 *
 * It can be bound to any LED just like other triggers using either a
 * board file or via sysfs interface.
 *
 * An API named ledtrig_cpu is exported for any user, who want to add CPU
 * activity indication in their code.
 *
 * Copyright 2011 Linus Walleij <linus.walleij@linaro.org>
 * Copyright 2011 - 2012 Bryan Wu <bryan.wu@canonical.com>
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
	bool is_active;
	char name[MAX_NAME_LEN];
	struct led_trigger *_trig;
};

static DEFINE_PER_CPU(struct led_trigger_cpu, cpu_trig);

static struct led_trigger *trig_cpu_all;
static atomic_t num_active_cpus = ATOMIC_INIT(0);

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
	bool is_active = trig->is_active;

	/* Locate the correct CPU LED */
	switch (ledevt) {
	case CPU_LED_IDLE_END:
	case CPU_LED_START:
		/* Will turn the LED on, max brightness */
		is_active = true;
		break;

	case CPU_LED_IDLE_START:
	case CPU_LED_STOP:
	case CPU_LED_HALTED:
		/* Will turn the LED off */
		is_active = false;
		break;

	default:
		/* Will leave the LED as it is */
		break;
	}

	if (is_active != trig->is_active) {
		unsigned int active_cpus;
		unsigned int total_cpus;

		/* Update trigger state */
		trig->is_active = is_active;
		atomic_add(is_active ? 1 : -1, &num_active_cpus);
		active_cpus = atomic_read(&num_active_cpus);
		total_cpus = num_present_cpus();

		led_trigger_event(trig->_trig,
			is_active ? LED_FULL : LED_OFF);


		led_trigger_event(trig_cpu_all,
			DIV_ROUND_UP(LED_FULL * active_cpus, total_cpus));

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
	 * Registering a trigger for all CPUs.
	 */
	led_trigger_register_simple("cpu", &trig_cpu_all);

	/*
	 * Registering CPU led trigger for each CPU core here
	 * ignores CPU hotplug, but after this CPU hotplug works
	 * fine with this trigger.
	 */
	for_each_possible_cpu(cpu) {
		struct led_trigger_cpu *trig = &per_cpu(cpu_trig, cpu);

		if (cpu >= 8)
			continue;

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
