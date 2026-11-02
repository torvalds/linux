/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions used by low-level trap handlers
 *
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2007 John Williams <john.williams@petalogix.com>
 */

#ifndef _ASM_MICROBLAZE_ENTRY_H
#define _ASM_MICROBLAZE_ENTRY_H

#include <asm/percpu.h>
#include <asm/ptrace.h>
#include <linux/linkage.h>

/*
 * These are per-cpu variables required in entry.S, among other
 * places
 */

#define PER_CPU(var) var

/*
 * The MicroBlaze ABI has the caller reserve an argument home area:
 * REG_PARM_STACK_SPACE is 24 and OUTGOING_REG_PARM_STACK_SPACE is 1, so a
 * callee may write to [caller_sp + 4, caller_sp + 28).  The kernel calls C
 * with r1 at the frame base, so reserve that area below pt_regs and reach
 * the saved registers through PTO.  This restores what 6e83557c38b4
 * ("microblaze: Remove r0_ram pointer and PTO alignment") removed; the old
 * value of 24 was one word short and left the last argument slot
 * overlapping pt_regs' r0.
 */
#define PTO			28
#define STATE_SAVE_SIZE		(PT_SIZE + PTO)

# ifndef __ASSEMBLER__
DECLARE_PER_CPU(unsigned int, KSP); /* Saved kernel stack pointer */
DECLARE_PER_CPU(unsigned int, KM); /* Kernel/user mode */
DECLARE_PER_CPU(unsigned int, ENTRY_SP); /* Saved SP on kernel entry */
DECLARE_PER_CPU(unsigned int, R11_SAVE); /* Temp variable for entry */
DECLARE_PER_CPU(unsigned int, CURRENT_SAVE); /* Saved current pointer */

extern asmlinkage void do_notify_resume(struct pt_regs *regs, int in_syscall);
# endif /* __ASSEMBLER__ */

#endif /* _ASM_MICROBLAZE_ENTRY_H */
