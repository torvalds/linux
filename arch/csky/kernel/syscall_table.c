// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/syscalls.h>
#include <asm/syscalls.h>

#undef __SYSCALL
#define __SYSCALL(nr, call)[nr] = (call),

#define sys_fadvise64_64 sys_csky_fadvise64_64
void * const sys_call_table[__NR_syscalls] __page_aligned_data = {
	[0 ... __NR_syscalls - 1] = sys_ni_syscall,
#include <asm/unistd.h>
};
