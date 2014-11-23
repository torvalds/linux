/**
 * @file cpu_buffer.c
 *
 * @remark Copyright 2002-2009 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 * @author Barry Kasindorf <barry.kasindorf@amd.com>
 * @author Robert Richter <robert.richter@amd.com>
 *
 * Each CPU has a local buffer that stores PC value/event
 * pairs. We also log context switches when we notice them.
 * Eventually each CPU's buffer is processed into the global
 * event buffer by sync_buffer().
 *
 * We use a local buffer for two reasons: an NMI or similar
 * interrupt cannot synchronise, and high sampling rates
 * would lead to catastrophic global synchronisation if
 * a global buffer was used.
 */

#include <linux/sched.h>
#include <linux/oprofile.h>
#include <linux/errno.h>

#include "event_buffer.h"
#include "cpu_buffer.h"
#include "buffer_sync.h"
#include "oprof.h"

#define OP_BUFFER_FLAGS	0

static struct ring_buffer *op_ring_buffer;
DEFINE_PER_CPU(struct oprofile_cpu_buffer, op_cpu_buffer);

static void wq_sync_buffer(struct work_struct *work);

#define DEFAULT_TIMER_EXPIRE (HZ / 10)
static int work_enabled;

unsigned long oprofile_get_cpu_buffer_size(void)
{
	return oprofile_cpu_buffer_size;
}

void oprofile_cpu_buffer_inc_smpl_lost(void)
{
	struct oprofile_cpu_buffer *cpu_buf = this_cpu_ptr(&op_cpu_buffer);

	cpu_buf->sample_lost_overflow++;
}

void free_cpu_buffers(void)
{
	if (op_ring_buffer)
		ring_buffer_free(op_ring_buffer);
	op_ring_buffer = NULL;
}

#define RB_EVENT_HDR_SIZE 4

int alloc_cpu_buffers(void)
{
	int i;

	unsigned long buffer_size = oprofile_cpu_buffer_size;
	unsigned long byte_size = buffer_size * (sizeof(struct op_sample) +
						 RB_EVENT_HDR_SIZE);

	op_ring_buffer = ring_buffer_alloc(byte_size, OP_BUFFER_FLAGS);
	if (!op_ring_buffer)
		goto fail;

	for_each_possible_cpu(i) {
		struct oprofile_cpu_buffer *b = &per_cpu(op_cpu_buffer, i);

		b->last_task = NULL;
		b->last_is_kernel = -1;
		b->tracing = 0;
		b->buffer_size = buffer_size;
		b->sample_received = 0;
		b->sample_lost_overflow = 0;
		b->backtrace_aborted = 0;
		b->sample_invalid_eip = 0;
		b->cpu = i;
		INIT_DELAYED_WORK(&b->work, wq_sync_buffer);
	}
	return 0;

fail:
	free_cpu_buffers();
	return -ENOMEM;
}

void start_cpu_work(void)
{
	int i;

	work_enabled = 1;

	for_each_online_cpu(i) {
		struct oprofile_cpu_buffer *b = &per_cpu(op_cpu_buffer, i);

		/*
		 * Spread the work by 1 jiffy per cpu so they dont all
		 * fire at once.
		 */
		schedule_delayed_work_on(i, &b->work, DEFAULT_TIMER_EXPIRE + i);
	}
}

void end_cpu_work(void)
{
	work_enabled = 0;
}

void flush_cpu_work(void)
{
	int i;

	for_each_online_cpu(i) {
		struct oprofile_cpu_buffer *b = &per_cpu(op_cpu_buffer, i);

		/* these works are per-cpu, no need for flush_sync */
		flush_delayed_work(&b->work);
	}
}

/*
 * This function prepares the cpu buffer to write a sample.
 *
 * Struct op_entry is used during operations on the ring buffer while
 * struct op_sample contains the data that is stored in the ring
 * buffer. Struct entry can be uninitialized. The function reserves a
 * data array that is specified by size. Use
 * op_cpu_buffer_write_commit() after preparing the sample. In case of
 * errors a null pointer is returned, otherwise the pointer to the
 * sample.
 *
 */
struct op_sample
*op_cpu_buffer_write_reserve(struct op_entry *entry, unsigned long size)
{
	entry->event = ring_buffer_lock_reserve
		(op_ring_buffer, sizeof(struct op_sample) +
		 size * sizeof(entry->sample->data[0]));
	if (!entry->event)
		return NULL;
	entry->sample = ring_buffer_event_data(entry->event);
	entry->size = size;
	entry->data = entry->sample->data;

	return entry->sample;
}

