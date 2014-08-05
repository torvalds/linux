/*
 * Sample activity provider
 *
 * Copyright (C) ARM Limited 2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * See gator_events_mmapped.c for additional directions and
 * troubleshooting.
 *
 * For this sample to work these entries must be present in the
 * events.xml file. So create an events-threads.xml in the gator
 * daemon source directory with the following contents and rebuild
 * gatord:
 *
 * <category name="threads">
 *   <event counter="Linux_threads" title="Linux" name="Threads" class="activity" activity1="odd" activity_color1="0x000000ff" rendering_type="bar" average_selection="yes" average_cores="yes" percentage="yes" description="Linux syscall activity"/>
 * </category>
 */

#include <trace/events/sched.h>

#include "gator.h"

static ulong threads_enabled;
static ulong threads_key;
static ulong threads_cores;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
GATOR_DEFINE_PROBE(sched_switch, TP_PROTO(struct rq *rq, struct task_struct *prev, struct task_struct *next))
#else
GATOR_DEFINE_PROBE(sched_switch, TP_PROTO(struct task_struct *prev, struct task_struct *next))
#endif
{
	int cpu = get_physical_cpu();
	int pid = next->pid;
	if (pid == 0) {
		// idle
		gator_marshal_activity_switch(cpu, threads_key, 0, 0);
	} else if (pid & 1) {
		// odd
		gator_marshal_activity_switch(cpu, threads_key, 1, pid);
	} else {
		// even
		//gator_marshal_activity_switch(cpu, threads_key, 2, current->pid);
		// Multiple activities are not yet supported so emit idle
		gator_marshal_activity_switch(cpu, threads_key, 0, 0);
	}
}

// Adds Linux_threads directory and enabled, key, and cores files to /dev/gator/events
static int gator_events_threads_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;

	dir = gatorfs_mkdir(sb, root, "Linux_threads");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &threads_enabled);
	gatorfs_create_ro_ulong(sb, dir, "key", &threads_key);
	// Number of cores associated with this activity
	gatorfs_create_ro_ulong(sb, dir, "cores", &threads_cores);

	return 0;
}

static int gator_events_threads_start(void)
{
	int cpu;

	if (threads_enabled) {
		preempt_disable();
		for (cpu = 0; cpu < nr_cpu_ids; ++cpu) {
			gator_marshal_activity_switch(cpu, threads_key, 0, 0);
		}
		preempt_enable();

		if (GATOR_REGISTER_TRACE(sched_switch)) {
			goto fail_sched_switch;
		}
	}

	return 0;

fail_sched_switch:
	return -1;
}

static void gator_events_threads_stop(void)
{
	if (threads_enabled) {
		GATOR_UNREGISTER_TRACE(sched_switch);
	}

	threads_enabled = 0;
}

static struct gator_interface gator_events_threads_interface = {
	.create_files = gator_events_threads_create_files,
	.start = gator_events_threads_start,
	.stop = gator_events_threads_stop,
};

// Must not be static. Ensure that this init function is added to GATOR_EVENTS_LIST in gator_main.c
int __init gator_events_threads_init(void)
{
	threads_enabled = 0;
	threads_key = gator_events_get_key();
	threads_cores = nr_cpu_ids;

	return gator_events_install(&gator_events_threads_interface);
}
