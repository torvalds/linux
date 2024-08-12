// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/syscalls.h>
#include <asm/syscalls.h>

#undef __SYSCALL
#define __SYSCALL(nr, call)[nr] = (call),
#define __SYSCALL_WITH_COMPAT(nr, native, compat) __SYSCALL(nr, native)

#define sys_fadvise64_64 sys_csky_fadvise64_64
#define sys_sync_file_range sys_sync_file_range2
void * const sys_call_table[__NR_syscalls] __page_aligned_data = {
	[0 ... __NR_syscalls - 1] = sys_ni_syscall,
#include <asm/syscall_table_32.h>
};
