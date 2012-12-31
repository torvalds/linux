/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include "gator.h"
#include <trace/events/sched.h>

#define SCHED_SWITCH	0
#define SCHED_TOTAL		(SCHED_SWITCH+1)

static ulong sched_switch_enabled;
static ulong sched_switch_key;
static DEFINE_PER_CPU(int[SCHED_TOTAL], schedCnt);
static DEFINE_PER_CPU(int[SCHED_TOTAL * 2], schedGet);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
GATOR_DEFINE_PROBE(sched_switch, TP_PROTO(struct rq *rq, struct task_struct *prev, struct task_struct *next))
#else
GATOR_DEFINE_PROBE(sched_switch, TP_PROTO(struct task_struct *prev, struct task_struct *next))
#endif
{
	unsigned long flags;

	// disable interrupts to synchronize with gator_events_sched_read()
	// spinlocks not needed since percpu buffers are used
	local_irq_save(flags);
	per_cpu(schedCnt, smp_processor_id())[SCHED_SWITCH]++;
	local_irq_restore(flags);
}

static int gator_events_sched_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;

	/* switch */
	dir = gatorfs_mkdir(sb, root, "Linux_sched_switch");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &sched_switch_enabled);
	gatorfs_create_ro_ulong(sb, dir, "key", &sched_switch_key);

	return 0;
}

static int gator_events_sched_start(void)
{
	// register tracepoints
	if (sched_switch_enabled)
		if (GATOR_REGISTER_TRACE(sched_switch))
			goto sched_switch_exit;
	pr_debug("gator: registered scheduler event tracepoints\n");

	return 0;

	// unregister tracepoints on error
sched_switch_exit:
	pr_err("gator: scheduler event tracepoints failed to activate, please verify that tracepoints are enabled in the linux kernel\n");

	return -1;
}

static void gator_events_sched_stop(void)
{
	if (sched_switch_enabled)
		GATOR_UNREGISTER_TRACE(sched_switch);
	pr_debug("gator: unregistered scheduler event tracepoints\n");

	sched_switch_enabled = 0;
}

static int gator_events_sched_read(int **buffer)
{
	unsigned long flags;
	int len, value;
	int cpu = smp_processor_id();

	len = 0;
	if (sched_switch_enabled) {
		local_irq_save(flags);
		value = per_cpu(schedCnt, cpu)[SCHED_SWITCH];
		per_cpu(schedCnt, cpu)[SCHED_SWITCH] = 0;
		local_irq_restore(flags);
		per_cpu(schedGet, cpu)[len++] = sched_switch_key;
		per_cpu(schedGet, cpu)[len++] = value;
	}

	if (buffer)
		*buffer = per_cpu(schedGet, cpu);

	return len;
}

static struct gator_interface gator_events_sched_interface = {
	.create_files = gator_events_sched_create_files,
	.start = gator_events_sched_start,
	.stop = gator_events_sched_stop,
	.read = gator_events_sched_read,
};

int gator_events_sched_init(void)
{
	sched_switch_enabled = 0;

	sched_switch_key = gator_events_get_key();

	return gator_events_install(&gator_events_sched_interface);
}
gator_events_init(gator_events_sched_init);
