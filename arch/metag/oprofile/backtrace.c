/*
 * Copyright (C) 2010-2013 Imagination Technologies Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/oprofile.h>
#include <linux/uaccess.h>
#include <asm/processor.h>
#include <asm/stacktrace.h>

#include "backtrace.h"

static void user_backtrace_fp(unsigned long __user *fp, unsigned int depth)
{
	while (depth-- && access_ok(VERIFY_READ, fp, 8)) {
		unsigned long addr;
		unsigned long __user *fpnew;
		if (__copy_from_user_inatomic(&addr, fp + 1, sizeof(addr)))
			break;
		addr -= 4;

		oprofile_add_trace(addr);

		/* stack grows up, so frame pointers must decrease */
		if (__copy_from_user_inatomic(&fpnew, fp + 0, sizeof(fpnew)))
			break;
		if (fpnew >= fp)
			break;
		fp = fpnew;
	}
}

static int kernel_backtrace_frame(struct stackframe *frame, void *data)
{
	unsigned int *depth = data;

	oprofile_add_trace(frame->pc);

	/* decrement depth and stop if we reach 0 */
	if ((*depth)-- == 0)
		return 1;

	/* otherwise onto the next frame */
	return 0;
}

void metag_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	if (user_mode(regs)) {
		unsigned long *fp = (unsigned long *)regs->ctx.AX[1].U0;
		user_backtrace_fp((unsigned long __user __force *)fp, depth);
	} else {
		struct stackframe frame;
		frame.fp = regs->ctx.AX[1].U0;		/* A0FrP */
		frame.sp = user_stack_pointer(regs);	/* A0StP */
		frame.lr = 0;				/* from stack */
		frame.pc = regs->ctx.CurrPC;		/* PC */
		walk_stackframe(&frame, &kernel_backtrace_frame, &depth);
	}
}
