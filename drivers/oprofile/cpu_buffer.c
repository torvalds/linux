/**
 * @file cpu_buffer.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 * @author Barry Kasindorf <barry.kasindorf@amd.com>
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
#include <linux/vmalloc.h>
#include <linux/errno.h>
 
#include "event_buffer.h"
#include "cpu_buffer.h"
#include "buffer_sync.h"
#include "oprof.h"

DEFINE_PER_CPU(struct oprofile_cpu_buffer, cpu_buffer);

static void wq_sync_buffer(struct work_struct *work);

#define DEFAULT_TIMER_EXPIRE (HZ / 10)
static int work_enabled;

void free_cpu_buffers(void)
{
	int i;
 
	for_each_online_cpu(i) {
		vfree(per_cpu(cpu_buffer, i).buffer);
		per_cpu(cpu_buffer, i).buffer = NULL;
	}
}

int alloc_cpu_buffers(void)
{
	int i;
 
	unsigned long buffer_size = fs_cpu_buffer_size;
 
	for_each_online_cpu(i) {
		struct oprofile_cpu_buffer *b = &per_cpu(cpu_buffer, i);
 
		b->buffer = vmalloc_node(sizeof(struct op_sample) * buffer_size,
			cpu_to_node(i));
		if (!b->buffer)
			goto fail;
 
		b->last_task = NULL;
		b->last_is_kernel = -1;
		b->tracing = 0;
		b->buffer_size = buffer_size;
		b->tail_pos = 0;
		b->head_pos = 0;
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
		struct oprofile_cpu_buffer *b = &per_cpu(cpu_buffer, i);

		/*
		 * Spread the work by 1 jiffy per cpu so they dont all
		 * fire at once.
		 */
		schedule_delayed_work_on(i, &b->work, DEFAULT_TIMER_EXPIRE + i);
	}
}

void end_cpu_work(void)
{
	int i;

	work_enabled = 0;

	for_each_online_cpu(i) {
		struct oprofile_cpu_buffer *b = &per_cpu(cpu_buffer, i);

		cancel_delayed_work(&b->work);
	}

	flush_scheduled_work();
}

/* Resets the cpu buffer to a sane state. */
void cpu_buffer_reset(struct oprofile_cpu_buffer * cpu_buf)
{
	/* reset these to invalid values; the next sample
	 * collected will populate the buffer with proper
	 * values to initialize the buffer
	 */
	cpu_buf->last_is_kernel = -1;
	cpu_buf->last_task = NULL;
}

/* compute number of available slots in cpu_buffer queue */
static unsigned long nr_available_slots(struct oprofile_cpu_buffer const * b)
{
	unsigned long head = b->head_pos;
	unsigned long tail = b->tail_pos;

	if (tail > head)
		return (tail - head) - 1;

	return tail + (b->buffer_size - head) - 1;
}

static void increment_head(struct oprofile_cpu_buffer * b)
{
	unsigned long new_head = b->head_pos + 1;

	/* Ensure anything written to the slot before we
	 * increment is visible */
	wmb();

	if (new_head < b->buffer_size)
		b->head_pos = new_head;
	else
		b->head_pos = 0;
}

static inline void
add_sample(struct oprofile_cpu_buffer * cpu_buf,
           unsigned long pc, unsigned long event)
{
	struct op_sample * entry = &cpu_buf->buffer[cpu_buf->head_pos];
	entry->eip = pc;
	entry->event = event;
	increment_head(cpu_buf);
}

static inline void
add_code(struct oprofile_cpu_buffer * buffer, unsigned long value)
{
	add_sample(buffer, ESCAPE_CODE, value);
}

/* This must be safe from any context. It's safe writing here
 * because of the head/tail separation of the writer and reader
 * of the CPU buffer.
 *
 * is_kernel is needed because on some architectures you cannot
 * tell if you are in kernel or user space simply by looking at
 * pc. We tag this in the buffer by generating kernel enter/exit
 * events whenever is_kernel changes
 */
static int log_sample(struct oprofile_cpu_buffer * cpu_buf, unsigned long pc,
		      int is_kernel, unsigned long event)
{
	struct task_struct * task;

	cpu_buf->sample_received++;

	if (pc == ESCAPE_CODE) {
		cpu_buf->sample_invalid_eip++;
		return 0;
	}

	if (nr_available_slots(cpu_buf) < 3) {
		cpu_buf->sample_lost_overflow++;
		return 0;
	}

	is_kernel = !!is_kernel;

	task = current;

	/* notice a switch from user->kernel or vice versa */
	if (cpu_buf->last_is_kernel != is_kernel) {
		cpu_buf->last_is_kernel = is_kernel;
		add_code(cpu_buf, is_kernel);
	}

	/* notice a task switch */
	if (cpu_buf->last_task != task) {
		cpu_buf->last_task = task;
		add_code(cpu_buf, (unsigned long)task);
	}
 
	add_sample(cpu_buf, pc, event);
	return 1;
}

