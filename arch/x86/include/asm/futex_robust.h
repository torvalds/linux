/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_FUTEX_ROBUST_H
#define _ASM_X86_FUTEX_ROBUST_H

#include <asm/ptrace.h>

static __always_inline void __user *x86_futex_robust_unlock_get_pop(struct pt_regs *regs)
{
	/*
	 * If ZF is set then the cmpxchg succeeded and the pending op pointer
	 * needs to be cleared.
	 */
	return regs->flags & X86_EFLAGS_ZF ? (void __user *)regs->dx : NULL;
}

#define arch_futex_robust_unlock_get_pop(regs)	\
	x86_futex_robust_unlock_get_pop(regs)

#endif /* _ASM_X86_FUTEX_ROBUST_H */
