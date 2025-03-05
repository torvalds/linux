// SPDX-License-Identifier: GPL-2.0-only
/*
 * System call table for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/unistd.h>

#include <asm/syscall.h>

#undef __SYSCALL
#define __SYSCALL(nr, call) [nr] = (call),

SYSCALL_DEFINE6(hexagon_fadvise64_64, int, fd, int, advice,
		SC_ARG64(offset), SC_ARG64(len))
{
	return ksys_fadvise64_64(fd, SC_VAL64(loff_t, offset), SC_VAL64(loff_t, len), advice);
}
#define sys_fadvise64_64 sys_hexagon_fadvise64_64

void *sys_call_table[__NR_syscalls] = {
#include <asm/unistd.h>
};
