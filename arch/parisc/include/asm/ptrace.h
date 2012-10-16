/* written by Philipp Rumpf, Copyright (C) 1999 SuSE GmbH Nuernberg
** Copyright (C) 2000 Grant Grundler, Hewlett-Packard
*/
#ifndef _PARISC_PTRACE_H
#define _PARISC_PTRACE_H

#include <uapi/asm/ptrace.h>


#define task_regs(task) ((struct pt_regs *) ((char *)(task) + TASK_REGS))

#define arch_has_single_step()	1
#define arch_has_block_step()	1

/* XXX should we use iaoq[1] or iaoq[0] ? */
#define user_mode(regs)			(((regs)->iaoq[0] & 3) ? 1 : 0)
#define user_space(regs)		(((regs)->iasq[1] != 0) ? 1 : 0)
#define instruction_pointer(regs)	((regs)->iaoq[0] & ~3)
#define user_stack_pointer(regs)	((regs)->gr[30])
unsigned long profile_pc(struct pt_regs *);


#endif
