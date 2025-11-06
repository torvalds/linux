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

/*
 * This disables KASAN checking when reading a value from another task's stack,
 * since the other task could be running on another CPU and could have poisoned
 * the stack in the meantime.
 */
#define READ_ONCE_TASK_STACK(task, x)			\
({							\
	unsigned long val;				\
	unsigned long addr = x;				\
	if ((task) == current)				\
		val = READ_ONCE(addr);			\
	else						\
		val = READ_ONCE_NOCHECK(addr);		\
	val;						\
})

extern asmlinkage void handle_exception(void);
extern unsigned long ret_from_exception_end;

static inline int fp_is_valid(unsigned long fp, unsigned long sp)
{
	unsigned long low, high;

	low = sp + sizeof(struct stackframe);
	high = ALIGN(sp, THREAD_SIZE);

	return !(fp < low || fp > high || fp & 0x07);
}

void notrace walk_stackframe(struct task_struct *task, struct pt_regs *regs,
			     bool (*fn)(void *, unsigned long), void *arg)
{
	unsigned long fp, sp, pc;
	int graph_idx = 0;
	int level = 0;

	if (regs) {
		fp = frame_pointer(regs);
		sp = user_stack_pointer(regs);
		pc = instruction_pointer(regs);
	} else if (task == NULL || task == current) {
		fp = (unsigned long)__builtin_frame_address(0);
		sp = current_stack_pointer;
		pc = (unsigned long)walk_stackframe;
		level = -1;
	} else {
		/* task blocked in __switch_to */
		fp = task->thread.s[0];
		sp = task->thread.sp;
		pc = task->thread.ra;
	}

	for (;;) {
		struct stackframe *frame;

		if (unlikely(!__kernel_text_address(pc) || (level++ >= 0 && !fn(arg, pc))))
			break;

		if (unlikely(!fp_is_valid(fp, sp)))
			break;

		/* Unwind stack frame */
		frame = (struct stackframe *)fp - 1;
		sp = fp;
		if (regs && (regs->epc == pc) && fp_is_valid(frame->ra, sp)) {
			/* We hit function where ra is not saved on the stack */
			fp = frame->ra;
			pc = regs->ra;
		} else {
			fp = READ_ONCE_TASK_STACK(task, frame->fp);
			pc = READ_ONCE_TASK_STACK(task, frame->ra);
			pc = ftrace_graph_ret_addr(current, &graph_idx, pc,
						   &frame->ra);
			if (pc >= (unsigned long)handle_exception &&
			    pc < (unsigned long)&ret_from_exception_end) {
				if (unlikely(!fn(arg, pc)))
					break;

				pc = ((struct pt_regs *)sp)->epc;
				fp = ((struct pt_regs *)sp)->s0;
			}
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
		pc = READ_ONCE_NOCHECK(*ksp++) - 0x4;
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

noinline noinstr void arch_stack_walk(stack_trace_consume_fn consume_entry, void *cookie,
		     struct task_struct *task, struct pt_regs *regs)
{
	walk_stackframe(task, regs, consume_entry, cookie);
}

/*
 * Get the return address for a single stackframe and return a pointer to the
 * next frame tail.
 */
static unsigned long unwind_user_frame(stack_trace_consume_fn consume_entry,
				       void *cookie, unsigned long fp,
				       unsigned long reg_ra)
{
	struct stackframe buftail;
	unsigned long ra = 0;
	unsigned long __user *user_frame_tail =
		(unsigned long __user *)(fp - sizeof(struct stackframe));

	/* Check accessibility of one struct frame_tail beyond */
	if (!access_ok(user_frame_tail, sizeof(buftail)))
		return 0;
	if (__copy_from_user_inatomic(&buftail, user_frame_tail,
				      sizeof(buftail)))
		return 0;

	ra = reg_ra ? : buftail.ra;

	fp = buftail.fp;
	if (!ra || !consume_entry(cookie, ra))
		return 0;

	return fp;
}

void arch_stack_walk_user(stack_trace_consume_fn consume_entry, void *cookie,
			  const struct pt_regs *regs)
{
	unsigned long fp = 0;

	fp = regs->s0;
	if (!consume_entry(cookie, regs->epc))
		return;

	fp = unwind_user_frame(consume_entry, cookie, fp, regs->ra);
	while (fp && !(fp & 0x7))
		fp = unwind_user_frame(consume_entry, cookie, fp, 0);
}
