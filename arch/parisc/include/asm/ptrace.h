/* SPDX-License-Identifier: GPL-2.0 */
/* written by Philipp Rumpf, Copyright (C) 1999 SuSE GmbH Nuernberg
** Copyright (C) 2000 Grant Grundler, Hewlett-Packard
*/
#ifndef _PARISC_PTRACE_H
#define _PARISC_PTRACE_H

#include <asm/assembly.h>
#include <uapi/asm/ptrace.h>

#define task_regs(task) ((struct pt_regs *) ((char *)(task) + TASK_REGS))

#define arch_has_single_step()	1
#define arch_has_block_step()	1

/* XXX should we use iaoq[1] or iaoq[0] ? */
#define user_mode(regs)			(((regs)->iaoq[0] & 3) != PRIV_KERNEL)
#define user_space(regs)		((regs)->iasq[1] != PRIV_KERNEL)
#define instruction_pointer(regs)	((regs)->iaoq[0] & ~3)
#define user_stack_pointer(regs)	((regs)->gr[30])
unsigned long profile_pc(struct pt_regs *);

static inline unsigned long regs_return_value(struct pt_regs *regs)
{
	return regs->gr[28];
}

static inline void instruction_pointer_set(struct pt_regs *regs,
						unsigned long val)
{
	regs->iaoq[0] = val;
	regs->iaoq[1] = val + 4;
}

/* Query offset/name of register from its name/offset */
extern int regs_query_register_offset(const char *name);
extern const char *regs_query_register_name(unsigned int offset);
#define MAX_REG_OFFSET (offsetof(struct pt_regs, ipsw))

#define kernel_stack_pointer(regs) ((regs)->gr[30])

static inline unsigned long regs_get_register(struct pt_regs *regs,
					      unsigned int offset)
{
	if (unlikely(offset > MAX_REG_OFFSET))
		return 0;
	return *(unsigned long *)((unsigned long)regs + offset);
}

unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs, unsigned int n);
int regs_within_kernel_stack(struct pt_regs *regs, unsigned long addr);

#endif
