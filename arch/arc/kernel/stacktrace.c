/*
 *	stacktrace.c : stacktracing APIs needed by rest of kernel
 *			(wrappers over ARC dwarf based unwinder)
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  vineetg: aug 2009
 *  -Implemented CONFIG_STACKTRACE APIs, primarily save_stack_trace_tsk( )
 *   for displaying task's kernel mode call stack in /proc/<pid>/stack
 *  -Iterator based approach to have single copy of unwinding core and APIs
 *   needing unwinding, implement the logic in iterator regarding:
 *      = which frame onwards to start capture
 *      = which frame to stop capturing (wchan)
 *      = specifics of data structs where trace is saved(CONFIG_STACKTRACE etc)
 *
 *  vineetg: March 2009
 *  -Implemented correct versions of thread_saved_pc() and get_wchan()
 *
 *  rajeshwarr: 2008
 *  -Initial implementation
 */

#include <linux/ptrace.h>
#include <linux/export.h>
#include <linux/stacktrace.h>
#include <linux/kallsyms.h>
#include <linux/sched/debug.h>

#include <asm/arcregs.h>
#include <asm/unwind.h>
#include <asm/switch_to.h>

/*-------------------------------------------------------------------------
 *              Unwinder Iterator
 *-------------------------------------------------------------------------
 */

#ifdef CONFIG_ARC_DW2_UNWIND

static void seed_unwind_frame_info(struct task_struct *tsk,
				   struct pt_regs *regs,
				   struct unwind_frame_info *frame_info)
{
	/*
	 * synchronous unwinding (e.g. dump_stack)
	 *  - uses current values of SP and friends
	 */
	if (tsk == NULL && regs == NULL) {
		unsigned long fp, sp, blink, ret;
		frame_info->task = current;

		__asm__ __volatile__(
			"mov %0,r27\n\t"
			"mov %1,r28\n\t"
			"mov %2,r31\n\t"
			"mov %3,r63\n\t"
			: "=r"(fp), "=r"(sp), "=r"(blink), "=r"(ret)
		);

		frame_info->regs.r27 = fp;
		frame_info->regs.r28 = sp;
		frame_info->regs.r31 = blink;
		frame_info->regs.r63 = ret;
		frame_info->call_frame = 0;
	} else if (regs == NULL) {
		/*
		 * Asynchronous unwinding of sleeping task
		 *  - Gets SP etc from task's pt_regs (saved bottom of kernel
		 *    mode stack of task)
		 */

		frame_info->task = tsk;

		frame_info->regs.r27 = TSK_K_FP(tsk);
		frame_info->regs.r28 = TSK_K_ESP(tsk);
		frame_info->regs.r31 = TSK_K_BLINK(tsk);
		frame_info->regs.r63 = (unsigned int)__switch_to;

		/* In the prologue of __switch_to, first FP is saved on stack
		 * and then SP is copied to FP. Dwarf assumes cfa as FP based
		 * but we didn't save FP. The value retrieved above is FP's
		 * state in previous frame.
		 * As a work around for this, we unwind from __switch_to start
		 * and adjust SP accordingly. The other limitation is that
		 * __switch_to macro is dwarf rules are not generated for inline
		 * assembly code
		 */
		frame_info->regs.r27 = 0;
		frame_info->regs.r28 += 60;
		frame_info->call_frame = 0;

	} else {
		/*
		 * Asynchronous unwinding of intr/exception
		 *  - Just uses the pt_regs passed
		 */
		frame_info->task = tsk;

		frame_info->regs.r27 = regs->fp;
		frame_info->regs.r28 = regs->sp;
		frame_info->regs.r31 = regs->blink;
		frame_info->regs.r63 = regs->ret;
		frame_info->call_frame = 0;
	}
}

#endif

