// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/unistd.h>
#include <asm/syscalls.h>

#undef __SYSCALL
#define __SYSCALL(nr, call) [nr] = (call),

#define sys_rt_sigreturn sys_rt_sigreturn_wrapper
#define sys_fadvise64_64 sys_fadvise64_64_wrapper
void *sys_call_table[__NR_syscalls] __aligned(8192) = {
	[0 ... __NR_syscalls - 1] = sys_ni_syscall,
#include <asm/unistd.h>
};
