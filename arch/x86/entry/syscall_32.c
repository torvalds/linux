// SPDX-License-Identifier: GPL-2.0
/* System call table for i386. */

#include <linux/linkage.h>
#include <linux/sys.h>
#include <linux/cache.h>
#include <linux/syscalls.h>
#include <asm/syscall.h>

#ifdef CONFIG_IA32_EMULATION
#define __SYSCALL_WITH_COMPAT(nr, native, compat)	__SYSCALL(nr, compat)
#else
#define __SYSCALL_WITH_COMPAT(nr, native, compat)	__SYSCALL(nr, native)
#endif

#define __SYSCALL(nr, sym) extern long __ia32_##sym(const struct pt_regs *);

#include <asm/syscalls_32.h>
#undef __SYSCALL

/*
 * The sys_call_table[] is no longer used for system calls, but
 * kernel/trace/trace_syscalls.c still wants to know the system
 * call address.
 */
#ifdef CONFIG_X86_32
#define __SYSCALL(nr, sym) __ia32_##sym,
const sys_call_ptr_t sys_call_table[] = {
#include <asm/syscalls_32.h>
};
#undef __SYSCALL
#endif

#define __SYSCALL(nr, sym) case nr: return __ia32_##sym(regs);

long ia32_sys_call(const struct pt_regs *regs, unsigned int nr)
{
	switch (nr) {
	#include <asm/syscalls_32.h>
	default: return __ia32_sys_ni_syscall(regs);
	}
};
