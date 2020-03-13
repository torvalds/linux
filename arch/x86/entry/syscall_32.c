// SPDX-License-Identifier: GPL-2.0
/* System call table for i386. */

#include <linux/linkage.h>
#include <linux/sys.h>
#include <linux/cache.h>
#include <asm/unistd.h>
#include <asm/syscall.h>

#ifdef CONFIG_IA32_EMULATION
/* On X86_64, we use struct pt_regs * to pass parameters to syscalls */
#define __SYSCALL_I386(nr, sym) extern asmlinkage long __ia32_##sym(const struct pt_regs *);
#define __sys_ni_syscall __ia32_sys_ni_syscall
#else /* CONFIG_IA32_EMULATION */
#define __SYSCALL_I386(nr, sym) extern asmlinkage long sym(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long);
extern asmlinkage long sys_ni_syscall(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long);
#define __sys_ni_syscall sys_ni_syscall
#endif /* CONFIG_IA32_EMULATION */

#include <asm/syscalls_32.h>
#undef __SYSCALL_I386

#ifdef CONFIG_IA32_EMULATION
#define __SYSCALL_I386(nr, sym) [nr] = __ia32_##sym,
#else /* CONFIG_IA32_EMULATION */
#define __SYSCALL_I386(nr, sym) [nr] = sym,
#endif /* CONFIG_IA32_EMULATION */

__visible const sys_call_ptr_t ia32_sys_call_table[__NR_ia32_syscall_max+1] = {
	/*
	 * Smells like a compiler bug -- it doesn't work
	 * when the & below is removed.
	 */
	[0 ... __NR_ia32_syscall_max] = &__sys_ni_syscall,
#include <asm/syscalls_32.h>
};