static int oprofile_begin_trace(struct oprofile_cpu_buffer *cpu_buf)
{
	if (nr_available_slots(cpu_buf) < 4) {
		cpu_buf->sample_lost_overflow++;
		return 0;
	}

	add_code(cpu_buf, CPU_TRACE_BEGIN);
	cpu_buf->tracing = 1;
	return 1;
}

static void oprofile_end_trace(struct oprofile_cpu_buffer * cpu_buf)
{
	cpu_buf->tracing = 0;
}

void oprofile_add_ext_sample(unsigned long pc, struct pt_regs * const regs,
				unsigned long event, int is_kernel)
{
	struct oprofile_cpu_buffer *cpu_buf = &__get_cpu_var(cpu_buffer);

	if (!backtrace_depth) {
		log_sample(cpu_buf, pc, is_kernel, event);
		return;
	}

	if (!oprofile_begin_trace(cpu_buf))
		return;

	/* if log_sample() fail we can't backtrace since we lost the source
	 * of this event */
	if (log_sample(cpu_buf, pc, is_kernel, event))
		oprofile_ops.backtrace(regs, backtrace_depth);
	oprofile_end_trace(cpu_buf);
}

void oprofile_add_sample(struct pt_regs * const regs, unsigned long event)
{
	int is_kernel = !user_mode(regs);
	unsigned long pc = profile_pc(regs);

	oprofile_add_ext_sample(pc, regs, event, is_kernel);
}

#ifdef CONFIG_OPROFILE_IBS

#define MAX_IBS_SAMPLE_SIZE	14
static int log_ibs_sample(struct oprofile_cpu_buffer *cpu_buf,
	unsigned long pc, int is_kernel, unsigned  int *ibs, int ibs_code)
{
	struct task_struct *task;

	cpu_buf->sample_received++;

	if (nr_available_slots(cpu_buf) < MAX_IBS_SAMPLE_SIZE) {
		cpu_buf->sample_lost_overflow++;
		return 0;
	}

	is_kernel = !!is_kernel;

	/* notice a switch from user->kernel or vice versa */
	if (cpu_buf->last_is_kernel != is_kernel) {
		cpu_buf->last_is_kernel = is_kernel;
		add_code(cpu_buf, is_kernel);
	}

	/* notice a task switch */
	if (!is_kernel) {
		task = current;

		if (cpu_buf->last_task != task) {
			cpu_buf->last_task = task;
			add_code(cpu_buf, (unsigned long)task);
		}
	}

	add_code(cpu_buf, ibs_code);
	add_sample(cpu_buf, ibs[0], ibs[1]);
	add_sample(cpu_buf, ibs[2], ibs[3]);
	add_sample(cpu_buf, ibs[4], ibs[5]);

	if (ibs_code == IBS_OP_BEGIN) {
	add_sample(cpu_buf, ibs[6], ibs[7]);
	add_sample(cpu_buf, ibs[8], ibs[9]);
	add_sample(cpu_buf, ibs[10], ibs[11]);
	}

	return 1;
}

void oprofile_add_ibs_sample(struct pt_regs *const regs,
				unsigned int * const ibs_sample, u8 code)
{
	int is_kernel = !user_mode(regs);
	unsigned long pc = profile_pc(regs);

	struct oprofile_cpu_buffer *cpu_buf =
			 &per_cpu(cpu_buffer, smp_processor_id());

	if (!backtrace_depth) {
		log_ibs_sample(cpu_buf, pc, is_kernel, ibs_sample, code);
		return;
	}

	/* if log_sample() fails we can't backtrace since we lost the source
	* of this event */
	if (log_ibs_sample(cpu_buf, pc, is_kernel, ibs_sample, code))
		oprofile_ops.backtrace(regs, backtrace_depth);
}

#endif

void oprofile_add_pc(unsigned long pc, int is_kernel, unsigned long event)
{
	struct oprofile_cpu_buffer *cpu_buf = &__get_cpu_var(cpu_buffer);
	log_sample(cpu_buf, pc, is_kernel, event);
}

void oprofile_add_trace(unsigned long pc)
{
	struct oprofile_cpu_buffer *cpu_buf = &__get_cpu_var(cpu_buffer);

	if (!cpu_buf->tracing)
		return;

	if (nr_available_slots(cpu_buf) < 1) {
		cpu_buf->tracing = 0;
		cpu_buf->sample_lost_overflow++;
		return;
	}

	/* broken frame can give an eip with the same value as an escape code,
	 * abort the trace if we get it */
	if (pc == ESCAPE_CODE) {
		cpu_buf->tracing = 0;
		cpu_buf->backtrace_aborted++;
		return;
	}

	add_sample(cpu_buf, pc, 0);
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
	struct oprofile_cpu_buffer * b =
		container_of(work, struct oprofile_cpu_buffer, work.work);
	if (b->cpu != smp_processor_id()) {
		printk(KERN_DEBUG "WQ on CPU%d, prefer CPU%d\n",
		       smp_processor_id(), b->cpu);
	}
	sync_buffer(b->cpu);

	/* don't re-add the work if we're shutting down */
	if (work_enabled)
		schedule_delayed_work(&b->work, DEFAULT_TIMER_EXPIRE);
}
