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
#include <asm/types.h>
#include <linux/atomic.h>
#include <asm/syscalls_32.h>

#define AA(__x)		((unsigned long)(__x))

#if __BITS_PER_LONG == 32

asmlinkage long sys32_truncate64(const char __user *filename,
				 unsigned long offset_low,
				 unsigned long offset_high)
{
       return sys_truncate64(filename, ((loff_t) offset_high << 32) | offset_low);
}

asmlinkage long sys32_ftruncate64(unsigned int fd, unsigned long offset_low,
				  unsigned long offset_high)
{
       return sys_ftruncate64(fd, ((loff_t) offset_high << 32) | offset_low);
}

#ifdef CONFIG_MMU
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

asmlinkage long sys32_mmap(struct mmap_arg_struct32 __user *arg)
{
	struct mmap_arg_struct32 a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;

	if (a.offset & ~PAGE_MASK)
		return -EINVAL;

	return sys_mmap_pgoff(a.addr, a.len, a.prot, a.flags, a.fd,
			       a.offset>>PAGE_SHIFT);
}
#endif

asmlinkage long sys32_wait4(pid_t pid, unsigned int __user *stat_addr,
			    int options, struct rusage __user *ru)
{
	return sys_wait4(pid, stat_addr, options, ru);
}

asmlinkage long sys32_pread64(unsigned int fd, char __user *ubuf, u32 count,
			      u32 poslo, u32 poshi)
{
	return sys_pread64(fd, ubuf, count,
			 ((loff_t)AA(poshi) << 32) | AA(poslo));
}

asmlinkage long sys32_pwrite64(unsigned int fd, const char __user *ubuf,
			       u32 count, u32 poslo, u32 poshi)
{
	return sys_pwrite64(fd, ubuf, count,
			  ((loff_t)AA(poshi) << 32) | AA(poslo));
}


/*
 * Some system calls that need sign extended arguments. This could be
 * done by a generic wrapper.
 */
long sys32_fadvise64_64(int fd, __u32 offset_low, __u32 offset_high,
			__u32 len_low, __u32 len_high, int advice)
{
	return sys_fadvise64_64(fd,
			       (((u64)offset_high)<<32) | offset_low,
			       (((u64)len_high)<<32) | len_low,
				advice);
}

asmlinkage ssize_t sys32_readahead(int fd, unsigned off_lo, unsigned off_hi,
				   size_t count)
{
	return sys_readahead(fd, ((u64)off_hi << 32) | off_lo, count);
}

asmlinkage long sys32_sync_file_range(int fd, unsigned off_low, unsigned off_hi,
				      unsigned n_low, unsigned n_hi, unsigned int flags)
{
	return sys_sync_file_range(fd,
				   ((u64)off_hi << 32) | off_low,
				   ((u64)n_hi << 32) | n_low, flags);
}

asmlinkage long sys32_sync_file_range2(int fd, unsigned int flags,
				       unsigned off_low, unsigned off_hi,
				       unsigned n_low, unsigned n_hi)
{
	return sys_sync_file_range(fd,
				   ((u64)off_hi << 32) | off_low,
				   ((u64)n_hi << 32) | n_low, flags);
}

asmlinkage long sys32_fallocate(int fd, int mode, unsigned offset_lo,
				unsigned offset_hi, unsigned len_lo,
				unsigned len_hi)
{
	return sys_fallocate(fd, mode, ((u64)offset_hi << 32) | offset_lo,
			     ((u64)len_hi << 32) | len_lo);
}

#endif
