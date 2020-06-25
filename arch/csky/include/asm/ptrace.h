/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_PTRACE_H
#define __ASM_CSKY_PTRACE_H

#include <uapi/asm/ptrace.h>
#include <asm/traps.h>
#include <linux/types.h>
#include <linux/compiler.h>

#ifndef __ASSEMBLY__

#define PS_S	0x80000000 /* Supervisor Mode */

#define USR_BKPT	0x1464

#define arch_has_single_step() (1)
#define current_pt_regs() \
({ (struct pt_regs *)((char *)current_thread_info() + THREAD_SIZE) - 1; })

#define user_stack_pointer(regs) ((regs)->usp)

#define user_mode(regs) (!((regs)->sr & PS_S))
#define instruction_pointer(regs) ((regs)->pc)
#define profile_pc(regs) instruction_pointer(regs)

static inline void instruction_pointer_set(struct pt_regs *regs,
					   unsigned long val)
{
	regs->pc = val;
}

#if defined(__CSKYABIV2__)
#define MAX_REG_OFFSET offsetof(struct pt_regs, dcsr)
#else
#define MAX_REG_OFFSET offsetof(struct pt_regs, regs[9])
#endif

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

/* Valid only for Kernel mode traps. */
static inline unsigned long kernel_stack_pointer(struct pt_regs *regs)
{
	return regs->usp;
}

static inline unsigned long frame_pointer(struct pt_regs *regs)
{
	return regs->regs[4];
}
static inline void frame_pointer_set(struct pt_regs *regs,
				     unsigned long val)
{
	regs->regs[4] = val;
}

extern int regs_query_register_offset(const char *name);
extern unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs,
						unsigned int n);

/*
 * regs_get_register() - get register value from its offset
 * @regs:      pt_regs from which register value is gotten
 * @offset:    offset of the register.
 *
 * regs_get_register returns the value of a register whose offset from @regs.
 * The @offset is the offset of the register in struct pt_regs.
 * If @offset is bigger than MAX_REG_OFFSET, this returns 0.
 */
static inline unsigned long regs_get_register(struct pt_regs *regs,
						unsigned int offset)
{
	if (unlikely(offset > MAX_REG_OFFSET))
		return 0;

	return *(unsigned long *)((unsigned long)regs + offset);
}

#endif /* __ASSEMBLY__ */
#endif /* __ASM_CSKY_PTRACE_H */
