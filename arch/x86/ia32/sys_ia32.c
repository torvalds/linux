// SPDX-License-Identifier: GPL-2.0
/*
 * sys_ia32.c: Conversion between 32bit and 64bit native syscalls. Based on
 *             sys_sparc32
 *
 * Copyright (C) 2000		VA Linux Co
 * Copyright (C) 2000		Don Dugger <n0ano@valinux.com>
 * Copyright (C) 1999		Arun Sharma <arun.sharma@intel.com>
 * Copyright (C) 1997,1998	Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997		David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 2000		Hewlett-Packard Co.
 * Copyright (C) 2000		David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2000,2001,2002	Andi Kleen, SuSE Labs (x86-64 port)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment. In 2.5 most of this should be moved to a generic directory.
 *
 * This file assumes that there is a hole at the end of user address space.
 *
 * Some of the functions are LE specific currently. These are
 * hopefully all marked.  This should be fixed.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/signal.h>
#include <linux/syscalls.h>
#include <linux/times.h>
#include <linux/utsname.h>
#include <linux/mm.h>
#include <linux/uio.h>
#include <linux/poll.h>
#include <linux/personality.h>
#include <linux/stat.h>
#include <linux/rwsem.h>
#include <linux/compat.h>
#include <linux/vfs.h>
#include <linux/ptrace.h>
#include <linux/highuid.h>
#include <linux/sysctl.h>
#include <linux/slab.h>
#include <asm/mman.h>
#include <asm/types.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <asm/vgtod.h>
#include <asm/sys_ia32.h>

#define AA(__x)		((unsigned long)(__x))


COMPAT_SYSCALL_DEFINE3(x86_truncate64, const char __user *, filename,
		       unsigned long, offset_low, unsigned long, offset_high)
{
       return sys_truncate(filename, ((loff_t) offset_high << 32) | offset_low);
}

COMPAT_SYSCALL_DEFINE3(x86_ftruncate64, unsigned int, fd,
		       unsigned long, offset_low, unsigned long, offset_high)
{
       return sys_ftruncate(fd, ((loff_t) offset_high << 32) | offset_low);
}

/*
 * Another set for IA32/LFS -- x86_64 struct stat is different due to
 * support for 64bit inode numbers.
 */
static int cp_stat64(struct stat64 __user *ubuf, struct kstat *stat)
{
	typeof(ubuf->st_uid) uid = 0;
	typeof(ubuf->st_gid) gid = 0;
	SET_UID(uid, from_kuid_munged(current_user_ns(), stat->uid));
	SET_GID(gid, from_kgid_munged(current_user_ns(), stat->gid));
	if (!access_ok(VERIFY_WRITE, ubuf, sizeof(struct stat64)) ||
	    __put_user(huge_encode_dev(stat->dev), &ubuf->st_dev) ||
	    __put_user(stat->ino, &ubuf->__st_ino) ||
	    __put_user(stat->ino, &ubuf->st_ino) ||
	    __put_user(stat->mode, &ubuf->st_mode) ||
	    __put_user(stat->nlink, &ubuf->st_nlink) ||
	    __put_user(uid, &ubuf->st_uid) ||
	    __put_user(gid, &ubuf->st_gid) ||
	    __put_user(huge_encode_dev(stat->rdev), &ubuf->st_rdev) ||
	    __put_user(stat->size, &ubuf->st_size) ||
	    __put_user(stat->atime.tv_sec, &ubuf->st_atime) ||
	    __put_user(stat->atime.tv_nsec, &ubuf->st_atime_nsec) ||
	    __put_user(stat->mtime.tv_sec, &ubuf->st_mtime) ||
	    __put_user(stat->mtime.tv_nsec, &ubuf->st_mtime_nsec) ||
	    __put_user(stat->ctime.tv_sec, &ubuf->st_ctime) ||
	    __put_user(stat->ctime.tv_nsec, &ubuf->st_ctime_nsec) ||
	    __put_user(stat->blksize, &ubuf->st_blksize) ||
	    __put_user(stat->blocks, &ubuf->st_blocks))
		return -EFAULT;
	return 0;
}

