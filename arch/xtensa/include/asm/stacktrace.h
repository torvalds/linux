/*
 * arch/xtensa/include/asm/stacktrace.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2013 Tensilica Inc.
 */
#ifndef _XTENSA_STACKTRACE_H
#define _XTENSA_STACKTRACE_H

#include <linux/sched.h>

struct stackframe {
	unsigned long pc;
	unsigned long sp;
};

static __always_inline unsigned long *stack_pointer(struct task_struct *task)
{
	unsigned long *sp;

	if (!task || task == current)
		__asm__ __volatile__ ("mov %0, a1\n" : "=a"(sp));
	else
		sp = (unsigned long *)task->thread.sp;

	return sp;
}

void walk_stackframe(unsigned long *sp,
		int (*fn)(struct stackframe *frame, void *data),
		void *data);

#endif /* _XTENSA_STACKTRACE_H */