int op_cpu_buffer_write_commit(struct op_entry *entry)
{
	return ring_buffer_unlock_commit(op_ring_buffer, entry->event);
}

struct op_sample *op_cpu_buffer_read_entry(struct op_entry *entry, int cpu)
{
	struct ring_buffer_event *e;
	e = ring_buffer_consume(op_ring_buffer, cpu, NULL, NULL);
	if (!e)
		return NULL;

	entry->event = e;
	entry->sample = ring_buffer_event_data(e);
	entry->size = (ring_buffer_event_length(e) - sizeof(struct op_sample))
		/ sizeof(entry->sample->data[0]);
	entry->data = entry->sample->data;
	return entry->sample;
}

unsigned long op_cpu_buffer_entries(int cpu)
{
	return ring_buffer_entries_cpu(op_ring_buffer, cpu);
}

static int
op_add_code(struct oprofile_cpu_buffer *cpu_buf, unsigned long backtrace,
	    int is_kernel, struct task_struct *task)
{
	struct op_entry entry;
	struct op_sample *sample;
	unsigned long flags;
	int size;

	flags = 0;

	if (backtrace)
		flags |= TRACE_BEGIN;

	/* notice a switch from user->kernel or vice versa */
	is_kernel = !!is_kernel;
	if (cpu_buf->last_is_kernel != is_kernel) {
		cpu_buf->last_is_kernel = is_kernel;
		flags |= KERNEL_CTX_SWITCH;
		if (is_kernel)
			flags |= IS_KERNEL;
	}

	/* notice a task switch */
	if (cpu_buf->last_task != task) {
		cpu_buf->last_task = task;
		flags |= USER_CTX_SWITCH;
	}

	if (!flags)
		/* nothing to do */
		return 0;

	if (flags & USER_CTX_SWITCH)
		size = 1;
	else
		size = 0;

	sample = op_cpu_buffer_write_reserve(&entry, size);
	if (!sample)
		return -ENOMEM;

	sample->eip = ESCAPE_CODE;
	sample->event = flags;

	if (size)
		op_cpu_buffer_add_data(&entry, (unsigned long)task);

	op_cpu_buffer_write_commit(&entry);

	return 0;
}

static inline int
op_add_sample(struct oprofile_cpu_buffer *cpu_buf,
	      unsigned long pc, unsigned long event)
{
	struct op_entry entry;
	struct op_sample *sample;

	sample = op_cpu_buffer_write_reserve(&entry, 0);
	if (!sample)
		return -ENOMEM;

	sample->eip = pc;
	sample->event = event;

	return op_cpu_buffer_write_commit(&entry);
}

/*
 * This must be safe from any context.
 *
 * is_kernel is needed because on some architectures you cannot
 * tell if you are in kernel or user space simply by looking at
 * pc. We tag this in the buffer by generating kernel enter/exit
 * events whenever is_kernel changes
 */
static int
log_sample(struct oprofile_cpu_buffer *cpu_buf, unsigned long pc,
	   unsigned long backtrace, int is_kernel, unsigned long event,
	   struct task_struct *task)
{
	struct task_struct *tsk = task ? task : current;
	cpu_buf->sample_received++;

	if (pc == ESCAPE_CODE) {
		cpu_buf->sample_invalid_eip++;
		return 0;
	}

	if (op_add_code(cpu_buf, backtrace, is_kernel, tsk))
		goto fail;

	if (op_add_sample(cpu_buf, pc, event))
		goto fail;

	return 1;

fail:
	cpu_buf->sample_lost_overflow++;
	return 0;
}

static inline void oprofile_begin_trace(struct oprofile_cpu_buffer *cpu_buf)
{
	cpu_buf->tracing = 1;
}

static inline void oprofile_end_trace(struct oprofile_cpu_buffer *cpu_buf)
{
	cpu_buf->tracing = 0;
}

static inline void
__oprofile_add_ext_sample(unsigned long pc, struct pt_regs * const regs,
			  unsigned long event, int is_kernel,
			  struct task_struct *task)
{
	struct oprofile_cpu_buffer *cpu_buf = this_cpu_ptr(&op_cpu_buffer);
	unsigned long backtrace = oprofile_backtrace_depth;

	/*
	 * if log_sample() fail we can't backtrace since we lost the
	 * source of this event
	 */
	if (!log_sample(cpu_buf, pc, backtrace, is_kernel, event, task))
		/* failed */
		return;

	if (!backtrace)
		return;

	oprofile_begin_trace(cpu_buf);
	oprofile_ops.backtrace(regs, backtrace);
	oprofile_end_trace(cpu_buf);
}

