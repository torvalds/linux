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
		     int (*fn)(struct stackframe *, void *), void *data)
{
	while (1) {
		int ret;

		if (fn(frame, data))
			break;
		ret = unwind_frame(frame);
		if (ret < 0)
			break;
	}
}
EXPORT_SYMBOL(walk_stackframe);

#ifdef CONFIG_STACKTRACE
struct stack_trace_data {
	struct stack_trace *trace;
	unsigned int no_sched_functions;
	unsigned int skip;
};

static int save_trace(struct stackframe *frame, void *d)
{
	struct stack_trace_data *data = d;
	struct stack_trace *trace = data->trace;
	unsigned long addr = frame->pc;

	if (data->no_sched_functions && in_sched_functions(addr))
		return 0;
	if (data->skip) {
		data->skip--;
		return 0;
	}

	trace->entries[trace->nr_entries++] = addr;
	return trace->nr_entries >= trace->max_entries;
}

/* This must be noinline to so that our skip calculation works correctly */
static noinline void __save_stack_trace(struct task_struct *tsk,
	struct stack_trace *trace, unsigned int nosched)
{
	struct stack_trace_data data;
	struct stackframe frame;

	data.trace = trace;
	data.skip = trace->skip;
	data.no_sched_functions = nosched;

	if (tsk != current) {
#ifdef CONFIG_SMP
		/*
		 * What guarantees do we have here that 'tsk' is not
		 * running on another CPU?  For now, ignore it as we
		 * can't guarantee we won't explode.
		 */
		return;
#else
		frame.fp = thread_saved_fp(tsk);
		frame.sp = thread_saved_sp(tsk);
		frame.lr = 0;		/* recovered from the stack */
		frame.pc = thread_saved_pc(tsk);
#endif
	} else {
		/* We don't want this function nor the caller */
		data.skip += 2;
		frame.fp = (unsigned long)__builtin_frame_address(0);
		frame.sp = current_stack_pointer;
		frame.lr = (unsigned long)__builtin_return_address(0);
here:
		frame.pc = (unsigned long)&&here;
	}
#ifdef CONFIG_KRETPROBES
	frame.kr_cur = NULL;
	frame.tsk = tsk;
#endif
#ifdef CONFIG_UNWINDER_FRAME_POINTER
	frame.ex_frame = false;
#endif

	walk_stackframe(&frame, save_trace, &data);
}

void save_stack_trace_regs(struct pt_regs *regs, struct stack_trace *trace)
{
	struct stack_trace_data data;
	struct stackframe frame;

	data.trace = trace;
	data.skip = trace->skip;
	data.no_sched_functions = 0;

	frame.fp = regs->ARM_fp;
	frame.sp = regs->ARM_sp;
	frame.lr = regs->ARM_lr;
	frame.pc = regs->ARM_pc;
#ifdef CONFIG_KRETPROBES
	frame.kr_cur = NULL;
	frame.tsk = current;
#endif
#ifdef CONFIG_UNWINDER_FRAME_POINTER
	frame.ex_frame = in_entry_text(frame.pc);
#endif

	walk_stackframe(&frame, save_trace, &data);
}

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	__save_stack_trace(tsk, trace, 1);
}
EXPORT_SYMBOL(save_stack_trace_tsk);

void save_stack_trace(struct stack_trace *trace)
{
	__save_stack_trace(current, trace, 0);
}
EXPORT_SYMBOL_GPL(save_stack_trace);
#endif
