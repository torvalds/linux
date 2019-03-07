/*
 *  Implementation of various system calls for Linux/PowerPC
 *
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Derived from "arch/i386/kernel/sys_i386.c"
 * Adapted from the i386 version by Gary Thomas
 * Modified by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras (paulus@cs.anu.edu.au).
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/PPC
 * platform.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/sys.h>
#include <linux/ipc.h>
#include <linux/utsname.h>
#include <linux/file.h>
#include <linux/personality.h>

#include <linux/uaccess.h>
#include <asm/syscalls.h>
#include <asm/time.h>
#include <asm/unistd.h>
#include <asm/asm-prototypes.h>

static inline long do_mmap2(unsigned long addr, size_t len,
			unsigned long prot, unsigned long flags,
			unsigned long fd, unsigned long off, int shift)
{
	long ret = -EINVAL;

	if (!arch_validate_prot(prot, addr))
		goto out;

	if (shift) {
		if (off & ((1 << shift) - 1))
			goto out;
		off >>= shift;
	}

	ret = ksys_mmap_pgoff(addr, len, prot, flags, fd, off);
out:
	return ret;
}

SYSCALL_DEFINE6(mmap2, unsigned long, addr, size_t, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, pgoff)
{
	return do_mmap2(addr, len, prot, flags, fd, pgoff, PAGE_SHIFT-12);
}

SYSCALL_DEFINE6(mmap, unsigned long, addr, size_t, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, off_t, offset)
{
	return do_mmap2(addr, len, prot, flags, fd, offset, PAGE_SHIFT);
}

#ifdef CONFIG_PPC32
/*
 * Due to some executables calling the wrong select we sometimes
 * get wrong args.  This determines how the args are being passed
 * (a single ptr to them all args passed) then calls
 * sys_select() with the appropriate args. -- Cort
 */
int
ppc_select(int n, fd_set __user *inp, fd_set __user *outp, fd_set __user *exp, struct timeval __user *tvp)
{
	if ( (unsigned long)n >= 4096 )
	{
		unsigned long __user *buffer = (unsigned long __user *)n;
		if (!access_ok(buffer, 5*sizeof(unsigned long))
		    || __get_user(n, buffer)
		    || __get_user(inp, ((fd_set __user * __user *)(buffer+1)))
		    || __get_user(outp, ((fd_set  __user * __user *)(buffer+2)))
		    || __get_user(exp, ((fd_set  __user * __user *)(buffer+3)))
		    || __get_user(tvp, ((struct timeval  __user * __user *)(buffer+4))))
			return -EFAULT;
	}
	return sys_select(n, inp, outp, exp, tvp);
}
#endif

#ifdef CONFIG_PPC64
long ppc64_personality(unsigned long personality)
{
	long ret;

	if (personality(current->personality) == PER_LINUX32
	    && personality(personality) == PER_LINUX)
		personality = (personality & ~PER_MASK) | PER_LINUX32;
	ret = sys_personality(personality);
	if (personality(ret) == PER_LINUX32)
		ret = (ret & ~PER_MASK) | PER_LINUX;
	return ret;
}
#endif

long ppc_fadvise64_64(int fd, int advice, u32 offset_high, u32 offset_low,
		      u32 len_high, u32 len_low)
{
	return ksys_fadvise64_64(fd, (u64)offset_high << 32 | offset_low,
				 (u64)len_high << 32 | len_low, advice);
}

SYSCALL_DEFINE0(switch_endian)
{
	struct thread_info *ti;

	current->thread.regs->msr ^= MSR_LE;

	/*
	 * Set TIF_RESTOREALL so that r3 isn't clobbered on return to
	 * userspace. That also has the effect of restoring the non-volatile
	 * GPRs, so we saved them on the way in here.
	 */
	ti = current_thread_info();
	ti->flags |= _TIF_RESTOREALL;

	return 0;
}
