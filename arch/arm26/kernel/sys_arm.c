/*
 *  linux/arch/arm26/kernel/sys_arm.c
 *
 *  Copyright (C) People who wrote linux/arch/i386/kernel/sys_i386.c
 *  Copyright (C) 1995, 1996 Russell King.
 *  Copyright (C) 2003 Ian Molton.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file contains various random system calls that
 *  have a non-standard calling sequence on the Linux/arm
 *  platform.
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/syscalls.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/utsname.h>

#include <asm/uaccess.h>
#include <asm/ipc.h>

extern unsigned long do_mremap(unsigned long addr, unsigned long old_len,
			       unsigned long new_len, unsigned long flags,
			       unsigned long new_addr);

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage int sys_pipe(unsigned long * fildes)
{
	int fd[2];
	int error;

	error = do_pipe(fd);
	if (!error) {
		if (copy_to_user(fildes, fd, 2*sizeof(int)))
			error = -EFAULT;
	}
	return error;
}

/* common code for old and new mmaps */
inline long do_mmap2(
	unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	int error = -EINVAL;
	struct file * file = NULL;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	/*
	 * If we are doing a fixed mapping, and address < FIRST_USER_ADDRESS,
	 * then deny it.
	 */
	if (flags & MAP_FIXED && addr < FIRST_USER_ADDRESS)
		goto out;

	error = -EBADF;
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
out:
	return error;
}

struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

asmlinkage int old_mmap(struct mmap_arg_struct *arg)
{
	int error = -EFAULT;
	struct mmap_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		goto out;

	error = -EINVAL;
	if (a.offset & ~PAGE_MASK)
		goto out;

	error = do_mmap2(a.addr, a.len, a.prot, a.flags, a.fd, a.offset >> PAGE_SHIFT);
out:
	return error;
}

asmlinkage unsigned long
sys_arm_mremap(unsigned long addr, unsigned long old_len,
	       unsigned long new_len, unsigned long flags,
	       unsigned long new_addr)
{
	unsigned long ret = -EINVAL;

	/*
	 * If we are doing a fixed mapping, and address < FIRST_USER_ADDRESS,
	 * then deny it.
	 */
	if (flags & MREMAP_FIXED && new_addr < FIRST_USER_ADDRESS)
		goto out;

	down_write(&current->mm->mmap_sem);
	ret = do_mremap(addr, old_len, new_len, flags, new_addr);
	up_write(&current->mm->mmap_sem);

out:
	return ret;
}

/*
 * Perform the select(nd, in, out, ex, tv) and mmap() system
 * calls.
 */

struct sel_arg_struct {
	unsigned long n;
	fd_set *inp, *outp, *exp;
	struct timeval *tvp;
};

asmlinkage int old_select(struct sel_arg_struct *arg)
{
	struct sel_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	/* sys_select() does the appropriate kernel locking */
	return sys_select(a.n, a.inp, a.outp, a.exp, a.tvp);
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage int sys_ipc (uint call, int first, int second, int third, void *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {
	case SEMOP:
		return sys_semop (first, (struct sembuf *)ptr, second);
	case SEMGET:
		return sys_semget (first, second, third);
	case SEMCTL: {
		union semun fourth;
		if (!ptr)
			return -EINVAL;
		if (get_user(fourth.__pad, (void **) ptr))
			return -EFAULT;
		return sys_semctl (first, second, third, fourth);
	}

	case MSGSND:
		return sys_msgsnd (first, (struct msgbuf *) ptr, 
				   second, third);
	case MSGRCV:
		switch (version) {
		case 0: {
			struct ipc_kludge tmp;
			if (!ptr)
				return -EINVAL;
			if (copy_from_user(&tmp,(struct ipc_kludge *) ptr,
					   sizeof (tmp)))
				return -EFAULT;
			return sys_msgrcv (first, tmp.msgp, second,
					   tmp.msgtyp, third);
		}
		default:
			return sys_msgrcv (first,
					   (struct msgbuf *) ptr,
					   second, fifth, third);
		}
	case MSGGET:
		return sys_msgget ((key_t) first, second);
	case MSGCTL:
		return sys_msgctl (first, second, (struct msqid_ds *) ptr);

	case SHMAT:
		switch (version) {
		default: {
			ulong raddr;
			ret = do_shmat (first, (char *) ptr, second, &raddr);
			if (ret)
				return ret;
			return put_user (raddr, (ulong *) third);
		}
		case 1:	/* iBCS2 emulator entry point */
			if (!segment_eq(get_fs(), get_ds()))
				return -EINVAL;
			return do_shmat (first, (char *) ptr,
					  second, (ulong *) third);
		}
	case SHMDT: 
		return sys_shmdt ((char *)ptr);
	case SHMGET:
		return sys_shmget (first, second, third);
	case SHMCTL:
		return sys_shmctl (first, second,
				   (struct shmid_ds *) ptr);
	default:
		return -EINVAL;
	}
}

/* Fork a new task - this creates a new program thread.
 * This is called indirectly via a small wrapper
 */
asmlinkage int sys_fork(struct pt_regs *regs)
{
	return do_fork(SIGCHLD, regs->ARM_sp, regs, 0, NULL, NULL);
}

/* Clone a task - this clones the calling program thread.
 * This is called indirectly via a small wrapper
 */
asmlinkage int sys_clone(unsigned long clone_flags, unsigned long newsp, struct pt_regs *regs)
{
	/*
         * We don't support SETTID / CLEARTID  (FIXME!!! (nicked from arm32))
         */
        if (clone_flags & (CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID))
                return -EINVAL;
	
	if (!newsp)
		newsp = regs->ARM_sp;

	return do_fork(clone_flags, newsp, regs, 0, NULL, NULL);
}

asmlinkage int sys_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->ARM_sp, regs, 0, NULL, NULL);
}

/* sys_execve() executes a new program.
 * This is called indirectly via a small wrapper
 */
asmlinkage int sys_execve(char *filenamei, char **argv, char **envp, struct pt_regs *regs)
{
	int error;
	char * filename;

	filename = getname(filenamei);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
out:
	return error;
}

/* FIXME - see if this is correct for arm26 */
int kernel_execve(const char *filename, char *const argv[], char *const envp[])
{
	struct pt_regs regs;
        int ret;
         memset(&regs, 0, sizeof(struct pt_regs));
        ret = do_execve((char *)filename, (char __user * __user *)argv,                         (char __user * __user *)envp, &regs);
        if (ret < 0)
                goto out;

        /*
         * Save argc to the register structure for userspace.
         */
        regs.ARM_r0 = ret;

        /*
         * We were successful.  We won't be returning to our caller, but
         * instead to user space by manipulating the kernel stack.
         */
        asm(    "add    r0, %0, %1\n\t"
                "mov    r1, %2\n\t"
                "mov    r2, %3\n\t"
                "bl     memmove\n\t"    /* copy regs to top of stack */
                "mov    r8, #0\n\t"     /* not a syscall */
                "mov    r9, %0\n\t"     /* thread structure */
                "mov    sp, r0\n\t"     /* reposition stack pointer */
                "b      ret_to_user"
                :
                : "r" (current_thread_info()),
                  "Ir" (THREAD_SIZE - 8 - sizeof(regs)),
                  "r" (&regs),
                  "Ir" (sizeof(regs))
                : "r0", "r1", "r2", "r3", "ip", "memory");

 out:
        return ret;
}

EXPORT_SYMBOL(kernel_execve);
