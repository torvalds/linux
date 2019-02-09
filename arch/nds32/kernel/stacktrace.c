// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

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
	unsigned long *fpn;
	int skip = trace->skip;
	int savesched;
	int graph_idx = 0;

	if (tsk == current) {
		__asm__ __volatile__("\tori\t%0, $fp, #0\n":"=r"(fpn));
		savesched = 1;
	} else {
		fpn = (unsigned long *)thread_saved_fp(tsk);
		savesched = 0;
	}

	while (!kstack_end(fpn) && !((unsigned long)fpn & 0x3)
	       && (fpn >= (unsigned long *)TASK_SIZE)) {
		unsigned long lpp, fpp;

		lpp = fpn[LP_OFFSET];
		fpp = fpn[FP_OFFSET];
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
		fpn = (unsigned long *)fpp;
	}
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);
