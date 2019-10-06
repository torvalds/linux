// SPDX-License-Identifier: GPL-2.0
/*
 *  S390 version
 *    Copyright IBM Corp. 2000
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Gerhard Tonn (ton@de.ibm.com)   
 *               Thomas Spatzier (tspat@de.ibm.com)
 *
 *  Conversion between 31bit and 64bit native syscalls.
 *
 * Heavily inspired by the 32-bit Sparc compat code which is 
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 *
 */


#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h> 
#include <linux/mm.h> 
#include <linux/file.h> 
#include <linux/signal.h>
#include <linux/resource.h>
#include <linux/times.h>
#include <linux/smp.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/uio.h>
#include <linux/quota.h>
#include <linux/poll.h>
#include <linux/personality.h>
#include <linux/stat.h>
#include <linux/filter.h>
#include <linux/highmem.h>
#include <linux/mman.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/icmpv6.h>
#include <linux/syscalls.h>
#include <linux/sysctl.h>
#include <linux/binfmts.h>
#include <linux/capability.h>
#include <linux/compat.h>
#include <linux/vfs.h>
#include <linux/ptrace.h>
#include <linux/fadvise.h>
#include <linux/ipc.h>
#include <linux/slab.h>

#include <asm/types.h>
#include <linux/uaccess.h>

#include <net/scm.h>
#include <net/sock.h>

#include "compat_linux.h"

#ifdef CONFIG_SYSVIPC
COMPAT_SYSCALL_DEFINE5(s390_ipc, uint, call, int, first, compat_ulong_t, second,
		compat_ulong_t, third, compat_uptr_t, ptr)
{
	if (call >> 16)		/* hack for backward compatibility */
		return -EINVAL;
	return compat_ksys_ipc(call, first, second, third, ptr, third);
}
#endif

COMPAT_SYSCALL_DEFINE3(s390_truncate64, const char __user *, path, u32, high, u32, low)
{
	return ksys_truncate(path, (unsigned long)high << 32 | low);
}

COMPAT_SYSCALL_DEFINE3(s390_ftruncate64, unsigned int, fd, u32, high, u32, low)
{
	return ksys_ftruncate(fd, (unsigned long)high << 32 | low);
}

COMPAT_SYSCALL_DEFINE5(s390_pread64, unsigned int, fd, char __user *, ubuf,
		       compat_size_t, count, u32, high, u32, low)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL;
	return ksys_pread64(fd, ubuf, count, (unsigned long)high << 32 | low);
}

COMPAT_SYSCALL_DEFINE5(s390_pwrite64, unsigned int, fd, const char __user *, ubuf,
		       compat_size_t, count, u32, high, u32, low)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL;
	return ksys_pwrite64(fd, ubuf, count, (unsigned long)high << 32 | low);
}

COMPAT_SYSCALL_DEFINE4(s390_readahead, int, fd, u32, high, u32, low, s32, count)
{
	return ksys_readahead(fd, (unsigned long)high << 32 | low, count);
}

struct stat64_emu31 {
	unsigned long long  st_dev;
	unsigned int    __pad1;
#define STAT64_HAS_BROKEN_ST_INO        1
	u32             __st_ino;
	unsigned int    st_mode;
	unsigned int    st_nlink;
	u32             st_uid;
	u32             st_gid;
	unsigned long long  st_rdev;
	unsigned int    __pad3;
	long            st_size;
	u32             st_blksize;
	unsigned char   __pad4[4];
	u32             __pad5;     /* future possible st_blocks high bits */
	u32             st_blocks;  /* Number 512-byte blocks allocated. */
	u32             st_atime;
	u32             __pad6;
	u32             st_mtime;
	u32             __pad7;
	u32             st_ctime;
	u32             __pad8;     /* will be high 32 bits of ctime someday */
	unsigned long   st_ino;
};	

static int cp_stat64(struct stat64_emu31 __user *ubuf, struct kstat *stat)
{
	struct stat64_emu31 tmp;

	memset(&tmp, 0, sizeof(tmp));

	tmp.st_dev = huge_encode_dev(stat->dev);
	tmp.st_ino = stat->ino;
	tmp.__st_ino = (u32)stat->ino;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = (unsigned int)stat->nlink;
	tmp.st_uid = from_kuid_munged(current_user_ns(), stat->uid);
	tmp.st_gid = from_kgid_munged(current_user_ns(), stat->gid);
	tmp.st_rdev = huge_encode_dev(stat->rdev);
	tmp.st_size = stat->size;
	tmp.st_blksize = (u32)stat->blksize;
	tmp.st_blocks = (u32)stat->blocks;
	tmp.st_atime = (u32)stat->atime.tv_sec;
	tmp.st_mtime = (u32)stat->mtime.tv_sec;
	tmp.st_ctime = (u32)stat->ctime.tv_sec;

	return copy_to_user(ubuf,&tmp,sizeof(tmp)) ? -EFAULT : 0; 
}

