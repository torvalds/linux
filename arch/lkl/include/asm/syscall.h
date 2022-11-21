/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_SYSCALL_H
#define _ASM_LKL_SYSCALL_H

static inline int
syscall_get_arch(struct task_struct *task)
{
	return 0;
}

#include <asm-generic/syscall.h>

#endif /* _ASM_LKL_SYSCALL_H */
