/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_STACKTRACE_H
#define __ASM_STACKTRACE_H

#include <asm/ptrace.h>

struct stackframe {
	/*
	 * FP member should hold R7 when CONFIG_THUMB2_KERNEL is enabled
	 * and R11 otherwise.
	 */
	unsigned long fp;
	unsigned long sp;
	unsigned long lr;
	unsigned long pc;
};

static __always_inline
void arm_get_current_stackframe(struct pt_regs *regs, struct stackframe *frame)
{
		frame->fp = frame_pointer(regs);
		frame->sp = regs->ARM_sp;
		frame->lr = regs->ARM_lr;
		frame->pc = regs->ARM_pc;
}

extern int unwind_frame(struct stackframe *frame);
extern void walk_stackframe(struct stackframe *frame,
			    int (*fn)(struct stackframe *, void *), void *data);

#endif	/* __ASM_STACKTRACE_H */
