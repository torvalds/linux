#ifndef __UM_ASM_SYSCALL_H
#define __UM_ASM_SYSCALL_H

#include <uapi/linux/audit.h>

static inline int syscall_get_arch(void)
{
#ifdef CONFIG_X86_32
	return AUDIT_ARCH_I386;
#else
	return AUDIT_ARCH_X86_64;
#endif
}

#endif /* __UM_ASM_SYSCALL_H */
