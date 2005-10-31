/*
 * Arm specific backtracing code for oprofile
 *
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Richard Purdie <rpurdie@openedhand.com>
 *
 * Based on i386 oprofile backtrace code by John Levon, David Smith
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/oprofile.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>


/*
 * The registers we're interested in are at the end of the variable
 * length saved register structure. The fp points at the end of this
 * structure so the address of this struct is:
 * (struct frame_tail *)(xxx->fp)-1
 */
struct frame_tail {
	struct frame_tail *fp;
	unsigned long sp;
	unsigned long lr;
} __attribute__((packed));


#ifdef CONFIG_FRAME_POINTER
static struct frame_tail* kernel_backtrace(struct frame_tail *tail)
{
	oprofile_add_trace(tail->lr);

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (tail >= tail->fp)
		return NULL;

	return tail->fp-1;
}
#endif

static struct frame_tail* user_backtrace(struct frame_tail *tail)
{
	struct frame_tail buftail[2];

	/* Also check accessibility of one struct frame_tail beyond */
	if (!access_ok(VERIFY_READ, tail, sizeof(buftail)))
		return NULL;
	if (__copy_from_user_inatomic(buftail, tail, sizeof(buftail)))
		return NULL;

	oprofile_add_trace(buftail[0].lr);

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (tail >= buftail[0].fp)
		return NULL;

	return buftail[0].fp-1;
}

/*
 * |             | /\ Higher addresses
 * |             |
 * --------------- stack base (address of current_thread_info)
 * | thread info |
 * .             .
 * |    stack    |
 * --------------- saved regs->ARM_fp value if valid (frame_tail address)
 * .             .
 * --------------- struct pt_regs stored on stack (struct pt_regs *)
 * |             |
 * .             .
 * |             |
 * --------------- %esp
 * |             |
 * |             | \/ Lower addresses
 *
 * Thus, &pt_regs <-> stack base restricts the valid(ish) fp values
 */
static int valid_kernel_stack(struct frame_tail *tail, struct pt_regs *regs)
{
	unsigned long tailaddr = (unsigned long)tail;
	unsigned long stack = (unsigned long)regs;
	unsigned long stack_base = (stack & ~(THREAD_SIZE - 1)) + THREAD_SIZE;

	return (tailaddr > stack) && (tailaddr < stack_base);
}

void arm_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	struct frame_tail *tail;

	tail = ((struct frame_tail *) regs->ARM_fp) - 1;

	if (!user_mode(regs)) {

#ifdef CONFIG_FRAME_POINTER
		while (depth-- && tail && valid_kernel_stack(tail, regs)) {
			tail = kernel_backtrace(tail);
		}
#endif
		return;
	}

	while (depth-- && tail && !((unsigned long) tail & 3))
		tail = user_backtrace(tail);
}
