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
#include <asm/uaccess.h>
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

#ifdef CONFIG_SYSVIPC
long compat_sys_ipc(u32 call, u32 first, u32 second, u32 third, compat_uptr_t ptr,
	       u32 fifth)
{
	int version;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {

	case SEMTIMEDOP:
		if (fifth)
			/* sign extend semid */
			return compat_sys_semtimedop((int)first,
						     compat_ptr(ptr), second,
						     compat_ptr(fifth));
		/* else fall through for normal semop() */
	case SEMOP:
		/* struct sembuf is the same on 32 and 64bit :)) */
		/* sign extend semid */
		return sys_semtimedop((int)first, compat_ptr(ptr), second,
				      NULL);
	case SEMGET:
		/* sign extend key, nsems */
		return sys_semget((int)first, (int)second, third);
	case SEMCTL:
		/* sign extend semid, semnum */
		return compat_sys_semctl((int)first, (int)second, third,
					 compat_ptr(ptr));

	case MSGSND:
		/* sign extend msqid */
		return compat_sys_msgsnd((int)first, (int)second, third,
					 compat_ptr(ptr));
	case MSGRCV:
		/* sign extend msqid, msgtyp */
		return compat_sys_msgrcv((int)first, second, (int)fifth,
					 third, version, compat_ptr(ptr));
	case MSGGET:
		/* sign extend key */
		return sys_msgget((int)first, second);
	case MSGCTL:
		/* sign extend msqid */
		return compat_sys_msgctl((int)first, second, compat_ptr(ptr));

	case SHMAT:
		/* sign extend shmid */
		return compat_sys_shmat((int)first, second, third, version,
					compat_ptr(ptr));
	case SHMDT:
		return sys_shmdt(compat_ptr(ptr));
	case SHMGET:
		/* sign extend key_t */
		return sys_shmget((int)first, second, third);
	case SHMCTL:
		/* sign extend shmid */
		return compat_sys_shmctl((int)first, second, compat_ptr(ptr));

	default:
		return -ENOSYS;
	}

	return -ENOSYS;
}
#endif

/* Note: it is necessary to treat out_fd and in_fd as unsigned ints, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long compat_sys_sendfile_wrapper(u32 out_fd, u32 in_fd,
					    compat_off_t __user *offset, u32 count)
{
	return compat_sys_sendfile((int)out_fd, (int)in_fd, offset, count);
}

asmlinkage long compat_sys_sendfile64_wrapper(u32 out_fd, u32 in_fd,
					      compat_loff_t __user *offset, u32 count)
{
	return sys_sendfile((int)out_fd, (int)in_fd,
			    (off_t __user *)offset, count);
}

off_t ppc32_lseek(unsigned int fd, u32 offset, unsigned int origin)
{
	/* sign extend n */
	return sys_lseek(fd, (int)offset, origin);
}

long compat_sys_truncate(const char __user * path, u32 length)
{
	/* sign extend length */
	return sys_truncate(path, (int)length);
}

long compat_sys_ftruncate(int fd, u32 length)
{
	/* sign extend length */
	return sys_ftruncate(fd, (int)length);
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
	return sys_pread64(fd, ubuf, count, ((loff_t)poshi << 32) | poslo);
}

compat_ssize_t compat_sys_pwrite64(unsigned int fd, const char __user *ubuf, compat_size_t count,
			      u32 reg6, u32 poshi, u32 poslo)
{
	return sys_pwrite64(fd, ubuf, count, ((loff_t)poshi << 32) | poslo);
}

compat_ssize_t compat_sys_readahead(int fd, u32 r4, u32 offhi, u32 offlo, u32 count)
{
	return sys_readahead(fd, ((loff_t)offhi << 32) | offlo, count);
}

asmlinkage int compat_sys_truncate64(const char __user * path, u32 reg4,
				unsigned long high, unsigned long low)
{
	return sys_truncate(path, (high << 32) | low);
}

asmlinkage long compat_sys_fallocate(int fd, int mode, u32 offhi, u32 offlo,
				     u32 lenhi, u32 lenlo)
{
	return sys_fallocate(fd, mode, ((loff_t)offhi << 32) | offlo,
			     ((loff_t)lenhi << 32) | lenlo);
}

asmlinkage int compat_sys_ftruncate64(unsigned int fd, u32 reg4, unsigned long high,
				 unsigned long low)
{
	return sys_ftruncate(fd, (high << 32) | low);
}

long ppc32_lookup_dcookie(u32 cookie_high, u32 cookie_low, char __user *buf,
			  size_t len)
{
	return sys_lookup_dcookie((u64)cookie_high << 32 | cookie_low,
				  buf, len);
}

long ppc32_fadvise64(int fd, u32 unused, u32 offset_high, u32 offset_low,
		     size_t len, int advice)
{
	return sys_fadvise64(fd, (u64)offset_high << 32 | offset_low, len,
			     advice);
}

asmlinkage long compat_sys_add_key(const char __user *_type,
			      const char __user *_description,
			      const void __user *_payload,
			      u32 plen,
			      u32 ringid)
{
	return sys_add_key(_type, _description, _payload, plen, ringid);
}

asmlinkage long compat_sys_request_key(const char __user *_type,
				  const char __user *_description,
				  const char __user *_callout_info,
				  u32 destringid)
{
	return sys_request_key(_type, _description, _callout_info, destringid);
}

asmlinkage long compat_sys_sync_file_range2(int fd, unsigned int flags,
				   unsigned offset_hi, unsigned offset_lo,
				   unsigned nbytes_hi, unsigned nbytes_lo)
{
	loff_t offset = ((loff_t)offset_hi << 32) | offset_lo;
	loff_t nbytes = ((loff_t)nbytes_hi << 32) | nbytes_lo;

	return sys_sync_file_range(fd, offset, nbytes, flags);
}

asmlinkage long compat_sys_fanotify_mark(int fanotify_fd, unsigned int flags,
					 unsigned mask_hi, unsigned mask_lo,
					 int dfd, const char __user *pathname)
{
	u64 mask = ((u64)mask_hi << 32) | mask_lo;
	return sys_fanotify_mark(fanotify_fd, flags, mask, dfd, pathname);
}
