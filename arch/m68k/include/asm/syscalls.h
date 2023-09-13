/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_M68K_SYSCALLS_H
#define _ASM_M68K_SYSCALLS_H

#include <linux/compiler_types.h>
#include <linux/linkage.h>

asmlinkage int sys_cacheflush(unsigned long addr, int scope, int cache,
			      unsigned long len);
asmlinkage int sys_atomic_cmpxchg_32(unsigned long newval, int oldval, int d3,
				     int d4, int d5, unsigned long __user *mem);
asmlinkage int sys_getpagesize(void);
asmlinkage unsigned long sys_get_thread_area(void);
asmlinkage int sys_set_thread_area(unsigned long tp);
asmlinkage int sys_atomic_barrier(void);

#include <asm-generic/syscalls.h>

#endif	/* _ASM_M68K_SYSCALLS_H */
