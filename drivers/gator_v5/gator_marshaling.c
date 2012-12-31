/**
 * Copyright (C) ARM Limited 2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

static void marshal_summary(long long timestamp, long long uptime) {
	int cpu = 0;
	gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, MESSAGE_SUMMARY);
	gator_buffer_write_packed_int64(cpu, BACKTRACE_BUF, timestamp);
	gator_buffer_write_packed_int64(cpu, BACKTRACE_BUF, uptime);
}

static bool marshal_cookie_header(char* text) {
	int cpu = smp_processor_id();
	return buffer_check_space(cpu, BACKTRACE_BUF, strlen(text) + 2 * MAXSIZE_PACK32);
}

static void marshal_cookie(int cookie, char* text) {
	int cpu = smp_processor_id();
	gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, MESSAGE_COOKIE);
	gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, cookie);
	gator_buffer_write_string(cpu, BACKTRACE_BUF, text);
}

static void marshal_pid_name(int pid, char* name) {
	unsigned long flags, cpu;
	local_irq_save(flags);
	cpu = smp_processor_id();
	if (buffer_check_space(cpu, BACKTRACE_BUF, TASK_COMM_LEN + 2 * MAXSIZE_PACK32 + MAXSIZE_PACK64)) {
		gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, MESSAGE_PID_NAME);
		gator_buffer_write_packed_int64(cpu, BACKTRACE_BUF, gator_get_time());
		gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, pid);
		gator_buffer_write_string(cpu, BACKTRACE_BUF, name);
	}
	local_irq_restore(flags);
}

static bool marshal_backtrace_header(int exec_cookie, int tgid, int pid, int inKernel) {
	int cpu = smp_processor_id();
	if (buffer_check_space(cpu, BACKTRACE_BUF, gator_backtrace_depth * 2 * MAXSIZE_PACK32)) {
		gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, MESSAGE_START_BACKTRACE);
		gator_buffer_write_packed_int64(cpu, BACKTRACE_BUF, gator_get_time());
		gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, exec_cookie);
		gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, tgid); 
		gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, pid);
		gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, inKernel);
		return true;
	}

	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, BACKTRACE_BUF);

	return false;
}

static void marshal_backtrace(int address, int cookie) {
	int cpu = smp_processor_id();
	gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, address);
	gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, cookie);
}

static void marshal_backtrace_footer(void) {
	int cpu = smp_processor_id();
	gator_buffer_write_packed_int(cpu, BACKTRACE_BUF, MESSAGE_END_BACKTRACE);

	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, BACKTRACE_BUF);
}

static bool marshal_event_header(void) {
	unsigned long flags, cpu = smp_processor_id();
	bool retval = false;
	
	local_irq_save(flags);
	if (buffer_check_space(cpu, COUNTER_BUF, MAXSIZE_PACK32 + MAXSIZE_PACK64)) {
		gator_buffer_write_packed_int(cpu, COUNTER_BUF, 0); // key of zero indicates a timestamp
		gator_buffer_write_packed_int64(cpu, COUNTER_BUF, gator_get_time());
		retval = true;
	}
	local_irq_restore(flags);

	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, COUNTER_BUF);

	return retval;
}

static void marshal_event(int len, int* buffer) {
	unsigned long i, flags, cpu = smp_processor_id();

	if (len <= 0)
		return;
	
	// length must be even since all data is a (key, value) pair
	if (len & 0x1) {
		pr_err("gator: invalid counter data detected and discarded");
		return;
	}

	// events must be written in key,value pairs
	for (i = 0; i < len; i += 2) {
		local_irq_save(flags);
		if (!buffer_check_space(cpu, COUNTER_BUF, MAXSIZE_PACK32 * 2)) {
			local_irq_restore(flags);
			break;
		}
		gator_buffer_write_packed_int(cpu, COUNTER_BUF, buffer[i]);
		gator_buffer_write_packed_int(cpu, COUNTER_BUF, buffer[i + 1]);
		local_irq_restore(flags);
	}

	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, COUNTER_BUF);
}

static void marshal_event64(int len, long long* buffer64) {
	unsigned long i, flags, cpu = smp_processor_id();

	if (len <= 0)
		return;
	
	// length must be even since all data is a (key, value) pair
	if (len & 0x1) {
		pr_err("gator: invalid counter data detected and discarded");
		return;
	}

	// events must be written in key,value pairs
	for (i = 0; i < len; i += 2) {
		local_irq_save(flags);
		if (!buffer_check_space(cpu, COUNTER_BUF, MAXSIZE_PACK64 * 2)) {
			local_irq_restore(flags);
			break;
		}
		gator_buffer_write_packed_int64(cpu, COUNTER_BUF, buffer64[i]);
		gator_buffer_write_packed_int64(cpu, COUNTER_BUF, buffer64[i + 1]);
		local_irq_restore(flags);
	}

	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, COUNTER_BUF);
}

#if GATOR_CPU_FREQ_SUPPORT
static void marshal_event_single(int core, int key, int value) {
	unsigned long flags, cpu;
	
	local_irq_save(flags);
	cpu = smp_processor_id();
	if (buffer_check_space(cpu, COUNTER2_BUF, MAXSIZE_PACK64 + MAXSIZE_PACK32 * 3)) {
		gator_buffer_write_packed_int64(cpu, COUNTER2_BUF, gator_get_time());
		gator_buffer_write_packed_int(cpu, COUNTER2_BUF, core);
		gator_buffer_write_packed_int(cpu, COUNTER2_BUF, key);
		gator_buffer_write_packed_int(cpu, COUNTER2_BUF, value);
	}
	local_irq_restore(flags);

	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, COUNTER2_BUF);
}
#endif

static void marshal_sched_gpu(int type, int unit, int core, int tgid, int pid) {
	unsigned long cpu = smp_processor_id(), flags;

	if (!per_cpu(gator_buffer, cpu)[GPU_TRACE_BUF])
		return;

	local_irq_save(flags);
	if (buffer_check_space(cpu, GPU_TRACE_BUF, MAXSIZE_PACK64 + 5 * MAXSIZE_PACK32)) {
		gator_buffer_write_packed_int(cpu, GPU_TRACE_BUF, type);
		gator_buffer_write_packed_int64(cpu, GPU_TRACE_BUF, gator_get_time());
		gator_buffer_write_packed_int(cpu, GPU_TRACE_BUF, unit);
		gator_buffer_write_packed_int(cpu, GPU_TRACE_BUF, core);
		gator_buffer_write_packed_int(cpu, GPU_TRACE_BUF, tgid);
		gator_buffer_write_packed_int(cpu, GPU_TRACE_BUF, pid);
	}
	local_irq_restore(flags);

	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, GPU_TRACE_BUF);
}

static void marshal_sched_trace(int type, int pid, int tgid, int cookie, int state) {
	unsigned long cpu = smp_processor_id(), flags;

	if (!per_cpu(gator_buffer, cpu)[SCHED_TRACE_BUF])
		return;

	local_irq_save(flags);
	if (buffer_check_space(cpu, SCHED_TRACE_BUF, MAXSIZE_PACK64 + 5 * MAXSIZE_PACK32)) {
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, type);
		gator_buffer_write_packed_int64(cpu, SCHED_TRACE_BUF, gator_get_time());
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, pid);
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, tgid);
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, cookie);
		gator_buffer_write_packed_int(cpu, SCHED_TRACE_BUF, state);
	}
	local_irq_restore(flags);

	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, SCHED_TRACE_BUF);
}

#if GATOR_CPU_FREQ_SUPPORT
static void marshal_wfi(int core, int state) {
	unsigned long flags, cpu;
	
	local_irq_save(flags);
	cpu = smp_processor_id();
	if (buffer_check_space(cpu, WFI_BUF, MAXSIZE_PACK64 + MAXSIZE_PACK32 * 2)) {
		gator_buffer_write_packed_int64(cpu, WFI_BUF, gator_get_time());
		gator_buffer_write_packed_int(cpu, WFI_BUF, core);
		gator_buffer_write_packed_int(cpu, WFI_BUF, state);
	}
	local_irq_restore(flags);

	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, WFI_BUF);
}
#endif

static void marshal_frame(int cpu, int buftype, int frame) {
	// add response type
	if (gator_response_type > 0) {
		gator_buffer_write_packed_int(cpu, buftype, gator_response_type);
	}

	// leave space for 4-byte unpacked length
	per_cpu(gator_buffer_write, cpu)[buftype] = (per_cpu(gator_buffer_write, cpu)[buftype] + 4) & gator_buffer_mask[buftype];

	// add frame type and core number
	gator_buffer_write_packed_int(cpu, buftype, frame);
	gator_buffer_write_packed_int(cpu, buftype, cpu);
}
