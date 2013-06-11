/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <trace/events/sched.h>
#include "gator.h"

#define SCHED_SWITCH			1
#define SCHED_PROCESS_EXIT		2

#define TASK_MAP_ENTRIES		1024	/* must be power of 2 */
#define TASK_MAX_COLLISIONS		2

enum {
	STATE_WAIT_ON_OTHER = 0,
	STATE_CONTENTION,
	STATE_WAIT_ON_IO,
	CPU_WAIT_TOTAL
};

static DEFINE_PER_CPU(uint64_t *, taskname_keys);
static DEFINE_PER_CPU(int, collecting);

// this array is never read as the cpu wait charts are derived counters
// the files are needed, nonetheless, to show that these counters are available
static ulong cpu_wait_enabled[CPU_WAIT_TOTAL];
static ulong sched_cpu_key[CPU_WAIT_TOTAL];

static int sched_trace_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;

	// CPU Wait - Contention
	dir = gatorfs_mkdir(sb, root, "Linux_cpu_wait_contention");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &cpu_wait_enabled[STATE_CONTENTION]);
	gatorfs_create_ro_ulong(sb, dir, "key", &sched_cpu_key[STATE_CONTENTION]);

	// CPU Wait - I/O
	dir = gatorfs_mkdir(sb, root, "Linux_cpu_wait_io");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &cpu_wait_enabled[STATE_WAIT_ON_IO]);
	gatorfs_create_ro_ulong(sb, dir, "key", &sched_cpu_key[STATE_WAIT_ON_IO]);

	return 0;
}

void emit_pid_name(struct task_struct *task)
{
	bool found = false;
	char taskcomm[TASK_COMM_LEN + 3];
	unsigned long x, cpu = get_physical_cpu();
	uint64_t *keys = &(per_cpu(taskname_keys, cpu)[(task->pid & 0xFF) * TASK_MAX_COLLISIONS]);
	uint64_t value;

	value = gator_chksum_crc32(task->comm);
	value = (value << 32) | (uint32_t)task->pid;

	// determine if the thread name was emitted already
	for (x = 0; x < TASK_MAX_COLLISIONS; x++) {
		if (keys[x] == value) {
			found = true;
			break;
		}
	}

	if (!found) {
		// shift values, new value always in front
		uint64_t oldv, newv = value;
		for (x = 0; x < TASK_MAX_COLLISIONS; x++) {
			oldv = keys[x];
			keys[x] = newv;
			newv = oldv;
		}

		// emit pid names, cannot use get_task_comm, as it's not exported on all kernel versions
		if (strlcpy(taskcomm, task->comm, TASK_COMM_LEN) == TASK_COMM_LEN - 1) {
			// append ellipses if task->comm has length of TASK_COMM_LEN - 1
			strcat(taskcomm, "...");
		}

		marshal_thread_name(task->pid, taskcomm);
	}
}

static void collect_counters(void)
{
	int *buffer, len, cpu = get_physical_cpu();
	long long *buffer64;
	struct gator_interface *gi;
	u64 time;

	time = gator_get_time();
	if (marshal_event_header(time)) {
		list_for_each_entry(gi, &gator_events, list) {
			if (gi->read) {
				len = gi->read(&buffer);
				marshal_event(len, buffer);
			} else if (gi->read64) {
				len = gi->read64(&buffer64);
				marshal_event64(len, buffer64);
			}
		}
		// Only check after writing all counters so that time and corresponding counters appear in the same frame
		buffer_check(cpu, BLOCK_COUNTER_BUF, time);

#if GATOR_LIVE
		// Commit buffers on timeout
		if (gator_live_rate > 0 && time >= per_cpu(gator_buffer_commit_time, cpu)) {
			static const int buftypes[] = { COUNTER_BUF, BLOCK_COUNTER_BUF, SCHED_TRACE_BUF };
			int i;
			for (i = 0; i < sizeof(buftypes)/sizeof(buftypes[0]); ++i) {
				gator_commit_buffer(cpu, buftypes[i], time);
			}
		}
#endif
	}
}

