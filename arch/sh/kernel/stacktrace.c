// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/kernel/stacktrace.c
 *
 * Stack trace management functions
 *
 *  Copyright (C) 2006 - 2008  Paul Mundt
 */
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/stacktrace.h>
#include <linux/thread_info.h>
#include <linux/module.h>
#include <asm/unwinder.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
static void save_stack_address(void *data, unsigned long addr, int reliable)
{
	struct stack_trace *trace = data;

	if (!reliable)
		return;

	if (trace->skip > 0) {
		trace->skip--;
		return;
	}

	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = addr;
}

static const struct stacktrace_ops save_stack_ops = {
	.address = save_stack_address,
};

void save_stack_trace(struct stack_trace *trace)
{
	unsigned long *sp = (unsigned long *)current_stack_pointer;

	unwind_stack(current, NULL, sp,  &save_stack_ops, trace);
}
EXPORT_SYMBOL_GPL(save_stack_trace);

static void
save_stack_address_nosched(void *data, unsigned long addr, int reliable)
{
	struct stack_trace *trace = (struct stack_trace *)data;

	if (!reliable)
		return;

	if (in_sched_functions(addr))
		return;

	if (trace->skip > 0) {
		trace->skip--;
		return;
	}

	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = addr;
}

static const struct stacktrace_ops save_stack_ops_nosched = {
	.address = save_stack_address_nosched,
};

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	unsigned long *sp = (unsigned long *)tsk->thread.sp;

	unwind_stack(current, NULL, sp,  &save_stack_ops_nosched, trace);
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);
