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

#define __SYSCALL(nr, call) [nr] = (call),
#define __SYSCALL_WITH_COMPAT(nr, native, compat)        __SYSCALL(nr, native)

#define sys_mmap2 sys_mmap_pgoff

SYSCALL_DEFINE6(hexagon_fadvise64_64, int, fd, int, advice,
		SC_ARG64(offset), SC_ARG64(len))
{
	return ksys_fadvise64_64(fd, SC_VAL64(loff_t, offset), SC_VAL64(loff_t, len), advice);
}
#define sys_fadvise64_64 sys_hexagon_fadvise64_64

#define sys_sync_file_range sys_sync_file_range2

void *sys_call_table[__NR_syscalls] = {
#include <asm/syscall_table_32.h>
};
