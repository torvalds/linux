// SPDX-License-Identifier: GPL-2.0
/* System call table for x32 ABI. */

#include <linux/linkage.h>
#include <linux/sys.h>
#include <linux/cache.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/syscall.h>

#define __SYSCALL_64(nr, sym)

#define __SYSCALL_X32(nr, sym) extern asmlinkage long sym(const struct pt_regs *);
#include <asm/syscalls_64.h>
#undef __SYSCALL_X32

#define __SYSCALL_X32(nr, sym) [nr] = sym,

asmlinkage const sys_call_ptr_t x32_sys_call_table[__NR_x32_syscall_max+1] = {
	/*
	 * Smells like a compiler bug -- it doesn't work
	 * when the & below is removed.
	 */
	[0 ... __NR_x32_syscall_max] = &__x64_sys_ni_syscall,
#include <asm/syscalls_64.h>
};