COMPAT_SYSCALL_DEFINE2(s390_stat64, const char __user *, filename, struct stat64_emu31 __user *, statbuf)
{
	struct kstat stat;
	int ret = vfs_stat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

COMPAT_SYSCALL_DEFINE2(s390_lstat64, const char __user *, filename, struct stat64_emu31 __user *, statbuf)
{
	struct kstat stat;
	int ret = vfs_lstat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

COMPAT_SYSCALL_DEFINE2(s390_fstat64, unsigned int, fd, struct stat64_emu31 __user *, statbuf)
{
	struct kstat stat;
	int ret = vfs_fstat(fd, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

COMPAT_SYSCALL_DEFINE4(s390_fstatat64, unsigned int, dfd, const char __user *, filename,
		       struct stat64_emu31 __user *, statbuf, int, flag)
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

struct mmap_arg_struct_emu31 {
	compat_ulong_t addr;
	compat_ulong_t len;
	compat_ulong_t prot;
	compat_ulong_t flags;
	compat_ulong_t fd;
	compat_ulong_t offset;
};

COMPAT_SYSCALL_DEFINE1(s390_old_mmap, struct mmap_arg_struct_emu31 __user *, arg)
{
	struct mmap_arg_struct_emu31 a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	if (a.offset & ~PAGE_MASK)
		return -EINVAL;
	return ksys_mmap_pgoff(a.addr, a.len, a.prot, a.flags, a.fd,
			       a.offset >> PAGE_SHIFT);
}

COMPAT_SYSCALL_DEFINE1(s390_mmap2, struct mmap_arg_struct_emu31 __user *, arg)
{
	struct mmap_arg_struct_emu31 a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	return ksys_mmap_pgoff(a.addr, a.len, a.prot, a.flags, a.fd, a.offset);
}

COMPAT_SYSCALL_DEFINE3(s390_read, unsigned int, fd, char __user *, buf, compat_size_t, count)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL; 

	return ksys_read(fd, buf, count);
}

COMPAT_SYSCALL_DEFINE3(s390_write, unsigned int, fd, const char __user *, buf, compat_size_t, count)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL; 

	return ksys_write(fd, buf, count);
}

/*
 * 31 bit emulation wrapper functions for sys_fadvise64/fadvise64_64.
 * These need to rewrite the advise values for POSIX_FADV_{DONTNEED,NOREUSE}
 * because the 31 bit values differ from the 64 bit values.
 */

COMPAT_SYSCALL_DEFINE5(s390_fadvise64, int, fd, u32, high, u32, low, compat_size_t, len, int, advise)
{
	if (advise == 4)
		advise = POSIX_FADV_DONTNEED;
	else if (advise == 5)
		advise = POSIX_FADV_NOREUSE;
	return ksys_fadvise64_64(fd, (unsigned long)high << 32 | low, len,
				 advise);
}

struct fadvise64_64_args {
	int fd;
	long long offset;
	long long len;
	int advice;
};

COMPAT_SYSCALL_DEFINE1(s390_fadvise64_64, struct fadvise64_64_args __user *, args)
{
	struct fadvise64_64_args a;

	if ( copy_from_user(&a, args, sizeof(a)) )
		return -EFAULT;
	if (a.advice == 4)
		a.advice = POSIX_FADV_DONTNEED;
	else if (a.advice == 5)
		a.advice = POSIX_FADV_NOREUSE;
	return ksys_fadvise64_64(a.fd, a.offset, a.len, a.advice);
}

COMPAT_SYSCALL_DEFINE6(s390_sync_file_range, int, fd, u32, offhigh, u32, offlow,
		       u32, nhigh, u32, nlow, unsigned int, flags)
{
	return ksys_sync_file_range(fd, ((loff_t)offhigh << 32) + offlow,
				   ((u64)nhigh << 32) + nlow, flags);
}

COMPAT_SYSCALL_DEFINE6(s390_fallocate, int, fd, int, mode, u32, offhigh, u32, offlow,
		       u32, lenhigh, u32, lenlow)
{
	return ksys_fallocate(fd, mode, ((loff_t)offhigh << 32) + offlow,
			      ((u64)lenhigh << 32) + lenlow);
}
