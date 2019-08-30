/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_UNICORE_SYSCALL_H
#define _ASM_UNICORE_SYSCALL_H

#include <uapi/linux/audit.h>

static inline int syscall_get_arch(struct task_struct *task)
{
	return AUDIT_ARCH_UNICORE;
}

#endif	/* _ASM_UNICORE_SYSCALL_H */
