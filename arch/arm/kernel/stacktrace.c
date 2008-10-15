#include <linux/module.h>
#include <linux/sched.h>
#include <linux/stacktrace.h>

#include "stacktrace.h"

int walk_stackframe(unsigned long fp, unsigned long low, unsigned long high,
		    int (*fn)(struct stackframe *, void *), void *data)
{
	struct stackframe *frame;

	do {
		/*
		 * Check current frame pointer is within bounds
		 */
		if (fp < (low + 12) || fp + 4 >= high)
			break;

		frame = (struct stackframe *)(fp - 12);

		if (fn(frame, data))
			break;

		/*
		 * Update the low bound - the next frame must always
		 * be at a higher address than the current frame.
		 */
		low = fp + 4;
		fp = frame->fp;
	} while (fp);

	return 0;
}
EXPORT_SYMBOL(walk_stackframe);

#ifdef CONFIG_STACKTRACE
struct stack_trace_data {
	struct stack_trace *trace;
	unsigned int no_sched_functions;
	unsigned int skip;
};

static int save_trace(struct stackframe *frame, void *d)
{
	struct stack_trace_data *data = d;
	struct stack_trace *trace = data->trace;
	unsigned long addr = frame->lr;

	if (data->no_sched_functions && in_sched_functions(addr))
		return 0;
	if (data->skip) {
		data->skip--;
		return 0;
	}

	trace->entries[trace->nr_entries++] = addr;

	return trace->nr_entries >= trace->max_entries;
}

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	struct stack_trace_data data;
	unsigned long fp, base;

	data.trace = trace;
	data.skip = trace->skip;
	base = (unsigned long)task_stack_page(tsk);

	if (tsk != current) {
#ifdef CONFIG_SMP
		/*
		 * What guarantees do we have here that 'tsk'
		 * is not running on another CPU?
		 */
		BUG();
#else
		data.no_sched_functions = 1;
		fp = thread_saved_fp(tsk);
#endif
	} else {
		data.no_sched_functions = 0;
		asm("mov %0, fp" : "=r" (fp));
	}

	walk_stackframe(fp, base, base + THREAD_SIZE, save_trace, &data);
	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = ULONG_MAX;
}

void save_stack_trace(struct stack_trace *trace)
{
	save_stack_trace_tsk(current, trace);
}
EXPORT_SYMBOL_GPL(save_stack_trace);
#endif
