/* ptrace.h: ptrace() relevant definitions
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_PTRACE_H
#define _ASM_PTRACE_H

#include <asm/irq_regs.h>
#include <uapi/asm/ptrace.h>

#define in_syscall(regs) (((regs)->tbr & TBR_TT) == TBR_TT_TRAP0)
#ifndef __ASSEMBLY__

struct task_struct;

/*
 * we dedicate GR28 to keeping a pointer to the current exception frame
 * - gr28 is destroyed on entry to the kernel from userspace
 */
register struct pt_regs *__frame asm("gr28");

#define user_mode(regs)			(!((regs)->psr & PSR_S))
#define instruction_pointer(regs)	((regs)->pc)
#define user_stack_pointer(regs)	((regs)->sp)
#define current_pt_regs()		(__frame)

extern unsigned long user_stack(const struct pt_regs *);
#define profile_pc(regs) ((regs)->pc)

#define task_pt_regs(task) ((task)->thread.frame0)

#define arch_has_single_step()	(1)

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_PTRACE_H */
