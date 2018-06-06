/*
 * Copyright (C) 2008 ARM Limited
 * Copyright (C) 2014 Regents of the University of California
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/export.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>
#include <linux/ftrace.h>

#ifdef CONFIG_FRAME_POINTER

struct stackframe {
	unsigned long fp;
	unsigned long ra;
};

static void notrace walk_stackframe(struct task_struct *task,
	struct pt_regs *regs, bool (*fn)(unsigned long, void *), void *arg)
{
	unsigned long fp, sp, pc;

	if (regs) {
		fp = GET_FP(regs);
		sp = GET_USP(regs);
		pc = GET_IP(regs);
	} else if (task == NULL || task == current) {
		const register unsigned long current_sp __asm__ ("sp");
		fp = (unsigned long)__builtin_frame_address(0);
		sp = current_sp;
		pc = (unsigned long)walk_stackframe;
	} else {
		/* task blocked in __switch_to */
		fp = task->thread.s[0];
		sp = task->thread.sp;
		pc = task->thread.ra;
	}

	for (;;) {
		unsigned long low, high;
		struct stackframe *frame;

		if (unlikely(!__kernel_text_address(pc) || fn(pc, arg)))
			break;

		/* Validate frame pointer */
		low = sp + sizeof(struct stackframe);
		high = ALIGN(sp, THREAD_SIZE);
		if (unlikely(fp < low || fp > high || fp & 0x7))
			break;
		/* Unwind stack frame */
		frame = (struct stackframe *)fp - 1;
		sp = fp;
		fp = frame->fp;
#ifdef HAVE_FUNCTION_GRAPH_RET_ADDR_PTR
		pc = ftrace_graph_ret_addr(current, NULL, frame->ra,
					   (unsigned long *)(fp - 8));
#else
		pc = frame->ra - 0x4;
#endif
	}
}

#else /* !CONFIG_FRAME_POINTER */

static void notrace walk_stackframe(struct task_struct *task,
	struct pt_regs *regs, bool (*fn)(unsigned long, void *), void *arg)
{
	unsigned long sp, pc;
	unsigned long *ksp;

	if (regs) {
		sp = GET_USP(regs);
		pc = GET_IP(regs);
	} else if (task == NULL || task == current) {
		const register unsigned long current_sp __asm__ ("sp");
		sp = current_sp;
		pc = (unsigned long)walk_stackframe;
	} else {
		/* task blocked in __switch_to */
		sp = task->thread.sp;
		pc = task->thread.ra;
	}

	if (unlikely(sp & 0x7))
		return;

	ksp = (unsigned long *)sp;
	while (!kstack_end(ksp)) {
		if (__kernel_text_address(pc) && unlikely(fn(pc, arg)))
			break;
		pc = (*ksp++) - 0x4;
	}
}

#endif /* CONFIG_FRAME_POINTER */


static bool print_trace_address(unsigned long pc, void *arg)
{
	print_ip_sym(pc);
	return false;
}

void show_stack(struct task_struct *task, unsigned long *sp)
{
	pr_cont("Call Trace:\n");
	walk_stackframe(task, NULL, print_trace_address, NULL);
}


static bool save_wchan(unsigned long pc, void *arg)
{
	if (!in_sched_functions(pc)) {
		unsigned long *p = arg;
		*p = pc;
		return true;
	}
	return false;
}

unsigned long get_wchan(struct task_struct *task)
{
	unsigned long pc = 0;

	if (likely(task && task != current && task->state != TASK_RUNNING))
		walk_stackframe(task, NULL, save_wchan, &pc);
	return pc;
}


#ifdef CONFIG_STACKTRACE

static bool __save_trace(unsigned long pc, void *arg, bool nosched)
{
	struct stack_trace *trace = arg;

	if (unlikely(nosched && in_sched_functions(pc)))
		return false;
	if (unlikely(trace->skip > 0)) {
		trace->skip--;
		return false;
	}

	trace->entries[trace->nr_entries++] = pc;
	return (trace->nr_entries >= trace->max_entries);
}

static bool save_trace(unsigned long pc, void *arg)
{
	return __save_trace(pc, arg, false);
}

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	walk_stackframe(tsk, NULL, save_trace, trace);
	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = ULONG_MAX;
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);

void save_stack_trace(struct stack_trace *trace)
{
	save_stack_trace_tsk(NULL, trace);
}
EXPORT_SYMBOL_GPL(save_stack_trace);

#endif /* CONFIG_STACKTRACE */
