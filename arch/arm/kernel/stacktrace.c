// SPDX-License-Identifier: GPL-2.0-only
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/stacktrace.h>

#include <asm/sections.h>
#include <asm/stacktrace.h>
#include <asm/traps.h>

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
int notrace unwind_frame(struct stackframe *frame)
{
	unsigned long high, low;
	unsigned long fp = frame->fp;

	/* only go to a higher address on the stack */
	low = frame->sp;
	high = ALIGN(low, THREAD_SIZE);

#ifdef CONFIG_CC_IS_CLANG
	/* check current frame pointer is within bounds */
	if (fp < low + 4 || fp > high - 4)
		return -EINVAL;

	frame->sp = frame->fp;
	frame->fp = READ_ONCE_NOCHECK(*(unsigned long *)(fp));
	frame->pc = READ_ONCE_NOCHECK(*(unsigned long *)(fp + 4));
#else
	/* check current frame pointer is within bounds */
	if (fp < low + 12 || fp > high - 4)
		return -EINVAL;

	/* restore the registers from the stack frame */
	frame->fp = READ_ONCE_NOCHECK(*(unsigned long *)(fp - 12));
	frame->sp = READ_ONCE_NOCHECK(*(unsigned long *)(fp - 8));
	frame->pc = READ_ONCE_NOCHECK(*(unsigned long *)(fp - 4));
#endif

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
	struct pt_regs *regs;
	unsigned long addr = frame->pc;

	if (data->no_sched_functions && in_sched_functions(addr))
		return 0;
	if (data->skip) {
		data->skip--;
		return 0;
	}

	trace->entries[trace->nr_entries++] = addr;

	if (trace->nr_entries >= trace->max_entries)
		return 1;

	if (!in_entry_text(frame->pc))
		return 0;

	regs = (struct pt_regs *)frame->sp;
	if ((unsigned long)&regs[1] > ALIGN(frame->sp, THREAD_SIZE))
		return 0;

	trace->entries[trace->nr_entries++] = regs->ARM_pc;

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
		frame.pc = (unsigned long)__save_stack_trace;
	}

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
