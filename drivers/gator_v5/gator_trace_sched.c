/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
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

#define TASK_MAP_ENTRIES		1024		/* must be power of 2 */
#define TASK_MAX_COLLISIONS		2

static DEFINE_PER_CPU(uint64_t *, taskname_keys);

enum {
	STATE_WAIT_ON_OTHER = 0,
	STATE_CONTENTION,
	STATE_WAIT_ON_IO,
	STATE_WAIT_ON_MUTEX,
};

void emit_pid_name(struct task_struct* task)
{
	bool found = false;
	char taskcomm[TASK_COMM_LEN + 3];
	unsigned long x, cpu = smp_processor_id();
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

		marshal_pid_name(task->pid, taskcomm);
	}
}


static void collect_counters(void)
{
	int *buffer, len;
	long long *buffer64;
	struct gator_interface *gi;

	if (marshal_event_header()) {
		list_for_each_entry(gi, &gator_events, list) {
			if (gi->read) {
				len = gi->read(&buffer);
				marshal_event(len, buffer);
			} else if (gi->read64) {
				len = gi->read64(&buffer64);
				marshal_event64(len, buffer64);
			}
		}
	}
}

static void probe_sched_write(int type, struct task_struct* task, struct task_struct* old_task)
{
	int cookie = 0, state = 0;
	int cpu = smp_processor_id();
	int pid = task->pid;
	int tgid = task->tgid;

	if (type == SCHED_SWITCH) {
		// do as much work as possible before disabling interrupts
		cookie = get_exec_cookie(cpu, BACKTRACE_BUF, task);
		emit_pid_name(task);
		if (old_task->state == TASK_RUNNING) {
			state = STATE_CONTENTION;
		} else if (old_task->in_iowait) {
			state = STATE_WAIT_ON_IO;
#ifdef CONFIG_DEBUG_MUTEXES
		} else if (old_task->blocked_on) {
			state = STATE_WAIT_ON_MUTEX;
#endif
		} else {
			state = STATE_WAIT_ON_OTHER;
		}

		collect_counters();
	}

	// marshal_sched_trace() disables interrupts as the free may trigger while switch is writing to the buffer; disabling preemption is not sufficient
	// is disable interrupts necessary now that exit is used instead of free?
	marshal_sched_trace(type, pid, tgid, cookie, state);
}

// special case used during a suspend of the system
static void trace_sched_insert_idle(void)
{
	marshal_sched_trace(SCHED_SWITCH, 0, 0, 0, 0);
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

static int register_scheduler_tracepoints(void) {
	// register tracepoints
	if (GATOR_REGISTER_TRACE(sched_switch))
		goto fail_sched_switch;
	if (GATOR_REGISTER_TRACE(sched_process_exit))
		goto fail_sched_process_exit;
	pr_debug("gator: registered tracepoints\n");

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
		per_cpu(taskname_keys, cpu) = (uint64_t*)kmalloc(size, GFP_KERNEL);
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
