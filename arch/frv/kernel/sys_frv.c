/* sys_frv.c: FRV arch-specific syscall wrappers
 *
 * Copyright (C) 2003-5 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from arch/m68k/kernel/sys_m68k.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/syscalls.h>

#include <asm/setup.h>
#include <asm/uaccess.h>
#include <asm/ipc.h>

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage long sys_pipe(unsigned long * fildes)
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

asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
			  unsigned long prot, unsigned long flags,
			  unsigned long fd, unsigned long pgoff)
{
	int error = -EBADF;
	struct file * file = NULL;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	/* As with sparc32, make sure the shift for mmap2 is constant
	   (12), no matter what PAGE_SIZE we have.... */

	/* But unlike sparc32, don't just silently break if we're
	   trying to map something we can't */
	if (pgoff & ((1<<(PAGE_SHIFT-12))-1))
		return -EINVAL;

	pgoff >>= (PAGE_SHIFT - 12);

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
out:
	return error;
}

#if 0 /* DAVIDM - do we want this */
struct mmap_arg_struct64 {
	__u32 addr;
	__u32 len;
	__u32 prot;
	__u32 flags;
	__u64 offset; /* 64 bits */
	__u32 fd;
};

asmlinkage long sys_mmap64(struct mmap_arg_struct64 *arg)
{
	int error = -EFAULT;
	struct file * file = NULL;
	struct mmap_arg_struct64 a;
	unsigned long pgoff;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;

	if ((long)a.offset & ~PAGE_MASK)
		return -EINVAL;

	pgoff = a.offset >> PAGE_SHIFT;
	if ((a.offset >> PAGE_SHIFT) != pgoff)
		return -EINVAL;

	if (!(a.flags & MAP_ANONYMOUS)) {
		error = -EBADF;
		file = fget(a.fd);
		if (!file)
			goto out;
	}
	a.flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, a.addr, a.len, a.prot, a.flags, pgoff);
	up_write(&current->mm->mmap_sem);
	if (file)
		fput(file);
out:
	return error;
}
#endif

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage long sys_ipc(unsigned long call,
			unsigned long first,
			unsigned long second,
			unsigned long third,
			void __user *ptr,
			unsigned long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {
	case SEMOP:
		return sys_semtimedop(first, (struct sembuf __user *)ptr, second, NULL);
	case SEMTIMEDOP:
		return sys_semtimedop(first, (struct sembuf __user *)ptr, second,
				      (const struct timespec __user *)fifth);

	case SEMGET:
		return sys_semget (first, second, third);
	case SEMCTL: {
		union semun fourth;
		if (!ptr)
			return -EINVAL;
		if (get_user(fourth.__pad, (void * __user *) ptr))
			return -EFAULT;
		return sys_semctl (first, second, third, fourth);
	}

	case MSGSND:
		return sys_msgsnd (first, (struct msgbuf __user *) ptr,
				   second, third);
	case MSGRCV:
		switch (version) {
		case 0: {
			struct ipc_kludge tmp;
			if (!ptr)
				return -EINVAL;

			if (copy_from_user(&tmp,
					   (struct ipc_kludge __user *) ptr,
					   sizeof (tmp)))
				return -EFAULT;
			return sys_msgrcv (first, tmp.msgp, second,
					   tmp.msgtyp, third);
		}
		default:
			return sys_msgrcv (first,
					   (struct msgbuf __user *) ptr,
					   second, fifth, third);
		}
	case MSGGET:
		return sys_msgget ((key_t) first, second);
	case MSGCTL:
		return sys_msgctl (first, second, (struct msqid_ds __user *) ptr);

	case SHMAT:
		switch (version) {
		default: {
			ulong raddr;
			ret = do_shmat (first, (char __user *) ptr, second, &raddr);
			if (ret)
				return ret;
			return put_user (raddr, (ulong __user *) third);
		}
		case 1:	/* iBCS2 emulator entry point */
			if (!segment_eq(get_fs(), get_ds()))
				return -EINVAL;
			/* The "(ulong *) third" is valid _only_ because of the kernel segment thing */
			return do_shmat (first, (char __user *) ptr, second, (ulong *) third);
		}
	case SHMDT:
		return sys_shmdt ((char __user *)ptr);
	case SHMGET:
		return sys_shmget (first, second, third);
	case SHMCTL:
		return sys_shmctl (first, second,
				   (struct shmid_ds __user *) ptr);
	default:
		return -ENOSYS;
	}
}
