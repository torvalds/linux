// SPDX-License-Identifier: GPL-2.0-only
#include <linux/export.h>
#include <linux/kprobes.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/stacktrace.h>

#include <asm/sections.h>
#include <asm/stacktrace.h>
#include <asm/traps.h>

#include "reboot.h"

#if defined(CONFIG_FRAME_POINTER) && !defined(CONFIG_ARM_UNWIND)
/*
 * Unwind the current stack frame and store the new register values in the
 * structure passed as argument. Unwinding is equivalent to a function return,
 * hence the new PC value rather than LR should be used for backtrace.
 *
 * With framepointer enabled, a simple function prologue looks like this:
 *	mov	ip, sp
 *	stmdb	sp!, {fp, ip, lr, pc}
 *	sub	fp, ip, #4
 *
 * A simple function epilogue looks like this:
 *	ldm	sp, {fp, sp, pc}
 *
 * When compiled with clang, pc and sp are not pushed. A simple function
 * prologue looks like this when built with clang:
 *
 *	stmdb	{..., fp, lr}
 *	add	fp, sp, #x
 *	sub	sp, sp, #y
 *
 * A simple function epilogue looks like this when built with clang:
 *
 *	sub	sp, fp, #x
 *	ldm	{..., fp, pc}
 *
 *
 * Note that with framepointer enabled, even the leaf functions have the same
 * prologue and epilogue, therefore we can ignore the LR value in this case.
 */

extern unsigned long call_with_stack_end;

static int frame_pointer_check(struct stackframe *frame)
{
	unsigned long high, low;
	unsigned long fp = frame->fp;
	unsigned long pc = frame->pc;

	/*
	 * call_with_stack() is the only place we allow SP to jump from one
	 * stack to another, with FP and SP pointing to different stacks,
	 * skipping the FP boundary check at this point.
	 */
	if (pc >= (unsigned long)&call_with_stack &&
			pc < (unsigned long)&call_with_stack_end)
		return 0;

	/* only go to a higher address on the stack */
	low = frame->sp;
	high = ALIGN(low, THREAD_SIZE);

	/* check current frame pointer is within bounds */
#ifdef CONFIG_CC_IS_CLANG
	if (fp < low + 4 || fp > high - 4)
		return -EINVAL;
#else
	if (fp < low + 12 || fp > high - 4)
		return -EINVAL;
#endif

	return 0;
}

int notrace unwind_frame(struct stackframe *frame)
{
	unsigned long fp = frame->fp;

	if (frame_pointer_check(frame))
		return -EINVAL;

	/*
	 * When we unwind through an exception stack, include the saved PC
	 * value into the stack trace.
	 */
	if (frame->ex_frame) {
		struct pt_regs *regs = (struct pt_regs *)frame->sp;

		/*
		 * We check that 'regs + sizeof(struct pt_regs)' (that is,
		 * &regs[1]) does not exceed the bottom of the stack to avoid
		 * accessing data outside the task's stack. This may happen
		 * when frame->ex_frame is a false positive.
		 */
		if ((unsigned long)&regs[1] > ALIGN(frame->sp, THREAD_SIZE))
			return -EINVAL;

		frame->pc = regs->ARM_pc;
		frame->ex_frame = false;
		return 0;
	}

	/* restore the registers from the stack frame */
#ifdef CONFIG_CC_IS_CLANG
	frame->sp = frame->fp;
	frame->fp = READ_ONCE_NOCHECK(*(unsigned long *)(fp));
	frame->pc = READ_ONCE_NOCHECK(*(unsigned long *)(fp + 4));
#else
	frame->fp = READ_ONCE_NOCHECK(*(unsigned long *)(fp - 12));
	frame->sp = READ_ONCE_NOCHECK(*(unsigned long *)(fp - 8));
	frame->pc = READ_ONCE_NOCHECK(*(unsigned long *)(fp - 4));
#endif
#ifdef CONFIG_KRETPROBES
	if (is_kretprobe_trampoline(frame->pc))
		frame->pc = kretprobe_find_ret_addr(frame->tsk,
					(void *)frame->fp, &frame->kr_cur);
#endif

	if (in_entry_text(frame->pc))
		frame->ex_frame = true;

	return 0;
}
#endif

void notrace walk_stackframe(struct stackframe *frame,
		     bool (*fn)(void *, unsigned long), void *data)
{
	while (1) {
		int ret;

		if (!fn(data, frame->pc))
			break;
		ret = unwind_frame(frame);
		if (ret < 0)
			break;
	}
}
EXPORT_SYMBOL(walk_stackframe);

#ifdef CONFIG_STACKTRACE
static void start_stack_trace(struct stackframe *frame, struct task_struct *task,
			      unsigned long fp, unsigned long sp,
			      unsigned long lr, unsigned long pc)
{
	frame->fp = fp;
	frame->sp = sp;
	frame->lr = lr;
	frame->pc = pc;
#ifdef CONFIG_KRETPROBES
	frame->kr_cur = NULL;
	frame->tsk = task;
#endif
#ifdef CONFIG_UNWINDER_FRAME_POINTER
	frame->ex_frame = in_entry_text(frame->pc);
#endif
}

void arch_stack_walk(stack_trace_consume_fn consume_entry, void *cookie,
		     struct task_struct *task, struct pt_regs *regs)
{
	struct stackframe frame;

	if (regs) {
		start_stack_trace(&frame, NULL, regs->ARM_fp, regs->ARM_sp,
				  regs->ARM_lr, regs->ARM_pc);
	} else if (task != current) {
#ifdef CONFIG_SMP
		/*
		 * What guarantees do we have here that 'tsk' is not
		 * running on another CPU?  For now, ignore it as we
		 * can't guarantee we won't explode.
		 */
		return;
#else
		start_stack_trace(&frame, task, thread_saved_fp(task),
				  thread_saved_sp(task), 0,
				  thread_saved_pc(task));
#endif
	} else {
here:
		start_stack_trace(&frame, task,
				  (unsigned long)__builtin_frame_address(0),
				  current_stack_pointer,
				  (unsigned long)__builtin_return_address(0),
				  (unsigned long)&&here);
		/* skip this function */
		if (unwind_frame(&frame))
			return;
	}

	walk_stackframe(&frame, consume_entry, cookie);
}
#endif
