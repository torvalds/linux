#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/stacktrace.h>
#include <asm/stacktrace.h>

void walk_stackframe(unsigned long sp,
		     int (*fn)(unsigned long addr, void *data),
		     void *data)
{
	unsigned long high = ALIGN(sp, THREAD_SIZE);

	for (; sp <= high - 4; sp += 4) {
		unsigned long addr = *(unsigned long *) sp;

		if (!kernel_text_address(addr))
			continue;

		if (fn(addr, data))
			break;
	}
}

struct stack_trace_data {
	struct stack_trace *trace;
	unsigned int no_sched_functions;
	unsigned int skip;
};

#ifdef CONFIG_STACKTRACE

static int save_trace(unsigned long addr, void *d)
{
	struct stack_trace_data *data = d;
	struct stack_trace *trace = data->trace;

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
	unsigned long sp;

	data.trace = trace;
	data.skip = trace->skip;

	if (tsk != current) {
		data.no_sched_functions = 1;
		sp = tsk->thread.ksp;
	} else {
		data.no_sched_functions = 0;
		sp = rdsp();
	}

	walk_stackframe(sp, save_trace, &data);
	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = ULONG_MAX;
}

void save_stack_trace(struct stack_trace *trace)
{
	save_stack_trace_tsk(current, trace);
}
EXPORT_SYMBOL_GPL(save_stack_trace);

#endif /* CONFIG_STACKTRACE */
