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

#define __SYSCALL(nr, sym) __ia32_##sym,

__visible const sys_call_ptr_t ia32_sys_call_table[] = {
#include <asm/syscalls_32.h>
};
