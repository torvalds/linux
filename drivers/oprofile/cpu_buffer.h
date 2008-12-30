/**
 * @file cpu_buffer.h
 *
 * @remark Copyright 2002-2009 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 * @author Robert Richter <robert.richter@amd.com>
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
	unsigned long data[0];
};

struct op_entry {
	struct ring_buffer_event *event;
	struct op_sample *sample;
	unsigned long irq_flags;
	unsigned long size;
	unsigned long *data;
};

struct oprofile_cpu_buffer {
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

DECLARE_PER_CPU(struct oprofile_cpu_buffer, cpu_buffer);

/*
 * Resets the cpu buffer to a sane state.
 *
 * reset these to invalid values; the next sample collected will
 * populate the buffer with proper values to initialize the buffer
 */
static inline void op_cpu_buffer_reset(int cpu)
{
	struct oprofile_cpu_buffer *cpu_buf = &per_cpu(cpu_buffer, cpu);

	cpu_buf->last_is_kernel = -1;
	cpu_buf->last_task = NULL;
}

struct op_sample
*op_cpu_buffer_write_reserve(struct op_entry *entry, unsigned long size);
int op_cpu_buffer_write_commit(struct op_entry *entry);
struct op_sample *op_cpu_buffer_read_entry(struct op_entry *entry, int cpu);
unsigned long op_cpu_buffer_entries(int cpu);

/* transient events for the CPU buffer -> event buffer */
#define CPU_IS_KERNEL 1
#define CPU_TRACE_BEGIN 2
#define IBS_FETCH_BEGIN 3
#define IBS_OP_BEGIN    4

#endif /* OPROFILE_CPU_BUFFER_H */
