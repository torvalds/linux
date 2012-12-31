/**
 * Copyright (C) ARM Limited 2011-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

// gator_hrtimer_gator.c is used if perf is not supported
//   update, gator_hrtimer_gator.c always used until issues resolved with perf hrtimers
#if 0

// Note: perf Cortex support added in 2.6.35 and PERF_COUNT_SW_CPU_CLOCK/hrtimer broken on 2.6.35 and 2.6.36
//       not relevant as this code is not active until 3.0.0, but wanted to document the issue

void (*callback)(void);
static int profiling_interval;
static DEFINE_PER_CPU(struct perf_event *, perf_hrtimer);
static DEFINE_PER_CPU(struct perf_event_attr *, perf_hrtimer_attr);

static void gator_hrtimer_shutdown(void);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
static void hrtimer_overflow_handler(struct perf_event *event, int unused, struct perf_sample_data *data, struct pt_regs *regs)
#else
static void hrtimer_overflow_handler(struct perf_event *event, struct perf_sample_data *data, struct pt_regs *regs)
#endif
{
	(*callback)();
}

static int gator_online_single_hrtimer(int cpu)
{
	if (per_cpu(perf_hrtimer, cpu) != 0 || per_cpu(perf_hrtimer_attr, cpu) == 0)
		return 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
	per_cpu(perf_hrtimer, cpu) = perf_event_create_kernel_counter(per_cpu(perf_hrtimer_attr, cpu), cpu, 0, hrtimer_overflow_handler);
#else
	per_cpu(perf_hrtimer, cpu) = perf_event_create_kernel_counter(per_cpu(perf_hrtimer_attr, cpu), cpu, 0, hrtimer_overflow_handler, 0);
#endif
	if (IS_ERR(per_cpu(perf_hrtimer, cpu))) {
		per_cpu(perf_hrtimer, cpu) = NULL;
		return -1;
	}

	if (per_cpu(perf_hrtimer, cpu)->state != PERF_EVENT_STATE_ACTIVE) {
		perf_event_release_kernel(per_cpu(perf_hrtimer, cpu));
		per_cpu(perf_hrtimer, cpu) = NULL;
		return -1;
	}

	return 0;
}

static void gator_hrtimer_online(int cpu)
{
	if (gator_online_single_hrtimer(cpu) < 0) {
		pr_debug("gator: unable to online the hrtimer on cpu%d\n", cpu);
	}
}

static void gator_hrtimer_offline(int cpu)
{
	if (per_cpu(perf_hrtimer, cpu)) {
		perf_event_release_kernel(per_cpu(perf_hrtimer, cpu));
		per_cpu(perf_hrtimer, cpu) = NULL;
	}
}

static int gator_hrtimer_init(int interval, void (*func)(void))
{
	u32 size = sizeof(struct perf_event_attr);
	int cpu;

	callback = func;

	// calculate profiling interval
	profiling_interval = 1000000000 / interval;

	for_each_present_cpu(cpu) {
		per_cpu(perf_hrtimer, cpu) = 0;
		per_cpu(perf_hrtimer_attr, cpu) = kmalloc(size, GFP_KERNEL);
		if (per_cpu(perf_hrtimer_attr, cpu) == 0) {
			gator_hrtimer_shutdown();
			return -1;
		}

		memset(per_cpu(perf_hrtimer_attr, cpu), 0, size);
		per_cpu(perf_hrtimer_attr, cpu)->type = PERF_TYPE_SOFTWARE;
		per_cpu(perf_hrtimer_attr, cpu)->size = size;
		per_cpu(perf_hrtimer_attr, cpu)->config = PERF_COUNT_SW_CPU_CLOCK;
		per_cpu(perf_hrtimer_attr, cpu)->sample_period = profiling_interval;
		per_cpu(perf_hrtimer_attr, cpu)->pinned = 1;
	}

	return 0;
}

static void gator_hrtimer_shutdown(void)
{
	int cpu;

	for_each_present_cpu(cpu) {
		if (per_cpu(perf_hrtimer_attr, cpu)) {
			kfree(per_cpu(perf_hrtimer_attr, cpu));
			per_cpu(perf_hrtimer_attr, cpu) = NULL;
		}
	}
}

#endif
