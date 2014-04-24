/**
 * Copyright (C) ARM Limited 2012-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define NEWLINE_CANARY \
	/* Unix */ \
	"1\n" \
	/* Windows */ \
	"2\r\n" \
	/* Mac OS */ \
	"3\r" \
	/* RISC OS */ \
	"4\n\r" \
	/* Add another character so the length isn't 0x0a bytes */ \
	"5"

#ifdef MALI_SUPPORT
#include "gator_events_mali_common.h"
#endif

static void marshal_summary(long long timestamp, long long uptime, long long monotonic_delta, const char * uname)
{
	unsigned long flags;
	int cpu = 0;

	local_irq_save(flags);
	gator_buffer_write_packed_int(cpu, SUMMARY_BUF, MESSAGE_SUMMARY);
	gator_buffer_write_string(cpu, SUMMARY_BUF, NEWLINE_CANARY);
	gator_buffer_write_packed_int64(cpu, SUMMARY_BUF, timestamp);
	gator_buffer_write_packed_int64(cpu, SUMMARY_BUF, uptime);
	gator_buffer_write_packed_int64(cpu, SUMMARY_BUF, monotonic_delta);
	gator_buffer_write_string(cpu, SUMMARY_BUF, "uname");
	gator_buffer_write_string(cpu, SUMMARY_BUF, uname);
#if GATOR_IKS_SUPPORT
	gator_buffer_write_string(cpu, SUMMARY_BUF, "iks");
	gator_buffer_write_string(cpu, SUMMARY_BUF, "");
#endif
	// Let Streamline know which GPU is used so that it can label the GPU Activity appropriately. This is a temporary fix, to be improved in a future release.
#ifdef MALI_SUPPORT
	gator_buffer_write_string(cpu, SUMMARY_BUF, "mali_type");
#if (MALI_SUPPORT == MALI_4xx)
	gator_buffer_write_string(cpu, SUMMARY_BUF, "4xx");
#elif (MALI_SUPPORT == MALI_T6xx)
	gator_buffer_write_string(cpu, SUMMARY_BUF, "6xx");
#else
	gator_buffer_write_string(cpu, SUMMARY_BUF, "unknown");
#endif
#endif
	gator_buffer_write_string(cpu, SUMMARY_BUF, "");
	// Commit the buffer now so it can be one of the first frames read by Streamline
	local_irq_restore(flags);
	gator_commit_buffer(cpu, SUMMARY_BUF, gator_get_time());
}

static bool marshal_cookie_header(const char *text)
{
	int cpu = get_physical_cpu();
	return buffer_check_space(cpu, NAME_BUF, strlen(text) + 3 * MAXSIZE_PACK32);
}

static void marshal_cookie(int cookie, const char *text)
{
	int cpu = get_physical_cpu();
	// buffer_check_space already called by marshal_cookie_header
	gator_buffer_write_packed_int(cpu, NAME_BUF, MESSAGE_COOKIE);
	gator_buffer_write_packed_int(cpu, NAME_BUF, cookie);
	gator_buffer_write_string(cpu, NAME_BUF, text);
	buffer_check(cpu, NAME_BUF, gator_get_time());
}

static void marshal_thread_name(int pid, char *name)
{
	unsigned long flags, cpu;
	u64 time;
	local_irq_save(flags);
	cpu = get_physical_cpu();
	time = gator_get_time();
	if (buffer_check_space(cpu, NAME_BUF, TASK_COMM_LEN + 3 * MAXSIZE_PACK32 + MAXSIZE_PACK64)) {
		gator_buffer_write_packed_int(cpu, NAME_BUF, MESSAGE_THREAD_NAME);
		gator_buffer_write_packed_int64(cpu, NAME_BUF, time);
		gator_buffer_write_packed_int(cpu, NAME_BUF, pid);
		gator_buffer_write_string(cpu, NAME_BUF, name);
	}
	local_irq_restore(flags);
	buffer_check(cpu, NAME_BUF, time);
}

static void marshal_link(int cookie, int tgid, int pid)
{
	unsigned long cpu = get_physical_cpu(), flags;
	u64 time;

	local_irq_save(flags);
	time = gator_get_time();
	if (buffer_check_space(cpu, NAME_BUF, MAXSIZE_PACK64 + 5 * MAXSIZE_PACK32)) {
		gator_buffer_write_packed_int(cpu, NAME_BUF, MESSAGE_LINK);
		gator_buffer_write_packed_int64(cpu, NAME_BUF, time);
		gator_buffer_write_packed_int(cpu, NAME_BUF, cookie);
		gator_buffer_write_packed_int(cpu, NAME_BUF, tgid);
		gator_buffer_write_packed_int(cpu, NAME_BUF, pid);
	}
	local_irq_restore(flags);
	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, NAME_BUF, time);
}

