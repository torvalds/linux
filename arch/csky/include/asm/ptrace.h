/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_PTRACE_H
#define __ASM_CSKY_PTRACE_H

#include <uapi/asm/ptrace.h>
#include <asm/traps.h>
#include <linux/types.h>

#ifndef __ASSEMBLY__

#define PS_S	0x80000000 /* Supervisor Mode */

#define arch_has_single_step() (1)
#define current_pt_regs() \
({ (struct pt_regs *)((char *)current_thread_info() + THREAD_SIZE) - 1; })

#define user_stack_pointer(regs) ((regs)->usp)

#define user_mode(regs) (!((regs)->sr & PS_S))
#define instruction_pointer(regs) ((regs)->pc)
#define profile_pc(regs) instruction_pointer(regs)

static inline bool in_syscall(struct pt_regs const *regs)
{
	return ((regs->sr >> 16) & 0xff) == VEC_TRAP0;
}

static inline void forget_syscall(struct pt_regs *regs)
{
	regs->sr &= ~(0xff << 16);
}

static inline unsigned long regs_return_value(struct pt_regs *regs)
{
	return regs->a0;
}

#endif /* __ASSEMBLY__ */
#endif /* __ASM_CSKY_PTRACE_H */
