// SPDX-License-Identifier: GPL-2.0

#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/unistd.h>

#include <asm/syscalls.h>

#define sys_clone	sys_clone_wrapper
#define sys_clone3	sys_clone3_wrapper
#define sys_mmap2	sys_mmap_pgoff

#define __SYSCALL(nr, call) [nr] = (call),
#define __SYSCALL_WITH_COMPAT(nr, native, compat)  __SYSCALL(nr, native)

void *sys_call_table[NR_syscalls] = {
	[0 ... NR_syscalls-1] = sys_ni_syscall,
#include <asm/syscall_table_32.h>
};
