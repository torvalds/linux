/*
 * SH specific backtracing code for oprofile
 *
 * Copyright 2007 STMicroelectronics Ltd.
 *
 * Author: Dave Peverley <dpeverley@mpc-data.co.uk>
 *
 * Based on ARM oprofile backtrace code by Richard Purdie and in turn, i386
 * oprofile backtrace code by John Levon, David Smith
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/oprofile.h>
#include <linux/sched.h>
#include <linux/kallsyms.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>
#include <asm/sections.h>

/* Limit to stop backtracing too far. */
static int backtrace_limit = 20;

static unsigned long *
user_backtrace(unsigned long *stackaddr, struct pt_regs *regs)
{
	unsigned long buf_stack;

	/* Also check accessibility of address */
	if (!access_ok(VERIFY_READ, stackaddr, sizeof(unsigned long)))
		return NULL;

	if (__copy_from_user_inatomic(&buf_stack, stackaddr, sizeof(unsigned long)))
		return NULL;

	/* Quick paranoia check */
	if (buf_stack & 3)
		return NULL;

	oprofile_add_trace(buf_stack);

	stackaddr++;

	return stackaddr;
}

/*
 * |             | /\ Higher addresses
 * |             |
 * --------------- stack base (address of current_thread_info)
 * | thread info |
 * .             .
 * |    stack    |
 * --------------- saved regs->regs[15] value if valid
 * .             .
 * --------------- struct pt_regs stored on stack (struct pt_regs *)
 * |             |
 * .             .
 * |             |
 * --------------- ???
 * |             |
 * |             | \/ Lower addresses
 *
 * Thus, &pt_regs <-> stack base restricts the valid(ish) fp values
 */
static int valid_kernel_stack(unsigned long *stackaddr, struct pt_regs *regs)
{
	unsigned long stack = (unsigned long)regs;
	unsigned long stack_base = (stack & ~(THREAD_SIZE - 1)) + THREAD_SIZE;

	return ((unsigned long)stackaddr > stack) && ((unsigned long)stackaddr < stack_base);
}

static unsigned long *
kernel_backtrace(unsigned long *stackaddr, struct pt_regs *regs)
{
	unsigned long addr;

	/*
	 * If not a valid kernel address, keep going till we find one
	 * or the SP stops being a valid address.
	 */
	do {
		addr = *stackaddr++;
		oprofile_add_trace(addr);
	} while (valid_kernel_stack(stackaddr, regs));

	return stackaddr;
}

void sh_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	unsigned long *stackaddr;

	/*
	 * Paranoia - clip max depth as we could get lost in the weeds.
	 */
	if (depth > backtrace_limit)
		depth = backtrace_limit;

	stackaddr = (unsigned long *)regs->regs[15];
	if (!user_mode(regs)) {
		while (depth-- && valid_kernel_stack(stackaddr, regs))
			stackaddr = kernel_backtrace(stackaddr, regs);

		return;
	}

	while (depth-- && (stackaddr != NULL))
		stackaddr = user_backtrace(stackaddr, regs);
}
