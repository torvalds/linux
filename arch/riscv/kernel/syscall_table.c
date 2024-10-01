// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2009 Arnd Bergmann <arnd@arndb.de>
 * Copyright (C) 2012 Regents of the University of California
 */

#include <linux/linkage.h>
#include <linux/syscalls.h>
#include <asm-generic/syscalls.h>
#include <asm/syscall.h>

#define __SYSCALL_WITH_COMPAT(nr, native, compat) __SYSCALL(nr, native)

#undef __SYSCALL
#define __SYSCALL(nr, call)	asmlinkage long __riscv_##call(const struct pt_regs *);
#include <asm/syscall_table.h>

#undef __SYSCALL
#define __SYSCALL(nr, call)	[nr] = __riscv_##call,

void * const sys_call_table[__NR_syscalls] = {
	[0 ... __NR_syscalls - 1] = __riscv_sys_ni_syscall,
#include <asm/syscall_table.h>
};
