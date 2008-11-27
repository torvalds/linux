/**
 * @file cpu_buffer.h
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#ifndef OPROFILE_CPU_BUFFER_H
#define OPROFILE_CPU_BUFFER_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/cache.h>
#include <linux/sched.h>

struct task_struct;

int alloc_cpu_buffers(void);
void free_cpu_buffers(void);

void start_cpu_work(void);
void end_cpu_work(void);

/* CPU buffer is composed of such entries (which are
 * also used for context switch notes)
 */
struct op_sample {
	unsigned long eip;
	unsigned long event;
};

struct oprofile_cpu_buffer {
	volatile unsigned long head_pos;
	volatile unsigned long tail_pos;
	unsigned long buffer_size;
	struct task_struct *last_task;
	int last_is_kernel;
	int tracing;
	struct op_sample *buffer;
	unsigned long sample_received;
	unsigned long sample_lost_overflow;
	unsigned long backtrace_aborted;
	unsigned long sample_invalid_eip;
	int cpu;
	struct delayed_work work;
};

DECLARE_PER_CPU(struct oprofile_cpu_buffer, cpu_buffer);

void cpu_buffer_reset(struct oprofile_cpu_buffer *cpu_buf);

static inline
struct op_sample *cpu_buffer_write_entry(struct oprofile_cpu_buffer *cpu_buf)
{
	return &cpu_buf->buffer[cpu_buf->head_pos];
}

static inline
void cpu_buffer_write_commit(struct oprofile_cpu_buffer *b)
{
	unsigned long new_head = b->head_pos + 1;

	/*
	 * Ensure anything written to the slot before we increment is
	 * visible
	 */
	wmb();

	if (new_head < b->buffer_size)
		b->head_pos = new_head;
	else
		b->head_pos = 0;
}

static inline
struct op_sample *cpu_buffer_read_entry(struct oprofile_cpu_buffer *cpu_buf)
{
	return &cpu_buf->buffer[cpu_buf->tail_pos];
}

/* "acquire" as many cpu buffer slots as we can */
static inline
unsigned long cpu_buffer_entries(struct oprofile_cpu_buffer *b)
{
	unsigned long head = b->head_pos;
	unsigned long tail = b->tail_pos;

	/*
	 * Subtle. This resets the persistent last_task
	 * and in_kernel values used for switching notes.
	 * BUT, there is a small window between reading
	 * head_pos, and this call, that means samples
	 * can appear at the new head position, but not
	 * be prefixed with the notes for switching
	 * kernel mode or a task switch. This small hole
	 * can lead to mis-attribution or samples where
	 * we don't know if it's in the kernel or not,
	 * at the start of an event buffer.
	 */
	cpu_buffer_reset(b);

	if (head >= tail)
		return head - tail;

	return head + (b->buffer_size - tail);
}

/* transient events for the CPU buffer -> event buffer */
#define CPU_IS_KERNEL 1
#define CPU_TRACE_BEGIN 2
#define IBS_FETCH_BEGIN 3
#define IBS_OP_BEGIN    4

#endif /* OPROFILE_CPU_BUFFER_H */
