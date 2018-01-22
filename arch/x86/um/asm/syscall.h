/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_ASM_SYSCALL_H
#define __UM_ASM_SYSCALL_H

#include <asm/syscall-generic.h>
#include <uapi/linux/audit.h>

typedef asmlinkage long (*sys_call_ptr_t)(unsigned long, unsigned long,
					  unsigned long, unsigned long,
					  unsigned long, unsigned long);

static inline int syscall_get_arch(void)
{
#ifdef CONFIG_X86_32
	return AUDIT_ARCH_I386;
#else
	return AUDIT_ARCH_X86_64;
#endif
}

#endif /* __UM_ASM_SYSCALL_H */