static bool marshal_backtrace_header(int exec_cookie, int tgid, int pid, u64 time)
{
	int cpu = get_physical_cpu();
	if (!buffer_check_space(cpu, BACKTRACE_BUF, MAXSIZE_PACK64 + 5 * MAXSIZE_PACK32 + gator_backtrace_depth * 2 * MAXSIZE_PACK32)) {
		// Check and commit; commit is set to occur once buffer is 3/4 full
		buffer_check(cpu, BACKTRACE_BUF, time);

		return false;
	}

	gator_buffer_write_packed_int64(cpu, BACKTRACE_BUF, time);
	gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, exec_cookie);
	gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, tgid);
	gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, pid);

	return true;
}

static void marshal_backtrace(unsigned long address, int cookie, int in_kernel)
{
	int cpu = get_physical_cpu();
	if (cookie == 0 && !in_kernel) {
		cookie = UNRESOLVED_COOKIE;
	}
	gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, cookie);
	gator_buffer_write_packed_int64(cpu, BACKTRACE_BUF, address);
}

static void marshal_backtrace_footer(u64 time)
{
	int cpu = get_physical_cpu();
	gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, MESSAGE_END_BACKTRACE);

	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, BACKTRACE_BUF, time);
}

static bool marshal_event_header(u64 time)
{
	unsigned long flags, cpu = get_physical_cpu();
	bool retval = false;

	local_irq_save(flags);
	if (buffer_check_space(cpu, BLOCK_COUNTER_BUF, MAXSIZE_PACK32 + MAXSIZE_PACK64)) {
		gator_buffer_write_packed_int(cpu, BLOCK_COUNTER_BUF, 0);	// key of zero indicates a timestamp
		gator_buffer_write_packed_int64(cpu, BLOCK_COUNTER_BUF, time);
		retval = true;
	}
	local_irq_restore(flags);

	return retval;
}

static void marshal_event(int len, int *buffer)
{
	unsigned long i, flags, cpu = get_physical_cpu();

	if (len <= 0)
		return;

	// length must be even since all data is a (key, value) pair
	if (len & 0x1) {
		pr_err("gator: invalid counter data detected and discarded");
		return;
	}

	// events must be written in key,value pairs
	local_irq_save(flags);
	for (i = 0; i < len; i += 2) {
		if (!buffer_check_space(cpu, BLOCK_COUNTER_BUF, 2 * MAXSIZE_PACK32)) {
			break;
		}
		gator_buffer_write_packed_int(cpu, BLOCK_COUNTER_BUF, buffer[i]);
		gator_buffer_write_packed_int(cpu, BLOCK_COUNTER_BUF, buffer[i + 1]);
	}
	local_irq_restore(flags);
}

static void marshal_event64(int len, long long *buffer64)
{
	unsigned long i, flags, cpu = get_physical_cpu();

	if (len <= 0)
		return;

	// length must be even since all data is a (key, value) pair
	if (len & 0x1) {
		pr_err("gator: invalid counter data detected and discarded");
		return;
	}

	// events must be written in key,value pairs
	local_irq_save(flags);
	for (i = 0; i < len; i += 2) {
		if (!buffer_check_space(cpu, BLOCK_COUNTER_BUF, 2 * MAXSIZE_PACK64)) {
			break;
		}
		gator_buffer_write_packed_int64(cpu, BLOCK_COUNTER_BUF, buffer64[i]);
		gator_buffer_write_packed_int64(cpu, BLOCK_COUNTER_BUF, buffer64[i + 1]);
	}
	local_irq_restore(flags);
}

#if GATOR_CPU_FREQ_SUPPORT
static void marshal_event_single(int core, int key, int value)
{
	unsigned long flags, cpu;
	u64 time;

	local_irq_save(flags);
	cpu = get_physical_cpu();
	time = gator_get_time();
	if (buffer_check_space(cpu, COUNTER_BUF, MAXSIZE_PACK64 + 3 * MAXSIZE_PACK32)) {
		gator_buffer_write_packed_int64(cpu, COUNTER_BUF, time);
		gator_buffer_write_packed_int(cpu, COUNTER_BUF, core);
		gator_buffer_write_packed_int(cpu, COUNTER_BUF, key);
		gator_buffer_write_packed_int(cpu, COUNTER_BUF, value);
	}
	local_irq_restore(flags);
	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, COUNTER_BUF, time);
}
#endif

static void marshal_sched_gpu_start(int unit, int core, int tgid, int pid)
{
	unsigned long cpu = get_physical_cpu(), flags;
	u64 time;

	if (!per_cpu(gator_buffer, cpu)[GPU_TRACE_BUF])
		return;

	local_irq_save(flags);
	time = gator_get_time();
	if (buffer_check_space(cpu, GPU_TRACE_BUF, MAXSIZE_PACK64 + 5 * MAXSIZE_PACK32)) {
		gator_buffer_write_packed_int(cpu, GPU_TRACE_BUF, MESSAGE_GPU_START);
		gator_buffer_write_packed_int64(cpu, GPU_TRACE_BUF, time);
		gator_buffer_write_packed_int(cpu, GPU_TRACE_BUF, unit);
		gator_buffer_write_packed_int(cpu, GPU_TRACE_BUF, core);
		gator_buffer_write_packed_int(cpu, GPU_TRACE_BUF, tgid);
		gator_buffer_write_packed_int(cpu, GPU_TRACE_BUF, pid);
	}
	local_irq_restore(flags);
	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, GPU_TRACE_BUF, time);
}

