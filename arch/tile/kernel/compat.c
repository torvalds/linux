/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/* Adjust unistd.h to provide 32-bit numbers and functions. */
#define __SYSCALL_COMPAT

#include <linux/compat.h>
#include <linux/syscalls.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/uaccess.h>
#include <linux/signal.h>
#include <asm/syscalls.h>

/*
 * Syscalls that take 64-bit numbers traditionally take them in 32-bit
 * "high" and "low" value parts on 32-bit architectures.
 * In principle, one could imagine passing some register arguments as
 * fully 64-bit on TILE-Gx in 32-bit mode, but it seems easier to
 * adapt the usual convention.
 */

long compat_sys_truncate64(char __user *filename, u32 dummy, u32 low, u32 high)
{
	return sys_truncate(filename, ((loff_t)high << 32) | low);
}

long compat_sys_ftruncate64(unsigned int fd, u32 dummy, u32 low, u32 high)
{
	return sys_ftruncate(fd, ((loff_t)high << 32) | low);
}

long compat_sys_pread64(unsigned int fd, char __user *ubuf, size_t count,
			u32 dummy, u32 low, u32 high)
{
	return sys_pread64(fd, ubuf, count, ((loff_t)high << 32) | low);
}

long compat_sys_pwrite64(unsigned int fd, char __user *ubuf, size_t count,
			 u32 dummy, u32 low, u32 high)
{
	return sys_pwrite64(fd, ubuf, count, ((loff_t)high << 32) | low);
}

long compat_sys_lookup_dcookie(u32 low, u32 high, char __user *buf, size_t len)
{
	return sys_lookup_dcookie(((loff_t)high << 32) | low, buf, len);
}

long compat_sys_sync_file_range2(int fd, unsigned int flags,
				 u32 offset_lo, u32 offset_hi,
				 u32 nbytes_lo, u32 nbytes_hi)
{
	return sys_sync_file_range(fd, ((loff_t)offset_hi << 32) | offset_lo,
				   ((loff_t)nbytes_hi << 32) | nbytes_lo,
				   flags);
}

long compat_sys_fallocate(int fd, int mode,
			  u32 offset_lo, u32 offset_hi,
			  u32 len_lo, u32 len_hi)
{
	return sys_fallocate(fd, mode, ((loff_t)offset_hi << 32) | offset_lo,
			     ((loff_t)len_hi << 32) | len_lo);
}



long compat_sys_sched_rr_get_interval(compat_pid_t pid,
				      struct compat_timespec __user *interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_sched_rr_get_interval(pid,
					(struct timespec __force __user *)&t);
	set_fs(old_fs);
	if (put_compat_timespec(&t, interval))
		return -EFAULT;
	return ret;
}

/* Provide the compat syscall number to call mapping. */
#undef __SYSCALL
#define __SYSCALL(nr, call) [nr] = (call),

/* See comments in sys.c */
#define compat_sys_fadvise64_64 sys32_fadvise64_64
#define compat_sys_readahead sys32_readahead

/* Call the assembly trampolines where necessary. */
#define compat_sys_rt_sigreturn _compat_sys_rt_sigreturn
#undef sys_clone
#define sys_clone _sys_clone

/*
 * Note that we can't include <linux/unistd.h> here since the header
 * guard will defeat us; <asm/unistd.h> checks for __SYSCALL as well.
 */
void *compat_sys_call_table[__NR_syscalls] = {
	[0 ... __NR_syscalls-1] = sys_ni_syscall,
#include <asm/unistd.h>
};
