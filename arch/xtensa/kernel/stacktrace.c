/*
 * arch/xtensa/kernel/stacktrace.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2013 Tensilica Inc.
 */
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/stacktrace.h>

#include <asm/stacktrace.h>
#include <asm/traps.h>

void walk_stackframe(unsigned long *sp,
		int (*fn)(struct stackframe *frame, void *data),
		void *data)
{
	unsigned long a0, a1;
	unsigned long sp_end;

	a1 = (unsigned long)sp;
	sp_end = ALIGN(a1, THREAD_SIZE);

	spill_registers();

	while (a1 < sp_end) {
		struct stackframe frame;

		sp = (unsigned long *)a1;

		a0 = *(sp - 4);
		a1 = *(sp - 3);

		if (a1 <= (unsigned long)sp)
			break;

		frame.pc = MAKE_PC_FROM_RA(a0, a1);
		frame.sp = a1;

		if (fn(&frame, data))
			return;
	}
}

#ifdef CONFIG_STACKTRACE

struct stack_trace_data {
	struct stack_trace *trace;
	unsigned skip;
};

static int stack_trace_cb(struct stackframe *frame, void *data)
{
	struct stack_trace_data *trace_data = data;
	struct stack_trace *trace = trace_data->trace;

	if (trace_data->skip) {
		--trace_data->skip;
		return 0;
	}
	if (!kernel_text_address(frame->pc))
		return 0;

	trace->entries[trace->nr_entries++] = frame->pc;
	return trace->nr_entries >= trace->max_entries;
}

void save_stack_trace_tsk(struct task_struct *task, struct stack_trace *trace)
{
	struct stack_trace_data trace_data = {
		.trace = trace,
		.skip = trace->skip,
	};
	walk_stackframe(stack_pointer(task), stack_trace_cb, &trace_data);
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);

void save_stack_trace(struct stack_trace *trace)
{
	save_stack_trace_tsk(current, trace);
}
EXPORT_SYMBOL_GPL(save_stack_trace);

#endif
