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
#include <linux/utsname.h>
#include <linux/time.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/nfs_fs.h>
#include <linux/ncp_fs.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfsd/xdr.h>
#include <linux/nfsd/syscall.h>
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

/*
 * sys32_execve() executes a new program.
 */

asmlinkage int sys32_execve(struct pt_regs *regs)
{
	int error;
	char *filename;

	DBG(("sys32_execve(%p) r26 = 0x%lx\n", regs, regs->gr[26]));
	filename = getname((const char __user *) regs->gr[26]);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = compat_do_execve(filename, compat_ptr(regs->gr[25]),
				 compat_ptr(regs->gr[24]), regs);
	if (error == 0) {
		task_lock(current);
		current->ptrace &= ~PT_DTRACE;
		task_unlock(current);
	}
	putname(filename);
out:

	return error;
}

asmlinkage long sys32_unimplemented(int r26, int r25, int r24, int r23,
	int r22, int r21, int r20)
{
    printk(KERN_ERR "%s(%d): Unimplemented 32 on 64 syscall #%d!\n", 
    	current->comm, current->pid, r20);
    return -ENOSYS;
}

#ifdef CONFIG_SYSCTL

struct __sysctl_args32 {
	u32 name;
	int nlen;
	u32 oldval;
	u32 oldlenp;
	u32 newval;
	u32 newlen;
	u32 __unused[4];
};

asmlinkage long sys32_sysctl(struct __sysctl_args32 __user *args)
{
#ifndef CONFIG_SYSCTL_SYSCALL
	return -ENOSYS;
#else
	struct __sysctl_args32 tmp;
	int error;
	unsigned int oldlen32;
	size_t oldlen, __user *oldlenp = NULL;
	unsigned long addr = (((long __force)&args->__unused[0]) + 7) & ~7;

	DBG(("sysctl32(%p)\n", args));

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	if (tmp.oldval && tmp.oldlenp) {
		/* Duh, this is ugly and might not work if sysctl_args
		   is in read-only memory, but do_sysctl does indirectly
		   a lot of uaccess in both directions and we'd have to
		   basically copy the whole sysctl.c here, and
		   glibc's __sysctl uses rw memory for the structure
		   anyway.  */
		/* a possibly better hack than this, which will avoid the
		 * problem if the struct is read only, is to push the
		 * 'oldlen' value out to the user's stack instead. -PB
		 */
		if (get_user(oldlen32, (u32 *)(u64)tmp.oldlenp))
			return -EFAULT;
		oldlen = oldlen32;
		if (put_user(oldlen, (size_t *)addr))
			return -EFAULT;
		oldlenp = (size_t *)addr;
	}

	lock_kernel();
	error = do_sysctl((int __user *)(u64)tmp.name, tmp.nlen,
			  (void __user *)(u64)tmp.oldval, oldlenp,
			  (void __user *)(u64)tmp.newval, tmp.newlen);
	unlock_kernel();
	if (oldlenp) {
		if (!error) {
			if (get_user(oldlen, (size_t *)addr)) {
				error = -EFAULT;
			} else {
				oldlen32 = oldlen;
				if (put_user(oldlen32, (u32 *)(u64)tmp.oldlenp))
					error = -EFAULT;
			}
		}
		if (copy_to_user(args->__unused, tmp.__unused, sizeof(tmp.__unused)))
			error = -EFAULT;
	}
	return error;
#endif
}

#endif /* CONFIG_SYSCTL */

asmlinkage long sys32_sched_rr_get_interval(pid_t pid,
	struct compat_timespec __user *interval)
{
	struct timespec t;
	int ret;

	KERNEL_SYSCALL(ret, sys_sched_rr_get_interval, pid, (struct timespec __user *)&t);
	if (put_compat_timespec(&t, interval))
		return -EFAULT;
	return ret;
}

/*** copied from mips64 ***/
/*
 * Ooo, nasty.  We need here to frob 32-bit unsigned longs to
 * 64-bit unsigned longs.
 */

