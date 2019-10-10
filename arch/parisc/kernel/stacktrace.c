// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stack trace management functions
 *
 *  Copyright (C) 2009 Helge Deller <deller@gmx.de>
 *  based on arch/x86/kernel/stacktrace.c by Ingo Molnar <mingo@redhat.com>
 *  and parisc unwind functions by Randolph Chung <tausq@debian.org>
 *
 *  TODO: Userspace stacktrace (CONFIG_USER_STACKTRACE_SUPPORT)
 */
#include <linux/module.h>
#include <linux/stacktrace.h>

#include <asm/unwind.h>

static void dump_trace(struct task_struct *task, struct stack_trace *trace)
{
	struct unwind_frame_info info;

	unwind_frame_init_task(&info, task, NULL);

	/* unwind stack and save entries in stack_trace struct */
	trace->nr_entries = 0;
	while (trace->nr_entries < trace->max_entries) {
		if (unwind_once(&info) < 0 || info.ip == 0)
			break;

		if (__kernel_text_address(info.ip))
			trace->entries[trace->nr_entries++] = info.ip;
	}
}

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
void save_stack_trace(struct stack_trace *trace)
{
	dump_trace(current, trace);
}
EXPORT_SYMBOL_GPL(save_stack_trace);

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	dump_trace(tsk, trace);
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);
