// SPDX-License-Identifier: GPL-2.0
/*
 * System call table for UML/x86-64, copied from arch/x86/kernel/syscall_*.c
 * with some changes for UML.
 */

#include <linux/linkage.h>
#include <linux/sys.h>
#include <linux/cache.h>
#include <asm/syscall.h>

/*
 * Below you can see, in terms of #define's, the differences between the x86-64
 * and the UML syscall table.
 */

/* Not going to be implemented by UML, since we have no hardware. */
#define sys_iopl sys_ni_syscall
#define sys_ioperm sys_ni_syscall

#define __SYSCALL(nr, sym) extern asmlinkage long sym(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long);
#include <asm/syscalls_64.h>

#undef __SYSCALL
#define __SYSCALL(nr, sym) sym,

extern asmlinkage long sys_ni_syscall(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long);

const sys_call_ptr_t sys_call_table[] ____cacheline_aligned = {
#include <asm/syscalls_64.h>
};

int syscall_table_size = sizeof(sys_call_table);
