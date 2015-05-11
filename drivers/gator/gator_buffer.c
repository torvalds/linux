/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

static void marshal_frame(int cpu, int buftype)
{
	int frame;
	bool write_cpu;

	if (!per_cpu(gator_buffer, cpu)[buftype])
		return;

	switch (buftype) {
	case SUMMARY_BUF:
		write_cpu = false;
		frame = FRAME_SUMMARY;
		break;
	case BACKTRACE_BUF:
		write_cpu = true;
		frame = FRAME_BACKTRACE;
		break;
	case NAME_BUF:
		write_cpu = true;
		frame = FRAME_NAME;
		break;
	case COUNTER_BUF:
		write_cpu = false;
		frame = FRAME_COUNTER;
		break;
	case BLOCK_COUNTER_BUF:
		write_cpu = true;
		frame = FRAME_BLOCK_COUNTER;
		break;
	case ANNOTATE_BUF:
		write_cpu = false;
		frame = FRAME_ANNOTATE;
		break;
	case SCHED_TRACE_BUF:
		write_cpu = true;
		frame = FRAME_SCHED_TRACE;
		break;
	case IDLE_BUF:
		write_cpu = false;
		frame = FRAME_IDLE;
		break;
	case ACTIVITY_BUF:
		write_cpu = false;
		frame = FRAME_ACTIVITY;
		break;
	default:
		write_cpu = false;
		frame = -1;
		break;
	}

	/* add response type */
	if (gator_response_type > 0)
		gator_buffer_write_packed_int(cpu, buftype, gator_response_type);

	/* leave space for 4-byte unpacked length */
	per_cpu(gator_buffer_write, cpu)[buftype] = (per_cpu(gator_buffer_write, cpu)[buftype] + sizeof(s32)) & gator_buffer_mask[buftype];

	/* add frame type and core number */
	gator_buffer_write_packed_int(cpu, buftype, frame);
	if (write_cpu)
		gator_buffer_write_packed_int(cpu, buftype, cpu);
}

static int buffer_bytes_available(int cpu, int buftype)
{
	int remaining, filled;

	filled = per_cpu(gator_buffer_write, cpu)[buftype] - per_cpu(gator_buffer_read, cpu)[buftype];
	if (filled < 0)
		filled += gator_buffer_size[buftype];

	remaining = gator_buffer_size[buftype] - filled;

	if (per_cpu(buffer_space_available, cpu)[buftype])
		/* Give some extra room; also allows space to insert the overflow error packet */
		remaining -= 200;
	else
		/* Hysteresis, prevents multiple overflow messages */
		remaining -= 2000;

	return remaining;
}

static bool buffer_check_space(int cpu, int buftype, int bytes)
{
	int remaining = buffer_bytes_available(cpu, buftype);

	if (remaining < bytes)
		per_cpu(buffer_space_available, cpu)[buftype] = false;
	else
		per_cpu(buffer_space_available, cpu)[buftype] = true;

	return per_cpu(buffer_space_available, cpu)[buftype];
}

static int contiguous_space_available(int cpu, int buftype)
{
	int remaining = buffer_bytes_available(cpu, buftype);
	int contiguous = gator_buffer_size[buftype] - per_cpu(gator_buffer_write, cpu)[buftype];

	if (remaining < contiguous)
		return remaining;
	return contiguous;
}

static void gator_commit_buffer(int cpu, int buftype, u64 time)
{
	int type_length, commit, length, byte;
	unsigned long flags;

	if (!per_cpu(gator_buffer, cpu)[buftype])
		return;

	/* post-populate the length, which does not include the response type length nor the length itself, i.e. only the length of the payload */
	local_irq_save(flags);
	type_length = gator_response_type ? 1 : 0;
	commit = per_cpu(gator_buffer_commit, cpu)[buftype];
	length = per_cpu(gator_buffer_write, cpu)[buftype] - commit;
	if (length < 0)
		length += gator_buffer_size[buftype];
	length = length - type_length - sizeof(s32);

	if (length <= FRAME_HEADER_SIZE) {
		/* Nothing to write, only the frame header is present */
		local_irq_restore(flags);
		return;
	}

	for (byte = 0; byte < sizeof(s32); byte++)
		per_cpu(gator_buffer, cpu)[buftype][(commit + type_length + byte) & gator_buffer_mask[buftype]] = (length >> byte * 8) & 0xFF;

	per_cpu(gator_buffer_commit, cpu)[buftype] = per_cpu(gator_buffer_write, cpu)[buftype];

	if (gator_live_rate > 0) {
		while (time > per_cpu(gator_buffer_commit_time, cpu))
			per_cpu(gator_buffer_commit_time, cpu) += gator_live_rate;
	}

	marshal_frame(cpu, buftype);
	local_irq_restore(flags);

	/* had to delay scheduling work as attempting to schedule work during the context switch is illegal in kernel versions 3.5 and greater */
	if (per_cpu(in_scheduler_context, cpu)) {
#ifndef CONFIG_PREEMPT_RT_FULL
		/* mod_timer can not be used in interrupt context in RT-Preempt full */
		mod_timer(&gator_buffer_wake_up_timer, jiffies + 1);
#endif
	} else {
		up(&gator_buffer_wake_sem);
	}
}

static void buffer_check(int cpu, int buftype, u64 time)
{
	int filled = per_cpu(gator_buffer_write, cpu)[buftype] - per_cpu(gator_buffer_commit, cpu)[buftype];

	if (filled < 0)
		filled += gator_buffer_size[buftype];
	if (filled >= ((gator_buffer_size[buftype] * 3) / 4))
		gator_commit_buffer(cpu, buftype, time);
}