static void marshal_sched_gpu_stop(int unit, int core)
{
	unsigned long cpu = get_physical_cpu(), flags;
	u64 time;

	if (!per_cpu(gator_buffer, cpu)[GPU_TRACE_BUF])
		return;

	local_irq_save(flags);
	time = gator_get_time();
	if (buffer_check_space(cpu, GPU_TRACE_BUF, MAXSIZE_PACK64 + 3 * MAXSIZE_PACK32)) {
		gator_buffer_write_packed_int(cpu, GPU_TRACE_BUF, MESSAGE_GPU_STOP);
		gator_buffer_write_packed_int64(cpu, GPU_TRACE_BUF, time);
		gator_buffer_write_packed_int(cpu, GPU_TRACE_BUF, unit);
		gator_buffer_write_packed_int(cpu, GPU_TRACE_BUF, core);
	}
	local_irq_restore(flags);
	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, GPU_TRACE_BUF, time);
}

static void marshal_sched_trace_start(int tgid, int pid, int cookie)
{
	unsigned long cpu = get_physical_cpu(), flags;
	u64 time;

	if (!per_cpu(gator_buffer, cpu)[SCHED_TRACE_BUF])
		return;

	local_irq_save(flags);
	time = gator_get_time();
	if (buffer_check_space(cpu, SCHED_TRACE_BUF, MAXSIZE_PACK64 + 5 * MAXSIZE_PACK32)) {
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, MESSAGE_SCHED_START);
		gator_buffer_write_packed_int64(cpu, SCHED_TRACE_BUF, time);
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, tgid);
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, pid);
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, cookie);
	}
	local_irq_restore(flags);
	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, SCHED_TRACE_BUF, time);
}

static void marshal_sched_trace_switch(int tgid, int pid, int cookie, int state)
{
	unsigned long cpu = get_physical_cpu(), flags;
	u64 time;

	if (!per_cpu(gator_buffer, cpu)[SCHED_TRACE_BUF])
		return;

	local_irq_save(flags);
	time = gator_get_time();
	if (buffer_check_space(cpu, SCHED_TRACE_BUF, MAXSIZE_PACK64 + 5 * MAXSIZE_PACK32)) {
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, MESSAGE_SCHED_SWITCH);
		gator_buffer_write_packed_int64(cpu, SCHED_TRACE_BUF, time);
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, tgid);
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, pid);
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, cookie);
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, state);
	}
	local_irq_restore(flags);
	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, SCHED_TRACE_BUF, time);
}

static void marshal_sched_trace_exit(int tgid, int pid)
{
	unsigned long cpu = get_physical_cpu(), flags;
	u64 time;

	if (!per_cpu(gator_buffer, cpu)[SCHED_TRACE_BUF])
		return;

	local_irq_save(flags);
	time = gator_get_time();
	if (buffer_check_space(cpu, SCHED_TRACE_BUF, MAXSIZE_PACK64 + 2 * MAXSIZE_PACK32)) {
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, MESSAGE_SCHED_EXIT);
		gator_buffer_write_packed_int64(cpu, SCHED_TRACE_BUF, time);
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, pid);
	}
	local_irq_restore(flags);
	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, SCHED_TRACE_BUF, time);
}

#if GATOR_CPU_FREQ_SUPPORT
static void marshal_idle(int core, int state)
{
	unsigned long flags, cpu;
	u64 time;

	local_irq_save(flags);
	cpu = get_physical_cpu();
	time = gator_get_time();
	if (buffer_check_space(cpu, IDLE_BUF, MAXSIZE_PACK64 + 2 * MAXSIZE_PACK32)) {
		gator_buffer_write_packed_int(cpu, IDLE_BUF, state);
		gator_buffer_write_packed_int64(cpu, IDLE_BUF, time);
		gator_buffer_write_packed_int(cpu, IDLE_BUF, core);
	}
	local_irq_restore(flags);
	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, IDLE_BUF, time);
}
#endif

#if defined(__arm__) || defined(__aarch64__)
static void marshal_core_name(const int core, const int cpuid, const char *name)
{
	int cpu = get_physical_cpu();
	unsigned long flags;
	local_irq_save(flags);
	if (buffer_check_space(cpu, SUMMARY_BUF, MAXSIZE_PACK32 + MAXSIZE_CORE_NAME)) {
		gator_buffer_write_packed_int(cpu, SUMMARY_BUF, MESSAGE_CORE_NAME);
		gator_buffer_write_packed_int(cpu, SUMMARY_BUF, core);
		gator_buffer_write_packed_int(cpu, SUMMARY_BUF, cpuid);
		gator_buffer_write_string(cpu, SUMMARY_BUF, name);
	}
	// Commit core names now so that they can show up in live
	local_irq_restore(flags);
	gator_commit_buffer(cpu, SUMMARY_BUF, gator_get_time());
}
#endif
