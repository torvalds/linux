/**
 * Copyright (C) ARM Limited 2012-2013. All rights reserved.
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

static void marshal_summary(long long timestamp, long long uptime, const char * uname)
{
	unsigned long flags;
	int cpu = 0;

	local_irq_save(flags);
	gator_buffer_write_string(cpu, SUMMARY_BUF, NEWLINE_CANARY);
	gator_buffer_write_packed_int64(cpu, SUMMARY_BUF, timestamp);
	gator_buffer_write_packed_int64(cpu, SUMMARY_BUF, uptime);
	gator_buffer_write_string(cpu, SUMMARY_BUF, "uname");
	gator_buffer_write_string(cpu, SUMMARY_BUF, uname);
#if GATOR_IKS_SUPPORT
	gator_buffer_write_string(cpu, SUMMARY_BUF, "iks");
	gator_buffer_write_string(cpu, SUMMARY_BUF, "");
#endif
	gator_buffer_write_string(cpu, SUMMARY_BUF, "");
	// Commit the buffer now so it can be one of the first frames read by Streamline
	gator_commit_buffer(cpu, SUMMARY_BUF, gator_get_time());
	local_irq_restore(flags);
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
	buffer_check(cpu, NAME_BUF, time);
	local_irq_restore(flags);
}

static bool marshal_backtrace_header(int exec_cookie, int tgid, int pid, int inKernel)
{
	int cpu = get_physical_cpu();
	u64 time = gator_get_time();
	if (buffer_check_space(cpu, BACKTRACE_BUF, MAXSIZE_PACK64 + 5 * MAXSIZE_PACK32 + gator_backtrace_depth * 2 * MAXSIZE_PACK32)) {
		gator_buffer_write_packed_int64(cpu, BACKTRACE_BUF, time);
		gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, exec_cookie);
		gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, tgid);
		gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, pid);
		gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, inKernel);
		return true;
	}

	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, BACKTRACE_BUF, time);

	return false;
}

static void marshal_backtrace(unsigned long address, int cookie)
{
	int cpu = get_physical_cpu();
	gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, cookie);
	gator_buffer_write_packed_int64(cpu, BACKTRACE_BUF, address);
}

static void marshal_backtrace_footer(void)
{
	int cpu = get_physical_cpu();
	gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, MESSAGE_END_BACKTRACE);

	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, BACKTRACE_BUF, gator_get_time());
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
	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, COUNTER_BUF, time);
	local_irq_restore(flags);
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
	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, GPU_TRACE_BUF, time);
	local_irq_restore(flags);
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
	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, GPU_TRACE_BUF, time);
	local_irq_restore(flags);
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
	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, SCHED_TRACE_BUF, time);
	local_irq_restore(flags);
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
	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, SCHED_TRACE_BUF, time);
	local_irq_restore(flags);
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
	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, IDLE_BUF, time);
	local_irq_restore(flags);
}
#endif

static void marshal_frame(int cpu, int buftype)
{
	int frame;

	if (!per_cpu(gator_buffer, cpu)[buftype]) {
		return;
	}

	switch (buftype) {
	case SUMMARY_BUF:
		frame = FRAME_SUMMARY;
		break;
	case BACKTRACE_BUF:
		frame = FRAME_BACKTRACE;
		break;
	case NAME_BUF:
		frame = FRAME_NAME;
		break;
	case COUNTER_BUF:
		frame = FRAME_COUNTER;
		break;
	case BLOCK_COUNTER_BUF:
		frame = FRAME_BLOCK_COUNTER;
		break;
	case ANNOTATE_BUF:
		frame = FRAME_ANNOTATE;
		break;
	case SCHED_TRACE_BUF:
		frame = FRAME_SCHED_TRACE;
		break;
	case GPU_TRACE_BUF:
		frame = FRAME_GPU_TRACE;
		break;
	case IDLE_BUF:
		frame = FRAME_IDLE;
		break;
	default:
		frame = -1;
		break;
	}

	// add response type
	if (gator_response_type > 0) {
		gator_buffer_write_packed_int(cpu, buftype, gator_response_type);
	}

	// leave space for 4-byte unpacked length
	per_cpu(gator_buffer_write, cpu)[buftype] = (per_cpu(gator_buffer_write, cpu)[buftype] + sizeof(s32)) & gator_buffer_mask[buftype];

	// add frame type and core number
	gator_buffer_write_packed_int(cpu, buftype, frame);
	gator_buffer_write_packed_int(cpu, buftype, cpu);
}

#if defined(__arm__) || defined(__aarch64__)
static void marshal_core_name(const int cpuid, const char *name)
{
	int cpu = get_physical_cpu();
	unsigned long flags;
	local_irq_save(flags);
	if (buffer_check_space(cpu, NAME_BUF, MAXSIZE_PACK32 + MAXSIZE_CORE_NAME)) {
		gator_buffer_write_packed_int(cpu, NAME_BUF, HRTIMER_CORE_NAME);
		gator_buffer_write_packed_int(cpu, NAME_BUF, cpuid);
		gator_buffer_write_string(cpu, NAME_BUF, name);
	}
	buffer_check(cpu, NAME_BUF, gator_get_time());
	local_irq_restore(flags);
}
#endif
