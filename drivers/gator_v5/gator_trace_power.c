/**
 * Copyright (C) ARM Limited 2011-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/cpufreq.h>
#include <trace/events/power.h>

// cpu_frequency and cpu_idle trace points were introduced in Linux kernel v2.6.38
// the now deprecated power_frequency trace point was available prior to 2.6.38, but only for x86
#if GATOR_CPU_FREQ_SUPPORT
enum {
	POWER_CPU_FREQ,
	POWER_CPU_IDLE,
	POWER_TOTAL
};

static DEFINE_PER_CPU(ulong, idle_prev_state);
static ulong power_cpu_enabled[POWER_TOTAL];
static ulong power_cpu_key[POWER_TOTAL];

static int gator_trace_power_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;

	// cpu_frequency
	dir = gatorfs_mkdir(sb, root, "Linux_power_cpu_freq");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &power_cpu_enabled[POWER_CPU_FREQ]);
	gatorfs_create_ro_ulong(sb, dir, "key", &power_cpu_key[POWER_CPU_FREQ]);

	// cpu_idle
	dir = gatorfs_mkdir(sb, root, "Linux_power_cpu_idle");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &power_cpu_enabled[POWER_CPU_IDLE]);
	gatorfs_create_ro_ulong(sb, dir, "key", &power_cpu_key[POWER_CPU_IDLE]);

	return 0;
}

// 'cpu' may not equal smp_processor_id(), i.e. may not be running on the core that is having the freq/idle state change
GATOR_DEFINE_PROBE(cpu_frequency, TP_PROTO(unsigned int frequency, unsigned int cpu))
{
	marshal_event_single(cpu, power_cpu_key[POWER_CPU_FREQ], frequency * 1000);
}

#define WFI_ACTIVE_THRESHOLD 2  // may vary on platform/OS
#define WFI_EXIT 0
#define WFI_ENTER 1
GATOR_DEFINE_PROBE(cpu_idle, TP_PROTO(unsigned int state, unsigned int cpu))
{
	// the streamline engine treats all counter values as unsigned
	if (state & 0x80000000) {
		state = 0;
	}

	if (state == per_cpu(idle_prev_state, cpu)) {
		return;
	}

	if (state < WFI_ACTIVE_THRESHOLD && per_cpu(idle_prev_state, cpu) >= WFI_ACTIVE_THRESHOLD) {
		// transition from wfi to non-wfi
		marshal_wfi(cpu, WFI_EXIT);
	} else if (state >= WFI_ACTIVE_THRESHOLD && per_cpu(idle_prev_state, cpu) < WFI_ACTIVE_THRESHOLD) {
		// transition from non-wfi to wfi
		marshal_wfi(cpu, WFI_ENTER);
	}

	per_cpu(idle_prev_state, cpu) = state;

	if (power_cpu_enabled[POWER_CPU_IDLE]) {
		marshal_event_single(cpu, power_cpu_key[POWER_CPU_IDLE], state);
	}
}

static void gator_trace_power_online(void)
{
	int cpu = smp_processor_id();
	if (power_cpu_enabled[POWER_CPU_FREQ]) {
		marshal_event_single(cpu, power_cpu_key[POWER_CPU_FREQ], cpufreq_quick_get(cpu) * 1000);
	}
}

static void gator_trace_power_offline(void)
{
	// Set frequency to zero on an offline
	int cpu = smp_processor_id();
	if (power_cpu_enabled[POWER_CPU_FREQ]) {
		marshal_event_single(cpu, power_cpu_key[POWER_CPU_FREQ], 0);
	}
}

static int gator_trace_power_start(void)
{
	int cpu;

	// register tracepoints
	if (power_cpu_enabled[POWER_CPU_FREQ])
		if (GATOR_REGISTER_TRACE(cpu_frequency))
			goto fail_cpu_frequency_exit;

	// Always register for cpu:idle for detecting WFI, independent of power_cpu_enabled[POWER_CPU_IDLE]
	if (GATOR_REGISTER_TRACE(cpu_idle))
		goto fail_cpu_idle_exit;
	pr_debug("gator: registered power event tracepoints\n");

	for_each_present_cpu(cpu) {
		per_cpu(idle_prev_state, cpu) = 0;
	}

	return 0;

	// unregister tracepoints on error
fail_cpu_idle_exit:
	if (power_cpu_enabled[POWER_CPU_FREQ])
		GATOR_UNREGISTER_TRACE(cpu_frequency);
fail_cpu_frequency_exit:
	pr_err("gator: power event tracepoints failed to activate, please verify that tracepoints are enabled in the linux kernel\n");

	return -1;
}

static void gator_trace_power_stop(void)
{
	int i;

	if (power_cpu_enabled[POWER_CPU_FREQ])
		GATOR_UNREGISTER_TRACE(cpu_frequency);
	GATOR_UNREGISTER_TRACE(cpu_idle);
	pr_debug("gator: unregistered power event tracepoints\n");

	for (i = 0; i < POWER_TOTAL; i++) {
		power_cpu_enabled[i] = 0;
	}
}

void gator_trace_power_init(void)
{
	int i;
	for (i = 0; i < POWER_TOTAL; i++) {
		power_cpu_enabled[i] = 0;
		power_cpu_key[i] = gator_events_get_key();
	}
}
#else
static int gator_trace_power_create_files(struct super_block *sb, struct dentry *root) {return 0;}
static void gator_trace_power_online(void) {}
static void gator_trace_power_offline(void) {}
static int gator_trace_power_start(void) {return 0;}
static void gator_trace_power_stop(void) {}
void gator_trace_power_init(void) {}
#endif
