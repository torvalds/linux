// SPDX-License-Identifier: GPL-2.0
/*
 * Kernel exception handling table support.  Derived from arch/alpha/mm/extable.c.
 *
 * Copyright (C) 1998, 1999, 2001-2002, 2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <asm/ptrace.h>
#include <asm/extable.h>
#include <asm/errno.h>
#include <asm/processor.h>

void
ia64_handle_exception (struct pt_regs *regs, const struct exception_table_entry *e)
{
	long fix = (u64) &e->fixup + e->fixup;

	regs->r8 = -EFAULT;
	if (fix & 4)
		regs->r9 = 0;
	regs->cr_iip = fix & ~0xf;
	ia64_psr(regs)->ri = fix & 0x3;		/* set continuation slot number */
}
