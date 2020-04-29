// SPDX-License-Identifier: GPL-2.0
/* System call table for i386. */

#include <linux/linkage.h>
#include <linux/sys.h>
#include <linux/cache.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/syscall.h>

#define __SYSCALL_I386(nr, sym) extern long __ia32_##sym(const struct pt_regs *);

#include <asm/syscalls_32.h>
#undef __SYSCALL_I386

#define __SYSCALL_I386(nr, sym) [nr] = __ia32_##sym,

__visible const sys_call_ptr_t ia32_sys_call_table[__NR_ia32_syscall_max+1] = {
	/*
	 * Smells like a compiler bug -- it doesn't work
	 * when the & below is removed.
	 */
	[0 ... __NR_ia32_syscall_max] = &__ia32_sys_ni_syscall,
#include <asm/syscalls_32.h>
};
