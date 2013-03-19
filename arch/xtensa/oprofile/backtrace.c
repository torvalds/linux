/**
 * @file backtrace.c
 *
 * @remark Copyright 2008 Tensilica Inc.
 * @remark Read the file COPYING
 *
 */

#include <linux/oprofile.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>
#include <asm/traps.h>

/* Address of common_exception_return, used to check the
 * transition from kernel to user space.
 */
extern int common_exception_return;

/* A struct that maps to the part of the frame containing the a0 and
 * a1 registers.
 */
struct frame_start {
	unsigned long a0;
	unsigned long a1;
};

static void xtensa_backtrace_user(struct pt_regs *regs, unsigned int depth)
{
	unsigned long windowstart = regs->windowstart;
	unsigned long windowbase = regs->windowbase;
	unsigned long a0 = regs->areg[0];
	unsigned long a1 = regs->areg[1];
	unsigned long pc = MAKE_PC_FROM_RA(a0, regs->pc);
	int index;

	/* First add the current PC to the trace. */
	if (pc != 0 && pc <= TASK_SIZE)
		oprofile_add_trace(pc);
	else
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
			/* Read a0 and a1 from the
			 * corresponding position in AREGs.
			 */
			a0 = regs->areg[index * 4];
			a1 = regs->areg[index * 4 + 1];
			/* Get the PC from a0 and a1. */
			pc = MAKE_PC_FROM_RA(a0, pc);

			/* Add the PC to the trace. */
			if (pc != 0 && pc <= TASK_SIZE)
				oprofile_add_trace(pc);
			else
				return;
		}

	/* Step 2. */
	/* We are done with the register window, we need to
	 * look through the stack.
	 */
	if (depth > 0) {
		/* Start from the a1 register. */
		/* a1 = regs->areg[1]; */
		while (a0 != 0 && depth--) {

			struct frame_start frame_start;
			/* Get the location for a1, a0 for the
			 * previous frame from the current a1.
			 */
			unsigned long *psp = (unsigned long *)a1;
			psp -= 4;

			/* Check if the region is OK to access. */
			if (!access_ok(VERIFY_READ, psp, sizeof(frame_start)))
				return;
			/* Copy a1, a0 from user space stack frame. */
			if (__copy_from_user_inatomic(&frame_start, psp,
						sizeof(frame_start)))
				return;

			a0 = frame_start.a0;
			a1 = frame_start.a1;
			pc = MAKE_PC_FROM_RA(a0, pc);

			if (pc != 0 && pc <= TASK_SIZE)
				oprofile_add_trace(pc);
			else
				return;
		}
	}
}

static void xtensa_backtrace_kernel(struct pt_regs *regs, unsigned int depth)
{
	unsigned long pc = regs->pc;
	unsigned long *psp;
	unsigned long sp_start, sp_end;
	unsigned long a0 = regs->areg[0];
	unsigned long a1 = regs->areg[1];

	sp_start = a1 & ~(THREAD_SIZE-1);
	sp_end = sp_start + THREAD_SIZE;

	/* Spill the register window to the stack first. */
	spill_registers();

	/* Read the stack frames one by one and create the PC
	 * from the a0 and a1 registers saved there.
	 */
	while (a1 > sp_start && a1 < sp_end && depth--) {
		pc = MAKE_PC_FROM_RA(a0, pc);

		/* Add the PC to the trace. */
		if (kernel_text_address(pc))
			oprofile_add_trace(pc);

		if (pc == (unsigned long) &common_exception_return) {
			regs = (struct pt_regs *)a1;
			if (user_mode(regs)) {
				pc = regs->pc;
				if (pc != 0 && pc <= TASK_SIZE)
					oprofile_add_trace(pc);
				else
					return;
				return xtensa_backtrace_user(regs, depth);
			}
			a0 = regs->areg[0];
			a1 = regs->areg[1];
			continue;
		}

		psp = (unsigned long *)a1;

		a0 = *(psp - 4);
		a1 = *(psp - 3);

		if (a1 <= (unsigned long)psp)
			return;

	}
	return;
}

void xtensa_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	if (user_mode(regs))
		xtensa_backtrace_user(regs, depth);
	else
		xtensa_backtrace_kernel(regs, depth);
}