COMPAT_SYSCALL_DEFINE2(x86_stat64, const char __user *, filename,
		       struct stat64 __user *, statbuf)
{
	struct kstat stat;
	int ret = vfs_stat(filename, &stat);

	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

COMPAT_SYSCALL_DEFINE2(x86_lstat64, const char __user *, filename,
		       struct stat64 __user *, statbuf)
{
	struct kstat stat;
	int ret = vfs_lstat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

COMPAT_SYSCALL_DEFINE2(x86_fstat64, unsigned int, fd,
		       struct stat64 __user *, statbuf)
{
	struct kstat stat;
	int ret = vfs_fstat(fd, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

COMPAT_SYSCALL_DEFINE4(x86_fstatat, unsigned int, dfd,
		       const char __user *, filename,
		       struct stat64 __user *, statbuf, int, flag)
{
	struct kstat stat;
	int error;

	error = vfs_fstatat(dfd, filename, &stat, flag);
	if (error)
		return error;
	return cp_stat64(statbuf, &stat);
}

/*
 * Linux/i386 didn't use to be able to handle more than
 * 4 system call parameters, so these system calls used a memory
 * block for parameter passing..
 */

struct mmap_arg_struct32 {
	unsigned int addr;
	unsigned int len;
	unsigned int prot;
	unsigned int flags;
	unsigned int fd;
	unsigned int offset;
};

COMPAT_SYSCALL_DEFINE1(x86_mmap, struct mmap_arg_struct32 __user *, arg)
{
	struct mmap_arg_struct32 a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;

	if (a.offset & ~PAGE_MASK)
		return -EINVAL;

	return sys_mmap_pgoff(a.addr, a.len, a.prot, a.flags, a.fd,
			       a.offset>>PAGE_SHIFT);
}

COMPAT_SYSCALL_DEFINE3(x86_waitpid, compat_pid_t, pid, unsigned int __user *,
		       stat_addr, int, options)
{
	return compat_sys_wait4(pid, stat_addr, options, NULL);
}

/* warning: next two assume little endian */
COMPAT_SYSCALL_DEFINE5(x86_pread, unsigned int, fd, char __user *, ubuf,
		       u32, count, u32, poslo, u32, poshi)
{
	return sys_pread64(fd, ubuf, count,
			 ((loff_t)AA(poshi) << 32) | AA(poslo));
}

COMPAT_SYSCALL_DEFINE5(x86_pwrite, unsigned int, fd, const char __user *, ubuf,
		       u32, count, u32, poslo, u32, poshi)
{
	return sys_pwrite64(fd, ubuf, count,
			  ((loff_t)AA(poshi) << 32) | AA(poslo));
}


/*
 * Some system calls that need sign extended arguments. This could be
 * done by a generic wrapper.
 */
COMPAT_SYSCALL_DEFINE6(x86_fadvise64_64, int, fd, __u32, offset_low,
		       __u32, offset_high, __u32, len_low, __u32, len_high,
		       int, advice)
{
	return sys_fadvise64_64(fd,
			       (((u64)offset_high)<<32) | offset_low,
			       (((u64)len_high)<<32) | len_low,
				advice);
}

COMPAT_SYSCALL_DEFINE4(x86_readahead, int, fd, unsigned int, off_lo,
		       unsigned int, off_hi, size_t, count)
{
	return sys_readahead(fd, ((u64)off_hi << 32) | off_lo, count);
}

COMPAT_SYSCALL_DEFINE6(x86_sync_file_range, int, fd, unsigned int, off_low,
		       unsigned int, off_hi, unsigned int, n_low,
		       unsigned int, n_hi, int, flags)
{
	return sys_sync_file_range(fd,
				   ((u64)off_hi << 32) | off_low,
				   ((u64)n_hi << 32) | n_low, flags);
}

COMPAT_SYSCALL_DEFINE5(x86_fadvise64, int, fd, unsigned int, offset_lo,
		       unsigned int, offset_hi, size_t, len, int, advice)
{
	return sys_fadvise64_64(fd, ((u64)offset_hi << 32) | offset_lo,
				len, advice);
}

COMPAT_SYSCALL_DEFINE6(x86_fallocate, int, fd, int, mode,
		       unsigned int, offset_lo, unsigned int, offset_hi,
		       unsigned int, len_lo, unsigned int, len_hi)
{
	return sys_fallocate(fd, mode, ((u64)offset_hi << 32) | offset_lo,
			     ((u64)len_hi << 32) | len_lo);
}

/*
 * The 32-bit clone ABI is CONFIG_CLONE_BACKWARDS
 */
COMPAT_SYSCALL_DEFINE5(x86_clone, unsigned long, clone_flags,
		       unsigned long, newsp, int __user *, parent_tidptr,
		       unsigned long, tls_val, int __user *, child_tidptr)
{
	return sys_clone(clone_flags, newsp, parent_tidptr, child_tidptr,
			tls_val);
}
