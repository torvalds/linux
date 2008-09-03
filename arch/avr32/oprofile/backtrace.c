/*
 * AVR32 specific backtracing code for oprofile
 *
 * Copyright 2008 Weinmann GmbH
 *
 * Author: Nikolaus Voss <n.voss@weinmann.de>
 *
 * Based on i386 oprofile backtrace code by John Levon and David Smith
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/oprofile.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

/* The first two words of each frame on the stack look like this if we have
 * frame pointers */
struct frame_head {
	unsigned long lr;
	struct frame_head *fp;
};

/* copied from arch/avr32/kernel/process.c */
static inline int valid_stack_ptr(struct thread_info *tinfo, unsigned long p)
{
	return (p > (unsigned long)tinfo)
		&& (p < (unsigned long)tinfo + THREAD_SIZE - 3);
}

/* copied from arch/x86/oprofile/backtrace.c */
static struct frame_head *dump_user_backtrace(struct frame_head *head)
{
	struct frame_head bufhead[2];

	/* Also check accessibility of one struct frame_head beyond */
	if (!access_ok(VERIFY_READ, head, sizeof(bufhead)))
		return NULL;
	if (__copy_from_user_inatomic(bufhead, head, sizeof(bufhead)))
		return NULL;

	oprofile_add_trace(bufhead[0].lr);

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (bufhead[0].fp <= head)
		return NULL;

	return bufhead[0].fp;
}

void avr32_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	/* Get first frame pointer */
	struct frame_head *head = (struct frame_head *)(regs->r7);

	if (!user_mode(regs)) {
#ifdef CONFIG_FRAME_POINTER
		/*
		 * Traverse the kernel stack from frame to frame up to
		 * "depth" steps.
		 */
		while (depth-- && valid_stack_ptr(task_thread_info(current),
						  (unsigned long)head)) {
			oprofile_add_trace(head->lr);
			if (head->fp <= head)
				break;
			head = head->fp;
		}
#endif
	} else {
		/* Assume we have frame pointers in user mode process */
		while (depth-- && head)
			head = dump_user_backtrace(head);
	}
}


