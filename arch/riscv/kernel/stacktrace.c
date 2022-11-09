// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2008 ARM Limited
 * Copyright (C) 2014 Regents of the University of California
 */

#include <linux/export.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>
#include <linux/ftrace.h>

#include <asm/stacktrace.h>

#ifdef CONFIG_FRAME_POINTER

void notrace walk_stackframe(struct task_struct *task, struct pt_regs *regs,
			     bool (*fn)(void *, unsigned long), void *arg)
{
	unsigned long fp, sp, pc;
	int level = 0;

	if (regs) {
		fp = frame_pointer(regs);
		sp = user_stack_pointer(regs);
		pc = instruction_pointer(regs);
	} else if (task == NULL || task == current) {
		fp = (unsigned long)__builtin_frame_address(0);
		sp = current_stack_pointer;
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

		if (unlikely(!__kernel_text_address(pc) || (level++ >= 1 && !fn(arg, pc))))
			break;

		/* Validate frame pointer */
		low = sp + sizeof(struct stackframe);
		high = ALIGN(sp, THREAD_SIZE);
		if (unlikely(fp < low || fp > high || fp & 0x7))
			break;
		/* Unwind stack frame */
		frame = (struct stackframe *)fp - 1;
		sp = fp;
		if (regs && (regs->epc == pc) && (frame->fp & 0x7)) {
			fp = frame->ra;
			pc = regs->ra;
		} else {
			fp = frame->fp;
			pc = ftrace_graph_ret_addr(current, NULL, frame->ra,
						   &frame->ra);
		}

	}
}

#else /* !CONFIG_FRAME_POINTER */

void notrace walk_stackframe(struct task_struct *task,
	struct pt_regs *regs, bool (*fn)(void *, unsigned long), void *arg)
{
	unsigned long sp, pc;
	unsigned long *ksp;

	if (regs) {
		sp = user_stack_pointer(regs);
		pc = instruction_pointer(regs);
	} else if (task == NULL || task == current) {
		sp = current_stack_pointer;
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
		if (__kernel_text_address(pc) && unlikely(!fn(arg, pc)))
			break;
		pc = (*ksp++) - 0x4;
	}
}

#endif /* CONFIG_FRAME_POINTER */

static bool print_trace_address(void *arg, unsigned long pc)
{
	const char *loglvl = arg;

	print_ip_sym(loglvl, pc);
	return true;
}

noinline void dump_backtrace(struct pt_regs *regs, struct task_struct *task,
		    const char *loglvl)
{
	walk_stackframe(task, regs, print_trace_address, (void *)loglvl);
}

void show_stack(struct task_struct *task, unsigned long *sp, const char *loglvl)
{
	pr_cont("%sCall Trace:\n", loglvl);
	dump_backtrace(NULL, task, loglvl);
}

static bool save_wchan(void *arg, unsigned long pc)
{
	if (!in_sched_functions(pc)) {
		unsigned long *p = arg;
		*p = pc;
		return false;
	}
	return true;
}

unsigned long __get_wchan(struct task_struct *task)
{
	unsigned long pc = 0;

	if (!try_get_task_stack(task))
		return 0;
	walk_stackframe(task, NULL, save_wchan, &pc);
	put_task_stack(task);
	return pc;
}

noinline void arch_stack_walk(stack_trace_consume_fn consume_entry, void *cookie,
		     struct task_struct *task, struct pt_regs *regs)
{
	walk_stackframe(task, regs, consume_entry, cookie);
}
