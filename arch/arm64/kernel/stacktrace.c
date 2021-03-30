// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stack tracing support
 *
 * Copyright (C) 2012 ARM Ltd.
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/ftrace.h>
#include <linux/kprobes.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>

#include <asm/irq.h>
#include <asm/pointer_auth.h>
#include <asm/stack_pointer.h>
#include <asm/stacktrace.h>

/*
 * AArch64 PCS assigns the frame pointer to x29.
 *
 * A simple function prologue looks like this:
 * 	sub	sp, sp, #0x10
 *   	stp	x29, x30, [sp]
 *	mov	x29, sp
 *
 * A simple function epilogue looks like this:
 *	mov	sp, x29
 *	ldp	x29, x30, [sp]
 *	add	sp, sp, #0x10
 */

/*
 * Unwind from one frame record (A) to the next frame record (B).
 *
 * We terminate early if the location of B indicates a malformed chain of frame
 * records (e.g. a cycle), determined based on the location and fp value of A
 * and the location (but not the fp value) of B.
 */
int notrace unwind_frame(struct task_struct *tsk, struct stackframe *frame)
{
	unsigned long fp = frame->fp;
	struct stack_info info;

	/* Terminal record; nothing to unwind */
	if (!fp)
		return -EINVAL;

	if (fp & 0xf)
		return -EINVAL;

	if (!tsk)
		tsk = current;

	if (!on_accessible_stack(tsk, fp, &info))
		return -EINVAL;

	if (test_bit(info.type, frame->stacks_done))
		return -EINVAL;

	/*
	 * As stacks grow downward, any valid record on the same stack must be
	 * at a strictly higher address than the prior record.
	 *
	 * Stacks can nest in several valid orders, e.g.
	 *
	 * TASK -> IRQ -> OVERFLOW -> SDEI_NORMAL
	 * TASK -> SDEI_NORMAL -> SDEI_CRITICAL -> OVERFLOW
	 *
	 * ... but the nesting itself is strict. Once we transition from one
	 * stack to another, it's never valid to unwind back to that first
	 * stack.
	 */
	if (info.type == frame->prev_type) {
		if (fp <= frame->prev_fp)
			return -EINVAL;
	} else {
		set_bit(frame->prev_type, frame->stacks_done);
	}

	/*
	 * Record this frame record's values and location. The prev_fp and
	 * prev_type are only meaningful to the next unwind_frame() invocation.
	 */
	frame->fp = READ_ONCE_NOCHECK(*(unsigned long *)(fp));
	frame->pc = READ_ONCE_NOCHECK(*(unsigned long *)(fp + 8));
	frame->prev_fp = fp;
	frame->prev_type = info.type;

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (tsk->ret_stack &&
		(ptrauth_strip_insn_pac(frame->pc) == (unsigned long)return_to_handler)) {
		struct ftrace_ret_stack *ret_stack;
		/*
		 * This is a case where function graph tracer has
		 * modified a return address (LR) in a stack frame
		 * to hook a function return.
		 * So replace it to an original value.
		 */
		ret_stack = ftrace_graph_get_ret_stack(tsk, frame->graph++);
		if (WARN_ON_ONCE(!ret_stack))
			return -EINVAL;
		frame->pc = ret_stack->ret;
	}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

	frame->pc = ptrauth_strip_insn_pac(frame->pc);

	return 0;
}
NOKPROBE_SYMBOL(unwind_frame);

void notrace walk_stackframe(struct task_struct *tsk, struct stackframe *frame,
			     bool (*fn)(void *, unsigned long), void *data)
{
	while (1) {
		int ret;

		if (!fn(data, frame->pc))
			break;
		ret = unwind_frame(tsk, frame);
		if (ret < 0)
			break;
	}
}
NOKPROBE_SYMBOL(walk_stackframe);

static void dump_backtrace_entry(unsigned long where, const char *loglvl)
{
	printk("%s %pS\n", loglvl, (void *)where);
}

void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk,
		    const char *loglvl)
{
	struct stackframe frame;
	int skip = 0;

	pr_debug("%s(regs = %p tsk = %p)\n", __func__, regs, tsk);

	if (regs) {
		if (user_mode(regs))
			return;
		skip = 1;
	}

	if (!tsk)
		tsk = current;

	if (!try_get_task_stack(tsk))
		return;

	if (tsk == current) {
		start_backtrace(&frame,
				(unsigned long)__builtin_frame_address(0),
				(unsigned long)dump_backtrace);
	} else {
		/*
		 * task blocked in __switch_to
		 */
		start_backtrace(&frame,
				thread_saved_fp(tsk),
				thread_saved_pc(tsk));
	}

	printk("%sCall trace:\n", loglvl);
	do {
		/* skip until specified stack frame */
		if (!skip) {
			dump_backtrace_entry(frame.pc, loglvl);
		} else if (frame.fp == regs->regs[29]) {
			skip = 0;
			/*
			 * Mostly, this is the case where this function is
			 * called in panic/abort. As exception handler's
			 * stack frame does not contain the corresponding pc
			 * at which an exception has taken place, use regs->pc
			 * instead.
			 */
			dump_backtrace_entry(regs->pc, loglvl);
		}
	} while (!unwind_frame(tsk, &frame));

	put_task_stack(tsk);
}

void show_stack(struct task_struct *tsk, unsigned long *sp, const char *loglvl)
{
	dump_backtrace(NULL, tsk, loglvl);
	barrier();
}

#ifdef CONFIG_STACKTRACE

void arch_stack_walk(stack_trace_consume_fn consume_entry, void *cookie,
		     struct task_struct *task, struct pt_regs *regs)
{
	struct stackframe frame;

	if (regs)
		start_backtrace(&frame, regs->regs[29], regs->pc);
	else if (task == current)
		start_backtrace(&frame,
				(unsigned long)__builtin_frame_address(0),
				(unsigned long)arch_stack_walk);
	else
		start_backtrace(&frame, thread_saved_fp(task),
				thread_saved_pc(task));

	walk_stackframe(task, &frame, consume_entry, cookie);
}

#endif
