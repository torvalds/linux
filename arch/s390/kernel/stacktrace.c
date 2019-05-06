// SPDX-License-Identifier: GPL-2.0
/*
 * Stack trace management functions
 *
 *  Copyright IBM Corp. 2006
 *  Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/stacktrace.h>
#include <linux/kallsyms.h>
#include <linux/export.h>
#include <asm/stacktrace.h>
#include <asm/unwind.h>

void save_stack_trace(struct stack_trace *trace)
{
	struct unwind_state state;

	unwind_for_each_frame(&state, current, NULL, 0) {
		if (trace->nr_entries >= trace->max_entries)
			break;
		if (trace->skip > 0)
			trace->skip--;
		else
			trace->entries[trace->nr_entries++] = state.ip;
	}
}
EXPORT_SYMBOL_GPL(save_stack_trace);

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	struct unwind_state state;

	unwind_for_each_frame(&state, tsk, NULL, 0) {
		if (trace->nr_entries >= trace->max_entries)
			break;
		if (in_sched_functions(state.ip))
			continue;
		if (trace->skip > 0)
			trace->skip--;
		else
			trace->entries[trace->nr_entries++] = state.ip;
	}
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);

void save_stack_trace_regs(struct pt_regs *regs, struct stack_trace *trace)
{
	struct unwind_state state;

	unwind_for_each_frame(&state, current, regs, 0) {
		if (trace->nr_entries >= trace->max_entries)
			break;
		if (trace->skip > 0)
			trace->skip--;
		else
			trace->entries[trace->nr_entries++] = state.ip;
	}
}
EXPORT_SYMBOL_GPL(save_stack_trace_regs);
