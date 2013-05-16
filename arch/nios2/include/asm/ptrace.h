/*
 * Copyright (C) 2013 Altera Corporation
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd
 *
 * based on m68k asm/processor.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_PTRACE_H
#define _ASM_NIOS2_PTRACE_H

#include <uapi/asm/ptrace.h>

#if defined __KERNEL__ && !defined CONFIG_MMU
# define PS_S		0x00000001
#endif

#ifndef __ASSEMBLY__

/* Arbitrarily choose the same ptrace numbers as used by the Sparc code. */
#define PTRACE_GETREGS		12
#define PTRACE_SETREGS		13

/*
 * Supervisor mode
 */

#ifdef CONFIG_MMU
# define user_mode(regs)	(((regs)->estatus & ESTATUS_EU))
#else
# define user_mode(regs)	(!((regs)->status_extension & PS_S))
#endif /* CONFIG_MMU */

#define instruction_pointer(regs)	((regs)->ra)
#define profile_pc(regs)		instruction_pointer(regs)
#define user_stack_pointer(regs)	((regs)->sp)
extern void show_regs(struct pt_regs *);

#define current_pt_regs() \
	((struct pt_regs *)((unsigned long)current_thread_info() + THREAD_SIZE)\
		- 1)
#endif /* __ASSEMBLY__ */
#endif /* _ASM_NIOS2_PTRACE_H */