notrace noinline unsigned int
arc_unwind_core(struct task_struct *tsk, struct pt_regs *regs,
		int (*consumer_fn) (unsigned int, void *), void *arg)
{
#ifdef CONFIG_ARC_DW2_UNWIND
	int ret = 0;
	unsigned int address;
	struct unwind_frame_info frame_info;

	seed_unwind_frame_info(tsk, regs, &frame_info);

	while (1) {
		address = UNW_PC(&frame_info);

		if (!address || !__kernel_text_address(address))
			break;

		if (consumer_fn(address, arg) == -1)
			break;

		ret = arc_unwind(&frame_info);
		if (ret)
			break;

		frame_info.regs.r63 = frame_info.regs.r31;
	}

	return address;		/* return the last address it saw */
#else
	/* On ARC, only Dward based unwinder works. fp based backtracing is
	 * not possible (-fno-omit-frame-pointer) because of the way function
	 * prelogue is setup (callee regs saved and then fp set and not other
	 * way around
	 */
	pr_warn_once("CONFIG_ARC_DW2_UNWIND needs to be enabled\n");
	return 0;

#endif
}

/*-------------------------------------------------------------------------
 * callbacks called by unwinder iterator to implement kernel APIs
 *
 * The callback can return -1 to force the iterator to stop, which by default
 * keeps going till the bottom-most frame.
 *-------------------------------------------------------------------------
 */

/* Call-back which plugs into unwinding core to dump the stack in
 * case of panic/OOPs/BUG etc
 */
static int __print_sym(unsigned int address, void *unused)
{
	__print_symbol("  %s\n", address);
	return 0;
}

#ifdef CONFIG_STACKTRACE

/* Call-back which plugs into unwinding core to capture the
 * traces needed by kernel on /proc/<pid>/stack
 */
static int __collect_all(unsigned int address, void *arg)
{
	struct stack_trace *trace = arg;

	if (trace->skip > 0)
		trace->skip--;
	else
		trace->entries[trace->nr_entries++] = address;

	if (trace->nr_entries >= trace->max_entries)
		return -1;

	return 0;
}

static int __collect_all_but_sched(unsigned int address, void *arg)
{
	struct stack_trace *trace = arg;

	if (in_sched_functions(address))
		return 0;

	if (trace->skip > 0)
		trace->skip--;
	else
		trace->entries[trace->nr_entries++] = address;

	if (trace->nr_entries >= trace->max_entries)
		return -1;

	return 0;
}

#endif

static int __get_first_nonsched(unsigned int address, void *unused)
{
	if (in_sched_functions(address))
		return 0;

	return -1;
}

/*-------------------------------------------------------------------------
 *              APIs expected by various kernel sub-systems
 *-------------------------------------------------------------------------
 */

noinline void show_stacktrace(struct task_struct *tsk, struct pt_regs *regs)
{
	pr_info("\nStack Trace:\n");
	arc_unwind_core(tsk, regs, __print_sym, NULL);
}
EXPORT_SYMBOL(show_stacktrace);

/* Expected by sched Code */
void show_stack(struct task_struct *tsk, unsigned long *sp)
{
	show_stacktrace(tsk, NULL);
}

/* Another API expected by schedular, shows up in "ps" as Wait Channel
 * Of course just returning schedule( ) would be pointless so unwind until
 * the function is not in schedular code
 */
unsigned int get_wchan(struct task_struct *tsk)
{
	return arc_unwind_core(tsk, NULL, __get_first_nonsched, NULL);
}

#ifdef CONFIG_STACKTRACE

/*
 * API required by CONFIG_STACKTRACE, CONFIG_LATENCYTOP.
 * A typical use is when /proc/<pid>/stack is queried by userland
 */
void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	/* Assumes @tsk is sleeping so unwinds from __switch_to */
	arc_unwind_core(tsk, NULL, __collect_all_but_sched, trace);
}

void save_stack_trace(struct stack_trace *trace)
{
	/* Pass NULL for task so it unwinds the current call frame */
	arc_unwind_core(NULL, NULL, __collect_all, trace);
}
EXPORT_SYMBOL_GPL(save_stack_trace);
#endif
