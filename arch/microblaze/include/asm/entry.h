/*
 * Definitions used by low-level trap handlers
 *
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2007 John Williams <john.williams@petalogix.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#ifndef _ASM_MICROBLAZE_ENTRY_H
#define _ASM_MICROBLAZE_ENTRY_H

#include <asm/percpu.h>
#include <asm/ptrace.h>

/*
 * These are per-cpu variables required in entry.S, among other
 * places
 */

#define PER_CPU(var) var

# ifndef __ASSEMBLY__
DECLARE_PER_CPU(unsigned int, KSP); /* Saved kernel stack pointer */
DECLARE_PER_CPU(unsigned int, KM); /* Kernel/user mode */
DECLARE_PER_CPU(unsigned int, ENTRY_SP); /* Saved SP on kernel entry */
DECLARE_PER_CPU(unsigned int, R11_SAVE); /* Temp variable for entry */
DECLARE_PER_CPU(unsigned int, CURRENT_SAVE); /* Saved current pointer */
# endif /* __ASSEMBLY__ */

#ifndef CONFIG_MMU

/* noMMU hasn't any space for args */
# define STATE_SAVE_ARG_SPACE	(0)

#else /* CONFIG_MMU */

/* If true, system calls save and restore all registers (except result
 * registers, of course).  If false, then `call clobbered' registers
 * will not be preserved, on the theory that system calls are basically
 * function calls anyway, and the caller should be able to deal with it.
 * This is a security risk, of course, as `internal' values may leak out
 * after a system call, but that certainly doesn't matter very much for
 * a processor with no MMU protection!  For a protected-mode kernel, it
 * would be faster to just zero those registers before returning.
 *
 * I can not rely on the glibc implementation. If you turn it off make
 * sure that r11/r12 is saved in user-space. --KAA
 *
 * These are special variables using by the kernel trap/interrupt code
 * to save registers in, at a time when there are no spare registers we
 * can use to do so, and we can't depend on the value of the stack
 * pointer.  This means that they must be within a signed 16-bit
 * displacement of 0x00000000.
 */

/* A `state save frame' is a struct pt_regs preceded by some extra space
 * suitable for a function call stack frame. */

/* Amount of room on the stack reserved for arguments and to satisfy the
 * C calling conventions, in addition to the space used by the struct
 * pt_regs that actually holds saved values. */
#define STATE_SAVE_ARG_SPACE	(6*4) /* Up to six arguments */

#endif /* CONFIG_MMU */

#endif /* _ASM_MICROBLAZE_ENTRY_H */