static void probe_sched_write(int type, struct task_struct *task, struct task_struct *old_task)
{
	int cookie = 0, state = 0;
	int cpu = get_physical_cpu();
	int tgid = task->tgid;
	int pid = task->pid;

	if (type == SCHED_SWITCH) {
		// do as much work as possible before disabling interrupts
		cookie = get_exec_cookie(cpu, task);
		emit_pid_name(task);
		if (old_task->state == TASK_RUNNING) {
			state = STATE_CONTENTION;
		} else if (old_task->in_iowait) {
			state = STATE_WAIT_ON_IO;
		} else {
			state = STATE_WAIT_ON_OTHER;
		}

		per_cpu(collecting, cpu) = 1;
		collect_counters();
		per_cpu(collecting, cpu) = 0;
	}

	// marshal_sched_trace() disables interrupts as the free may trigger while switch is writing to the buffer; disabling preemption is not sufficient
	// is disable interrupts necessary now that exit is used instead of free?
	if (type == SCHED_SWITCH) {
		marshal_sched_trace_switch(tgid, pid, cookie, state);
	} else {
		marshal_sched_trace_exit(tgid, pid);
	}
}

// special case used during a suspend of the system
static void trace_sched_insert_idle(void)
{
	marshal_sched_trace_switch(0, 0, 0, 0);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
GATOR_DEFINE_PROBE(sched_switch, TP_PROTO(struct rq *rq, struct task_struct *prev, struct task_struct *next))
#else
GATOR_DEFINE_PROBE(sched_switch, TP_PROTO(struct task_struct *prev, struct task_struct *next))
#endif
{
	probe_sched_write(SCHED_SWITCH, next, prev);
}

GATOR_DEFINE_PROBE(sched_process_exit, TP_PROTO(struct task_struct *p))
{
	probe_sched_write(SCHED_PROCESS_EXIT, p, 0);
}

static void do_nothing(void *info)
{
	// Intentionally do nothing
	(void)info;
}

static int register_scheduler_tracepoints(void)
{
	// register tracepoints
	if (GATOR_REGISTER_TRACE(sched_switch))
		goto fail_sched_switch;
	if (GATOR_REGISTER_TRACE(sched_process_exit))
		goto fail_sched_process_exit;
	pr_debug("gator: registered tracepoints\n");

	// Now that the scheduler tracepoint is registered, force a context switch
	// on all cpus to capture what is currently running.
	on_each_cpu(do_nothing, NULL, 0);

	return 0;

	// unregister tracepoints on error
fail_sched_process_exit:
	GATOR_UNREGISTER_TRACE(sched_switch);
fail_sched_switch:
	pr_err("gator: tracepoints failed to activate, please verify that tracepoints are enabled in the linux kernel\n");

	return -1;
}

int gator_trace_sched_start(void)
{
	int cpu, size;

	for_each_present_cpu(cpu) {
		size = TASK_MAP_ENTRIES * TASK_MAX_COLLISIONS * sizeof(uint64_t);
		per_cpu(taskname_keys, cpu) = (uint64_t *)kmalloc(size, GFP_KERNEL);
		if (!per_cpu(taskname_keys, cpu))
			return -1;
		memset(per_cpu(taskname_keys, cpu), 0, size);
	}

	return register_scheduler_tracepoints();
}

void gator_trace_sched_offline(void)
{
	trace_sched_insert_idle();
}

static void unregister_scheduler_tracepoints(void)
{
	GATOR_UNREGISTER_TRACE(sched_switch);
	GATOR_UNREGISTER_TRACE(sched_process_exit);
	pr_debug("gator: unregistered tracepoints\n");
}

void gator_trace_sched_stop(void)
{
	int cpu;
	unregister_scheduler_tracepoints();

	for_each_present_cpu(cpu) {
		kfree(per_cpu(taskname_keys, cpu));
	}
}

void gator_trace_sched_init(void)
{
	int i;
	for (i = 0; i < CPU_WAIT_TOTAL; i++) {
		cpu_wait_enabled[i] = 0;
		sched_cpu_key[i] = gator_events_get_key();
	}
}