void oprofile_add_ext_hw_sample(unsigned long pc, struct pt_regs * const regs,
				unsigned long event, int is_kernel,
				struct task_struct *task)
{
	__oprofile_add_ext_sample(pc, regs, event, is_kernel, task);
}

void oprofile_add_ext_sample(unsigned long pc, struct pt_regs * const regs,
			     unsigned long event, int is_kernel)
{
	__oprofile_add_ext_sample(pc, regs, event, is_kernel, NULL);
}

void oprofile_add_sample(struct pt_regs * const regs, unsigned long event)
{
	int is_kernel;
	unsigned long pc;

	if (likely(regs)) {
		is_kernel = !user_mode(regs);
		pc = profile_pc(regs);
	} else {
		is_kernel = 0;    /* This value will not be used */
		pc = ESCAPE_CODE; /* as this causes an early return. */
	}

	__oprofile_add_ext_sample(pc, regs, event, is_kernel, NULL);
}

/*
 * Add samples with data to the ring buffer.
 *
 * Use oprofile_add_data(&entry, val) to add data and
 * oprofile_write_commit(&entry) to commit the sample.
 */
void
oprofile_write_reserve(struct op_entry *entry, struct pt_regs * const regs,
		       unsigned long pc, int code, int size)
{
	struct op_sample *sample;
	int is_kernel = !user_mode(regs);
	struct oprofile_cpu_buffer *cpu_buf = this_cpu_ptr(&op_cpu_buffer);

	cpu_buf->sample_received++;

	/* no backtraces for samples with data */
	if (op_add_code(cpu_buf, 0, is_kernel, current))
		goto fail;

	sample = op_cpu_buffer_write_reserve(entry, size + 2);
	if (!sample)
		goto fail;
	sample->eip = ESCAPE_CODE;
	sample->event = 0;		/* no flags */

	op_cpu_buffer_add_data(entry, code);
	op_cpu_buffer_add_data(entry, pc);

	return;

fail:
	entry->event = NULL;
	cpu_buf->sample_lost_overflow++;
}

int oprofile_add_data(struct op_entry *entry, unsigned long val)
{
	if (!entry->event)
		return 0;
	return op_cpu_buffer_add_data(entry, val);
}

int oprofile_add_data64(struct op_entry *entry, u64 val)
{
	if (!entry->event)
		return 0;
	if (op_cpu_buffer_get_size(entry) < 2)
		/*
		 * the function returns 0 to indicate a too small
		 * buffer, even if there is some space left
		 */
		return 0;
	if (!op_cpu_buffer_add_data(entry, (u32)val))
		return 0;
	return op_cpu_buffer_add_data(entry, (u32)(val >> 32));
}

int oprofile_write_commit(struct op_entry *entry)
{
	if (!entry->event)
		return -EINVAL;
	return op_cpu_buffer_write_commit(entry);
}

void oprofile_add_pc(unsigned long pc, int is_kernel, unsigned long event)
{
	struct oprofile_cpu_buffer *cpu_buf = this_cpu_ptr(&op_cpu_buffer);
	log_sample(cpu_buf, pc, 0, is_kernel, event, NULL);
}

void oprofile_add_trace(unsigned long pc)
{
	struct oprofile_cpu_buffer *cpu_buf = this_cpu_ptr(&op_cpu_buffer);

	if (!cpu_buf->tracing)
		return;

	/*
	 * broken frame can give an eip with the same value as an
	 * escape code, abort the trace if we get it
	 */
	if (pc == ESCAPE_CODE)
		goto fail;

	if (op_add_sample(cpu_buf, pc, 0))
		goto fail;

	return;
fail:
	cpu_buf->tracing = 0;
	cpu_buf->backtrace_aborted++;
	return;
}

/*
 * This serves to avoid cpu buffer overflow, and makes sure
 * the task mortuary progresses
 *
 * By using schedule_delayed_work_on and then schedule_delayed_work
 * we guarantee this will stay on the correct cpu
 */
static void wq_sync_buffer(struct work_struct *work)
{
	struct oprofile_cpu_buffer *b =
		container_of(work, struct oprofile_cpu_buffer, work.work);
	if (b->cpu != smp_processor_id() && !cpu_online(b->cpu)) {
		cancel_delayed_work(&b->work);
		return;
	}
	sync_buffer(b->cpu);

	/* don't re-add the work if we're shutting down */
	if (work_enabled)
		schedule_delayed_work(&b->work, DEFAULT_TIMER_EXPIRE);
}
