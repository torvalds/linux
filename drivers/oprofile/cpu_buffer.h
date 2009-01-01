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
#include <linux/ring_buffer.h>

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

struct op_entry {
	struct ring_buffer_event *event;
	struct op_sample *sample;
	unsigned long irq_flags;
};

struct oprofile_cpu_buffer {
	volatile unsigned long head_pos;
	volatile unsigned long tail_pos;
	unsigned long buffer_size;
	struct task_struct *last_task;
	int last_is_kernel;
	int tracing;
	unsigned long sample_received;
	unsigned long sample_lost_overflow;
	unsigned long backtrace_aborted;
	unsigned long sample_invalid_eip;
	int cpu;
	struct delayed_work work;
};

extern struct ring_buffer *op_ring_buffer_read;
extern struct ring_buffer *op_ring_buffer_write;
DECLARE_PER_CPU(struct oprofile_cpu_buffer, cpu_buffer);

/*
 * Resets the cpu buffer to a sane state.
 *
 * reset these to invalid values; the next sample collected will
 * populate the buffer with proper values to initialize the buffer
 */
static inline void cpu_buffer_reset(int cpu)
{
	struct oprofile_cpu_buffer *cpu_buf = &per_cpu(cpu_buffer, cpu);

	cpu_buf->last_is_kernel = -1;
	cpu_buf->last_task = NULL;
}

static inline int cpu_buffer_write_entry(struct op_entry *entry)
{
	entry->event = ring_buffer_lock_reserve(op_ring_buffer_write,
						sizeof(struct op_sample),
						&entry->irq_flags);
	if (entry->event)
		entry->sample = ring_buffer_event_data(entry->event);
	else
		entry->sample = NULL;

	if (!entry->sample)
		return -ENOMEM;

	return 0;
}

static inline int cpu_buffer_write_commit(struct op_entry *entry)
{
	return ring_buffer_unlock_commit(op_ring_buffer_write, entry->event,
					 entry->irq_flags);
}

static inline struct op_sample *cpu_buffer_read_entry(int cpu)
{
	struct ring_buffer_event *e;
	e = ring_buffer_consume(op_ring_buffer_read, cpu, NULL);
	if (e)
		return ring_buffer_event_data(e);
	if (ring_buffer_swap_cpu(op_ring_buffer_read,
				 op_ring_buffer_write,
				 cpu))
		return NULL;
	e = ring_buffer_consume(op_ring_buffer_read, cpu, NULL);
	if (e)
		return ring_buffer_event_data(e);
	return NULL;
}

/* "acquire" as many cpu buffer slots as we can */
static inline unsigned long cpu_buffer_entries(int cpu)
{
	return ring_buffer_entries_cpu(op_ring_buffer_read, cpu)
		+ ring_buffer_entries_cpu(op_ring_buffer_write, cpu);
}

/* transient events for the CPU buffer -> event buffer */
#define CPU_IS_KERNEL 1
#define CPU_TRACE_BEGIN 2
#define IBS_FETCH_BEGIN 3
#define IBS_OP_BEGIN    4

#endif /* OPROFILE_CPU_BUFFER_H */
