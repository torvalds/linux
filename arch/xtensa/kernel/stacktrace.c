/*
 * Kernel and userspace stack tracing.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2013 Tensilica Inc.
 * Copyright (C) 2015 Cadence Design Systems Inc.
 */
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/stacktrace.h>

#include <asm/stacktrace.h>
#include <asm/traps.h>
#include <linux/uaccess.h>

#if IS_ENABLED(CONFIG_OPROFILE) || IS_ENABLED(CONFIG_PERF_EVENTS)

/* Address of common_exception_return, used to check the
 * transition from kernel to user space.
 */
extern int common_exception_return;

void xtensa_backtrace_user(struct pt_regs *regs, unsigned int depth,
			   int (*ufn)(struct stackframe *frame, void *data),
			   void *data)
{
	unsigned long windowstart = regs->windowstart;
	unsigned long windowbase = regs->windowbase;
	unsigned long a0 = regs->areg[0];
	unsigned long a1 = regs->areg[1];
	unsigned long pc = regs->pc;
	struct stackframe frame;
	int index;

	if (!depth--)
		return;

	frame.pc = pc;
	frame.sp = a1;

	if (pc == 0 || pc >= TASK_SIZE || ufn(&frame, data))
		return;

	if (IS_ENABLED(CONFIG_USER_ABI_CALL0_ONLY) ||
	    (IS_ENABLED(CONFIG_USER_ABI_CALL0_PROBE) &&
	     !(regs->ps & PS_WOE_MASK)))
		return;

	/* Two steps:
	 *
	 * 1. Look through the register window for the
	 * previous PCs in the call trace.
	 *
	 * 2. Look on the stack.
	 */

	/* Step 1.  */
	/* Rotate WINDOWSTART to move the bit corresponding to
	 * the current window to the bit #0.
	 */
	windowstart = (windowstart << WSBITS | windowstart) >> windowbase;

	/* Look for bits that are set, they correspond to
	 * valid windows.
	 */
	for (index = WSBITS - 1; (index > 0) && depth; depth--, index--)
		if (windowstart & (1 << index)) {
			/* Get the PC from a0 and a1. */
			pc = MAKE_PC_FROM_RA(a0, pc);
			/* Read a0 and a1 from the
			 * corresponding position in AREGs.
			 */
			a0 = regs->areg[index * 4];
			a1 = regs->areg[index * 4 + 1];

			frame.pc = pc;
			frame.sp = a1;

			if (pc == 0 || pc >= TASK_SIZE || ufn(&frame, data))
				return;
		}

	/* Step 2. */
	/* We are done with the register window, we need to
	 * look through the stack.
	 */
	if (!depth)
		return;

	/* Start from the a1 register. */
	/* a1 = regs->areg[1]; */
	while (a0 != 0 && depth--) {
		pc = MAKE_PC_FROM_RA(a0, pc);

		/* Check if the region is OK to access. */
		if (!access_ok(&SPILL_SLOT(a1, 0), 8))
			return;
		/* Copy a1, a0 from user space stack frame. */
		if (__get_user(a0, &SPILL_SLOT(a1, 0)) ||
		    __get_user(a1, &SPILL_SLOT(a1, 1)))
			return;

		frame.pc = pc;
		frame.sp = a1;

		if (pc == 0 || pc >= TASK_SIZE || ufn(&frame, data))
			return;
	}
}
EXPORT_SYMBOL(xtensa_backtrace_user);

void xtensa_backtrace_kernel(struct pt_regs *regs, unsigned int depth,
			     int (*kfn)(struct stackframe *frame, void *data),
			     int (*ufn)(struct stackframe *frame, void *data),
			     void *data)
{
	unsigned long pc = regs->depc > VALID_DOUBLE_EXCEPTION_ADDRESS ?
		regs->depc : regs->pc;
	unsigned long sp_start, sp_end;
	unsigned long a0 = regs->areg[0];
	unsigned long a1 = regs->areg[1];

	sp_start = a1 & ~(THREAD_SIZE - 1);
	sp_end = sp_start + THREAD_SIZE;

	/* Spill the register window to the stack first. */
	spill_registers();

	/* Read the stack frames one by one and create the PC
	 * from the a0 and a1 registers saved there.
	 */
	while (a1 > sp_start && a1 < sp_end && depth--) {
		struct stackframe frame;

		frame.pc = pc;
		frame.sp = a1;

		if (kernel_text_address(pc) && kfn(&frame, data))
			return;

		if (pc == (unsigned long)&common_exception_return) {
			regs = (struct pt_regs *)a1;
			if (user_mode(regs)) {
				if (ufn == NULL)
					return;
				xtensa_backtrace_user(regs, depth, ufn, data);
				return;
			}
			a0 = regs->areg[0];
			a1 = regs->areg[1];
			continue;
		}

		sp_start = a1;

		pc = MAKE_PC_FROM_RA(a0, pc);
		a0 = SPILL_SLOT(a1, 0);
		a1 = SPILL_SLOT(a1, 1);
	}
}
EXPORT_SYMBOL(xtensa_backtrace_kernel);

#endif

void walk_stackframe(unsigned long *sp,
		int (*fn)(struct stackframe *frame, void *data),
		void *data)
{
	unsigned long a0, a1;
	unsigned long sp_end;

	a1 = (unsigned long)sp;
	sp_end = ALIGN(a1, THREAD_SIZE);

	spill_registers();

	while (a1 < sp_end) {
		struct stackframe frame;

		sp = (unsigned long *)a1;

		a0 = SPILL_SLOT(a1, 0);
		a1 = SPILL_SLOT(a1, 1);

		if (a1 <= (unsigned long)sp)
			break;

		frame.pc = MAKE_PC_FROM_RA(a0, a1);
		frame.sp = a1;

		if (fn(&frame, data))
			return;
	}
}

#ifdef CONFIG_STACKTRACE

struct stack_trace_data {
	struct stack_trace *trace;
	unsigned skip;
};

static int stack_trace_cb(struct stackframe *frame, void *data)
{
	struct stack_trace_data *trace_data = data;
	struct stack_trace *trace = trace_data->trace;

	if (trace_data->skip) {
		--trace_data->skip;
		return 0;
	}
	if (!kernel_text_address(frame->pc))
		return 0;

	trace->entries[trace->nr_entries++] = frame->pc;
	return trace->nr_entries >= trace->max_entries;
}

void save_stack_trace_tsk(struct task_struct *task, struct stack_trace *trace)
{
	struct stack_trace_data trace_data = {
		.trace = trace,
		.skip = trace->skip,
	};
	walk_stackframe(stack_pointer(task), stack_trace_cb, &trace_data);
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);

void save_stack_trace(struct stack_trace *trace)
{
	save_stack_trace_tsk(current, trace);
}
EXPORT_SYMBOL_GPL(save_stack_trace);

#endif

#ifdef CONFIG_FRAME_POINTER

struct return_addr_data {
	unsigned long addr;
	unsigned skip;
};

static int return_address_cb(struct stackframe *frame, void *data)
{
	struct return_addr_data *r = data;

	if (r->skip) {
		--r->skip;
		return 0;
	}
	if (!kernel_text_address(frame->pc))
		return 0;
	r->addr = frame->pc;
	return 1;
}

/*
 * level == 0 is for the return address from the caller of this function,
 * not from this function itself.
 */
unsigned long return_address(unsigned level)
{
	struct return_addr_data r = {
		.skip = level,
	};
	walk_stackframe(stack_pointer(NULL), return_address_cb, &r);
	return r.addr;
}
EXPORT_SYMBOL(return_address);

#endif
