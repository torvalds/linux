/*
 * sys_parisc32.c: Conversion between 32bit and 64bit native syscalls.
 *
 * Copyright (C) 2000-2001 Hewlett Packard Company
 * Copyright (C) 2000 John Marvin
 * Copyright (C) 2001 Matthew Wilcox
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment. Based heavily on sys_ia32.c and sys_sparc32.c.
 */

#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h> 
#include <linux/mm.h> 
#include <linux/file.h> 
#include <linux/signal.h>
#include <linux/resource.h>
#include <linux/times.h>
#include <linux/time.h>
#include <linux/smp.h>
#include <linux/sem.h>
#include <linux/shm.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/ncp_fs.h>
#include <linux/poll.h>
#include <linux/personality.h>
#include <linux/stat.h>
#include <linux/highmem.h>
#include <linux/highuid.h>
#include <linux/mman.h>
#include <linux/binfmts.h>
#include <linux/namei.h>
#include <linux/vfs.h>
#include <linux/ptrace.h>
#include <linux/swap.h>
#include <linux/syscalls.h>

#include <asm/types.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>

#include "sys32.h"

#undef DEBUG

#ifdef DEBUG
#define DBG(x)	printk x
#else
#define DBG(x)
#endif

asmlinkage long sys32_unimplemented(int r26, int r25, int r24, int r23,
	int r22, int r21, int r20)
{
    printk(KERN_ERR "%s(%d): Unimplemented 32 on 64 syscall #%d!\n", 
    	current->comm, current->pid, r20);
    return -ENOSYS;
}

/* Note: it is necessary to treat out_fd and in_fd as unsigned ints, with the
 * corresponding cast to a signed int to insure that the proper conversion
 * (sign extension) between the register representation of a signed int (msr in
 * 32-bit mode) and the register representation of a signed int (msr in 64-bit
 * mode) is performed.
 */
asmlinkage long sys32_sendfile(u32 out_fd, u32 in_fd,
			       compat_off_t __user *offset, compat_size_t count)
{
	return compat_sys_sendfile((int)out_fd, (int)in_fd, offset, count);
}

asmlinkage long sys32_sendfile64(u32 out_fd, u32 in_fd,
				 compat_loff_t __user *offset, compat_size_t count)
{
	return sys_sendfile64((int)out_fd, (int)in_fd,
				(loff_t __user *)offset, count);
}

asmlinkage long sys32_semctl(int semid, int semnum, int cmd, union semun arg)
{
        union semun u;
	
        if (cmd == SETVAL) {
                /* Ugh.  arg is a union of int,ptr,ptr,ptr, so is 8 bytes.
                 * The int should be in the first 4, but our argument
                 * frobbing has left it in the last 4.
                 */
                u.val = *((int *)&arg + 1);
                return sys_semctl (semid, semnum, cmd, u);
	}
	return sys_semctl (semid, semnum, cmd, arg);
}

long sys32_lookup_dcookie(u32 cookie_high, u32 cookie_low, char __user *buf,
			  size_t len)
{
	return sys_lookup_dcookie((u64)cookie_high << 32 | cookie_low,
				  buf, len);
}

asmlinkage long compat_sys_fanotify_mark(int fan_fd, int flags, u32 mask_hi,
					 u32 mask_lo, int fd,
					 const char __user *pathname)
{
	return sys_fanotify_mark(fan_fd, flags, ((u64)mask_hi << 32) | mask_lo,
				 fd, pathname);
}
