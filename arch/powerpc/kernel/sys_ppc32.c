/*
 * sys_ppc32.c: Conversion between 32bit and 64bit native syscalls.
 *
 * Copyright (C) 2001 IBM
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
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
#include <linux/poll.h>
#include <linux/personality.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/in.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/sysctl.h>
#include <linux/binfmts.h>
#include <linux/security.h>
#include <linux/compat.h>
#include <linux/ptrace.h>
#include <linux/elf.h>
#include <linux/ipc.h>
#include <linux/slab.h>

#include <asm/ptrace.h>
#include <asm/types.h>
#include <linux/uaccess.h>
#include <asm/unistd.h>
#include <asm/time.h>
#include <asm/mmu_context.h>
#include <asm/ppc-pci.h>
#include <asm/syscalls.h>
#include <asm/switch_to.h>


asmlinkage long ppc32_select(u32 n, compat_ulong_t __user *inp,
		compat_ulong_t __user *outp, compat_ulong_t __user *exp,
		compat_uptr_t tvp_x)
{
	/* sign extend n */
	return compat_sys_select((int)n, inp, outp, exp, compat_ptr(tvp_x));
}

unsigned long compat_sys_mmap2(unsigned long addr, size_t len,
			  unsigned long prot, unsigned long flags,
			  unsigned long fd, unsigned long pgoff)
{
	/* This should remain 12 even if PAGE_SIZE changes */
	return sys_mmap(addr, len, prot, flags, fd, pgoff << 12);
}

/* 
 * long long munging:
 * The 32 bit ABI passes long longs in an odd even register pair.
 */

compat_ssize_t compat_sys_pread64(unsigned int fd, char __user *ubuf, compat_size_t count,
			     u32 reg6, u32 poshi, u32 poslo)
{
	return ksys_pread64(fd, ubuf, count, ((loff_t)poshi << 32) | poslo);
}

compat_ssize_t compat_sys_pwrite64(unsigned int fd, const char __user *ubuf, compat_size_t count,
			      u32 reg6, u32 poshi, u32 poslo)
{
	return ksys_pwrite64(fd, ubuf, count, ((loff_t)poshi << 32) | poslo);
}

compat_ssize_t compat_sys_readahead(int fd, u32 r4, u32 offhi, u32 offlo, u32 count)
{
	return ksys_readahead(fd, ((loff_t)offhi << 32) | offlo, count);
}

asmlinkage int compat_sys_truncate64(const char __user * path, u32 reg4,
				unsigned long high, unsigned long low)
{
	return ksys_truncate(path, (high << 32) | low);
}

asmlinkage long compat_sys_fallocate(int fd, int mode, u32 offhi, u32 offlo,
				     u32 lenhi, u32 lenlo)
{
	return ksys_fallocate(fd, mode, ((loff_t)offhi << 32) | offlo,
			     ((loff_t)lenhi << 32) | lenlo);
}

asmlinkage int compat_sys_ftruncate64(unsigned int fd, u32 reg4, unsigned long high,
				 unsigned long low)
{
	return ksys_ftruncate(fd, (high << 32) | low);
}

long ppc32_fadvise64(int fd, u32 unused, u32 offset_high, u32 offset_low,
		     size_t len, int advice)
{
	return ksys_fadvise64_64(fd, (u64)offset_high << 32 | offset_low, len,
				 advice);
}

asmlinkage long compat_sys_sync_file_range2(int fd, unsigned int flags,
				   unsigned offset_hi, unsigned offset_lo,
				   unsigned nbytes_hi, unsigned nbytes_lo)
{
	loff_t offset = ((loff_t)offset_hi << 32) | offset_lo;
	loff_t nbytes = ((loff_t)nbytes_hi << 32) | nbytes_lo;

	return ksys_sync_file_range(fd, offset, nbytes, flags);
}
