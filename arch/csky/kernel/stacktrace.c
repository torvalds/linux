// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd. */

#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>
#include <linux/ftrace.h>

void save_stack_trace(struct stack_trace *trace)
{
	save_stack_trace_tsk(current, trace);
}
EXPORT_SYMBOL_GPL(save_stack_trace);

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	unsigned long *fp, *stack_start, *stack_end;
	unsigned long addr;
	int skip = trace->skip;
	int savesched;
	int graph_idx = 0;

	if (tsk == current) {
		asm volatile("mov %0, r8\n":"=r"(fp));
		savesched = 1;
	} else {
		fp = (unsigned long *)thread_saved_fp(tsk);
		savesched = 0;
	}

	addr = (unsigned long) fp & THREAD_MASK;
	stack_start = (unsigned long *) addr;
	stack_end = (unsigned long *) (addr + THREAD_SIZE);

	while (fp > stack_start && fp < stack_end) {
		unsigned long lpp, fpp;

		fpp = fp[0];
		lpp = fp[1];
		if (!__kernel_text_address(lpp))
			break;
		else
			lpp = ftrace_graph_ret_addr(tsk, &graph_idx, lpp, NULL);

		if (savesched || !in_sched_functions(lpp)) {
			if (skip) {
				skip--;
			} else {
				trace->entries[trace->nr_entries++] = lpp;
				if (trace->nr_entries >= trace->max_entries)
					break;
			}
		}
		fp = (unsigned long *)fpp;
	}
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);
