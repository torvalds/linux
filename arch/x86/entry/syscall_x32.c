// SPDX-License-Identifier: GPL-2.0
/* System call table for x32 ABI. */

#include <linux/linkage.h>
#include <linux/sys.h>
#include <linux/cache.h>
#include <linux/syscalls.h>
#include <asm/unistd.h>
#include <asm/syscall.h>

/*
 * Reuse the 64-bit entry points for the x32 versions that occupy different
 * slots in the syscall table.
 */
#define __x32_sys_readv		__x64_sys_readv
#define __x32_sys_writev	__x64_sys_writev
#define __x32_sys_getsockopt	__x64_sys_getsockopt
#define __x32_sys_setsockopt	__x64_sys_setsockopt

#define __SYSCALL_64(nr, sym)

#define __SYSCALL_X32(nr, sym) extern long __x32_##sym(const struct pt_regs *);
#define __SYSCALL_COMMON(nr, sym) extern long __x64_##sym(const struct pt_regs *);
#include <asm/syscalls_64.h>
#undef __SYSCALL_X32
#undef __SYSCALL_COMMON

#define __SYSCALL_X32(nr, sym) [nr] = __x32_##sym,
#define __SYSCALL_COMMON(nr, sym) [nr] = __x64_##sym,

asmlinkage const sys_call_ptr_t x32_sys_call_table[__NR_x32_syscall_max+1] = {
	/*
	 * Smells like a compiler bug -- it doesn't work
	 * when the & below is removed.
	 */
	[0 ... __NR_x32_syscall_max] = &__x64_sys_ni_syscall,
#include <asm/syscalls_64.h>
};