static inline int
get_fd_set32(unsigned long n, u32 *ufdset, unsigned long *fdset)
{
	n = (n + 8*sizeof(u32) - 1) / (8*sizeof(u32));
	if (ufdset) {
		unsigned long odd;

		if (!access_ok(VERIFY_WRITE, ufdset, n*sizeof(u32)))
			return -EFAULT;

		odd = n & 1UL;
		n &= ~1UL;
		while (n) {
			unsigned long h, l;
			__get_user(l, ufdset);
			__get_user(h, ufdset+1);
			ufdset += 2;
			*fdset++ = h << 32 | l;
			n -= 2;
		}
		if (odd)
			__get_user(*fdset, ufdset);
	} else {
		/* Tricky, must clear full unsigned long in the
		 * kernel fdset at the end, this makes sure that
		 * actually happens.
		 */
		memset(fdset, 0, ((n + 1) & ~1)*sizeof(u32));
	}
	return 0;
}

static inline void
set_fd_set32(unsigned long n, u32 *ufdset, unsigned long *fdset)
{
	unsigned long odd;
	n = (n + 8*sizeof(u32) - 1) / (8*sizeof(u32));

	if (!ufdset)
		return;

	odd = n & 1UL;
	n &= ~1UL;
	while (n) {
		unsigned long h, l;
		l = *fdset++;
		h = l >> 32;
		__put_user(l, ufdset);
		__put_user(h, ufdset+1);
		ufdset += 2;
		n -= 2;
	}
	if (odd)
		__put_user(*fdset, ufdset);
}

struct msgbuf32 {
    int mtype;
    char mtext[1];
};

asmlinkage long sys32_msgsnd(int msqid,
				struct msgbuf32 __user *umsgp32,
				size_t msgsz, int msgflg)
{
	struct msgbuf *mb;
	struct msgbuf32 mb32;
	int err;

	if ((mb = kmalloc(msgsz + sizeof *mb + 4, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	err = get_user(mb32.mtype, &umsgp32->mtype);
	mb->mtype = mb32.mtype;
	err |= copy_from_user(mb->mtext, &umsgp32->mtext, msgsz);

	if (err)
		err = -EFAULT;
	else
		KERNEL_SYSCALL(err, sys_msgsnd, msqid, (struct msgbuf __user *)mb, msgsz, msgflg);

	kfree(mb);
	return err;
}

asmlinkage long sys32_msgrcv(int msqid,
				struct msgbuf32 __user *umsgp32,
				size_t msgsz, long msgtyp, int msgflg)
{
	struct msgbuf *mb;
	struct msgbuf32 mb32;
	int err, len;

	if ((mb = kmalloc(msgsz + sizeof *mb + 4, GFP_KERNEL)) == NULL)
		return -ENOMEM;

	KERNEL_SYSCALL(err, sys_msgrcv, msqid, (struct msgbuf __user *)mb, msgsz, msgtyp, msgflg);

	if (err >= 0) {
		len = err;
		mb32.mtype = mb->mtype;
		err = put_user(mb32.mtype, &umsgp32->mtype);
		err |= copy_to_user(&umsgp32->mtext, mb->mtext, len);
		if (err)
			err = -EFAULT;
		else
			err = len;
	}

	kfree(mb);
	return err;
}

asmlinkage int sys32_sendfile(int out_fd, int in_fd, compat_off_t __user *offset, s32 count)
{
        mm_segment_t old_fs = get_fs();
        int ret;
        off_t of;

        if (offset && get_user(of, offset))
                return -EFAULT;

        set_fs(KERNEL_DS);
        ret = sys_sendfile(out_fd, in_fd, offset ? (off_t __user *)&of : NULL, count);
        set_fs(old_fs);

        if (offset && put_user(of, offset))
                return -EFAULT;

        return ret;
}

asmlinkage int sys32_sendfile64(int out_fd, int in_fd, compat_loff_t __user *offset, s32 count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	loff_t lof;
	
	if (offset && get_user(lof, offset))
		return -EFAULT;
		
	set_fs(KERNEL_DS);
	ret = sys_sendfile64(out_fd, in_fd, offset ? (loff_t __user *)&lof : NULL, count);
	set_fs(old_fs);
	
	if (offset && put_user(lof, offset))
		return -EFAULT;
		
	return ret;
}


/* lseek() needs a wrapper because 'offset' can be negative, but the top
 * half of the argument has been zeroed by syscall.S.
 */

asmlinkage int sys32_lseek(unsigned int fd, int offset, unsigned int origin)
{
	return sys_lseek(fd, offset, origin);
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

asmlinkage long compat_sys_fallocate(int fd, int mode, u32 offhi, u32 offlo,
				u32 lenhi, u32 lenlo)
{
        return sys_fallocate(fd, mode, ((loff_t)offhi << 32) | offlo,
                             ((loff_t)lenhi << 32) | lenlo);
}
