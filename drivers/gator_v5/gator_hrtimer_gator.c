/**
 * Copyright (C) ARM Limited 2011-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

// gator_hrtimer_perf.c is used if perf is supported
//   update, gator_hrtimer_gator.c always used until issues resolved with perf hrtimers
#if 1

void (*callback)(void);
DEFINE_PER_CPU(struct hrtimer, percpu_hrtimer);
DEFINE_PER_CPU(int, hrtimer_is_active);
static ktime_t profiling_interval;
static void gator_hrtimer_online(int cpu);
static void gator_hrtimer_offline(int cpu);

static enum hrtimer_restart gator_hrtimer_notify(struct hrtimer *hrtimer)
{
	hrtimer_forward_now(hrtimer, profiling_interval);
	(*callback)();
	return HRTIMER_RESTART;
}

static void gator_hrtimer_switch_cpus_online(void *unused)
{
	gator_hrtimer_online(smp_processor_id());
}

static void gator_hrtimer_online(int cpu)
{
	struct hrtimer *hrtimer = &per_cpu(percpu_hrtimer, cpu);

	if (cpu != smp_processor_id()) {
		smp_call_function_single(cpu, gator_hrtimer_switch_cpus_online, NULL, 1);
		return;
	}

	if (per_cpu(hrtimer_is_active, cpu) || profiling_interval.tv64 == 0)
		return;

	per_cpu(hrtimer_is_active, cpu) = 1;
	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer->function = gator_hrtimer_notify;
	hrtimer_start(hrtimer, profiling_interval, HRTIMER_MODE_REL_PINNED);
}

static void gator_hrtimer_switch_cpus_offline(void *unused)
{
	gator_hrtimer_offline(smp_processor_id());
}

static void gator_hrtimer_offline(int cpu)
{
	struct hrtimer *hrtimer = &per_cpu(percpu_hrtimer, cpu);

	if (cpu != smp_processor_id()) {
		smp_call_function_single(cpu, gator_hrtimer_switch_cpus_offline, NULL, 1);
		return;
	}

	if (!per_cpu(hrtimer_is_active, cpu))
		return;

	per_cpu(hrtimer_is_active, cpu) = 0;
	hrtimer_cancel(hrtimer);
}

static int gator_hrtimer_init(int interval, void (*func)(void))
{
	int cpu;

	(callback) = (func);

	for_each_present_cpu(cpu) {
		per_cpu(hrtimer_is_active, cpu) = 0;
	}

	// calculate profiling interval
	if (interval > 0) {
		profiling_interval = ns_to_ktime(1000000000UL / interval);
	} else {
		profiling_interval.tv64 = 0;
	}

	return 0;
}

static void gator_hrtimer_shutdown(void)
{
	/* empty */
}

#endif
