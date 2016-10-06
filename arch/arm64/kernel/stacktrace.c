/*
 * Stack tracing support
 *
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/ftrace.h>
#include <linux/sched.h>
#include <linux/stacktrace.h>

#include <asm/irq.h>
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
int notrace unwind_frame(struct task_struct *tsk, struct stackframe *frame)
{
	unsigned long high, low;
	unsigned long fp = frame->fp;
	unsigned long irq_stack_ptr;

	if (!tsk)
		tsk = current;

	/*
	 * Switching between stacks is valid when tracing current and in
	 * non-preemptible context.
	 */
	if (tsk == current && !preemptible())
		irq_stack_ptr = IRQ_STACK_PTR(smp_processor_id());
	else
		irq_stack_ptr = 0;

	low  = frame->sp;
	/* irq stacks are not THREAD_SIZE aligned */
	if (on_irq_stack(frame->sp, raw_smp_processor_id()))
		high = irq_stack_ptr;
	else
		high = ALIGN(low, THREAD_SIZE) - 0x20;

	if (fp < low || fp > high || fp & 0xf)
		return -EINVAL;

	frame->sp = fp + 0x10;
	frame->fp = READ_ONCE_NOCHECK(*(unsigned long *)(fp));
	frame->pc = READ_ONCE_NOCHECK(*(unsigned long *)(fp + 8));

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (tsk->ret_stack &&
			(frame->pc == (unsigned long)return_to_handler)) {
		/*
		 * This is a case where function graph tracer has
		 * modified a return address (LR) in a stack frame
		 * to hook a function return.
		 * So replace it to an original value.
		 */
		frame->pc = tsk->ret_stack[frame->graph--].ret;
	}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

	/*
	 * Check whether we are going to walk through from interrupt stack
	 * to task stack.
	 * If we reach the end of the stack - and its an interrupt stack,
	 * unpack the dummy frame to find the original elr.
	 *
	 * Check the frame->fp we read from the bottom of the irq_stack,
	 * and the original task stack pointer are both in current->stack.
	 */
	if (frame->sp == irq_stack_ptr) {
		struct pt_regs *irq_args;
		unsigned long orig_sp = IRQ_STACK_TO_TASK_STACK(irq_stack_ptr);

		if (object_is_on_stack((void *)orig_sp) &&
		   object_is_on_stack((void *)frame->fp)) {
			frame->sp = orig_sp;

			/* orig_sp is the saved pt_regs, find the elr */
			irq_args = (struct pt_regs *)orig_sp;
			frame->pc = irq_args->pc;
		} else {
			/*
			 * This frame has a non-standard format, and we
			 * didn't fix it, because the data looked wrong.
			 * Refuse to output this frame.
			 */
			return -EINVAL;
		}
	}

	return 0;
}

void notrace walk_stackframe(struct task_struct *tsk, struct stackframe *frame,
		     int (*fn)(struct stackframe *, void *), void *data)
{
	while (1) {
		int ret;

		if (fn(frame, data))
			break;
		ret = unwind_frame(tsk, frame);
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

void save_stack_trace_regs(struct pt_regs *regs, struct stack_trace *trace)
{
	struct stack_trace_data data;
	struct stackframe frame;

	data.trace = trace;
	data.skip = trace->skip;
	data.no_sched_functions = 0;

	frame.fp = regs->regs[29];
	frame.sp = regs->sp;
	frame.pc = regs->pc;
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	frame.graph = current->curr_ret_stack;
#endif

	walk_stackframe(current, &frame, save_trace, &data);
	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = ULONG_MAX;
}

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	struct stack_trace_data data;
	struct stackframe frame;

	data.trace = trace;
	data.skip = trace->skip;

	if (tsk != current) {
		data.no_sched_functions = 1;
		frame.fp = thread_saved_fp(tsk);
		frame.sp = thread_saved_sp(tsk);
		frame.pc = thread_saved_pc(tsk);
	} else {
		data.no_sched_functions = 0;
		frame.fp = (unsigned long)__builtin_frame_address(0);
		frame.sp = current_stack_pointer;
		frame.pc = (unsigned long)save_stack_trace_tsk;
	}
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	frame.graph = tsk->curr_ret_stack;
#endif

	walk_stackframe(tsk, &frame, save_trace, &data);
	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = ULONG_MAX;
}

void save_stack_trace(struct stack_trace *trace)
{
	save_stack_trace_tsk(current, trace);
}
EXPORT_SYMBOL_GPL(save_stack_trace);
#endif
