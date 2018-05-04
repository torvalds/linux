/*
 * Stack trace utility
 *
 * Copyright 2008 Christoph Hellwig, IBM Corp.
 * Copyright 2018 SUSE Linux GmbH
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/export.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <linux/ftrace.h>
#include <asm/kprobes.h>

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
static void save_context_stack(struct stack_trace *trace, unsigned long sp,
			struct task_struct *tsk, int savesched)
{
	for (;;) {
		unsigned long *stack = (unsigned long *) sp;
		unsigned long newsp, ip;

		if (!validate_sp(sp, tsk, STACK_FRAME_OVERHEAD))
			return;

		newsp = stack[0];
		ip = stack[STACK_FRAME_LR_SAVE];

		if (savesched || !in_sched_functions(ip)) {
			if (!trace->skip)
				trace->entries[trace->nr_entries++] = ip;
			else
				trace->skip--;
		}

		if (trace->nr_entries >= trace->max_entries)
			return;

		sp = newsp;
	}
}

void save_stack_trace(struct stack_trace *trace)
{
	unsigned long sp;

	sp = current_stack_pointer();

	save_context_stack(trace, sp, current, 1);
}
EXPORT_SYMBOL_GPL(save_stack_trace);

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	unsigned long sp;

	if (tsk == current)
		sp = current_stack_pointer();
	else
		sp = tsk->thread.ksp;

	save_context_stack(trace, sp, tsk, 0);
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);

void
save_stack_trace_regs(struct pt_regs *regs, struct stack_trace *trace)
{
	save_context_stack(trace, regs->gpr[1], current, 0);
}
EXPORT_SYMBOL_GPL(save_stack_trace_regs);

#ifdef CONFIG_HAVE_RELIABLE_STACKTRACE
int
save_stack_trace_tsk_reliable(struct task_struct *tsk,
				struct stack_trace *trace)
{
	unsigned long sp;
	unsigned long stack_page = (unsigned long)task_stack_page(tsk);
	unsigned long stack_end;
	int graph_idx = 0;

	/*
	 * The last frame (unwinding first) may not yet have saved
	 * its LR onto the stack.
	 */
	int firstframe = 1;

	if (tsk == current)
		sp = current_stack_pointer();
	else
		sp = tsk->thread.ksp;

	stack_end = stack_page + THREAD_SIZE;
	if (!is_idle_task(tsk)) {
		/*
		 * For user tasks, this is the SP value loaded on
		 * kernel entry, see "PACAKSAVE(r13)" in _switch() and
		 * system_call_common()/EXCEPTION_PROLOG_COMMON().
		 *
		 * Likewise for non-swapper kernel threads,
		 * this also happens to be the top of the stack
		 * as setup by copy_thread().
		 *
		 * Note that stack backlinks are not properly setup by
		 * copy_thread() and thus, a forked task() will have
		 * an unreliable stack trace until it's been
		 * _switch()'ed to for the first time.
		 */
		stack_end -= STACK_FRAME_OVERHEAD + sizeof(struct pt_regs);
	} else {
		/*
		 * idle tasks have a custom stack layout,
		 * c.f. cpu_idle_thread_init().
		 */
		stack_end -= STACK_FRAME_OVERHEAD;
	}

	if (sp < stack_page + sizeof(struct thread_struct) ||
	    sp > stack_end - STACK_FRAME_MIN_SIZE) {
		return 1;
	}

	for (;;) {
		unsigned long *stack = (unsigned long *) sp;
		unsigned long newsp, ip;

		/* sanity check: ABI requires SP to be aligned 16 bytes. */
		if (sp & 0xF)
			return 1;

		/* Mark stacktraces with exception frames as unreliable. */
		if (sp <= stack_end - STACK_INT_FRAME_SIZE &&
		    stack[STACK_FRAME_MARKER] == STACK_FRAME_REGS_MARKER) {
			return 1;
		}

		newsp = stack[0];
		/* Stack grows downwards; unwinder may only go up. */
		if (newsp <= sp)
			return 1;

		if (newsp != stack_end &&
		    newsp > stack_end - STACK_FRAME_MIN_SIZE) {
			return 1; /* invalid backlink, too far up. */
		}

		/* Examine the saved LR: it must point into kernel code. */
		ip = stack[STACK_FRAME_LR_SAVE];
		if (!firstframe && !__kernel_text_address(ip))
			return 1;
		firstframe = 0;

		/*
		 * FIXME: IMHO these tests do not belong in
		 * arch-dependent code, they are generic.
		 */
		ip = ftrace_graph_ret_addr(tsk, &graph_idx, ip, NULL);

		/*
		 * Mark stacktraces with kretprobed functions on them
		 * as unreliable.
		 */
		if (ip == (unsigned long)kretprobe_trampoline)
			return 1;

		if (!trace->skip)
			trace->entries[trace->nr_entries++] = ip;
		else
			trace->skip--;

		if (newsp == stack_end)
			break;

		if (trace->nr_entries >= trace->max_entries)
			return -E2BIG;

		sp = newsp;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk_reliable);
#endif /* CONFIG_HAVE_RELIABLE_STACKTRACE */
