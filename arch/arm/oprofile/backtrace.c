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
	struct frame_tail buftail;

	/* hardware pte might not be valid due to dirty/accessed bit emulation
	 * so we use copy_from_user and benefit from exception fixups */
	if (copy_from_user(&buftail, tail, sizeof(struct frame_tail)))
		return NULL;

	oprofile_add_trace(buftail.lr);

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (tail >= buftail.fp)
		return NULL;

	return buftail.fp-1;
}

/* Compare two addresses and see if they're on the same page */
#define CMP_ADDR_EQUAL(x,y,offset) ((((unsigned long) x) >> PAGE_SHIFT) \
	== ((((unsigned long) y) + offset) >> PAGE_SHIFT))

/* check that the page(s) containing the frame tail are present */
static int pages_present(struct frame_tail *tail)
{
	struct mm_struct * mm = current->mm;

	if (!check_user_page_readable(mm, (unsigned long)tail))
		return 0;

	if (CMP_ADDR_EQUAL(tail, tail, 8))
		return 1;

	if (!check_user_page_readable(mm, ((unsigned long)tail) + 8))
		return 0;

	return 1;
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
	unsigned long last_address = 0;

	tail = ((struct frame_tail *) regs->ARM_fp) - 1;

	if (!user_mode(regs)) {

#ifdef CONFIG_FRAME_POINTER
		while (depth-- && tail && valid_kernel_stack(tail, regs)) {
			tail = kernel_backtrace(tail);
		}
#endif
		return;
	}

	while (depth-- && tail && !((unsigned long) tail & 3)) {
		if ((!CMP_ADDR_EQUAL(last_address, tail, 0)
			|| !CMP_ADDR_EQUAL(last_address, tail, 8))
				&& !pages_present(tail))
			return;
		last_address = (unsigned long) tail;
		tail = user_backtrace(tail);
	}
}

