/* MN10300 Weird system calls
 *
 * Copyright (C) 2007 Matsushita Electric Industrial Co., Ltd.
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/tty.h>

#include <asm/uaccess.h>

asmlinkage long old_mmap(unsigned long addr, unsigned long len,
			 unsigned long prot, unsigned long flags,
			 unsigned long fd, unsigned long offset)
{
	if (offset & ~PAGE_MASK)
		return -EINVAL;
	return sys_mmap_pgoff(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
}

struct sel_arg_struct {
	unsigned long n;
	fd_set *inp;
	fd_set *outp;
	fd_set *exp;
	struct timeval *tvp;
};

asmlinkage int old_select(struct sel_arg_struct __user *arg)
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
asmlinkage long sys_ipc(uint call, int first, int second,
			int third, void __user *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {
	case SEMOP:
		return sys_semtimedop(first, (struct sembuf __user *)ptr,
				      second, NULL);
	case SEMTIMEDOP:
		return sys_semtimedop(first, (struct sembuf __user *)ptr,
				      second,
				      (const struct timespec __user *)fifth);
	case SEMGET:
		return sys_semget(first, second, third);
	case SEMCTL: {
		union semun fourth;
		if (!ptr)
			return -EINVAL;
		if (get_user(fourth.__pad, (void __user * __user *) ptr))
			return -EFAULT;
		return sys_semctl(first, second, third, fourth);
	}

	case MSGSND:
		return sys_msgsnd(first, (struct msgbuf __user *) ptr,
				  second, third);
	case MSGRCV:
		switch (version) {
		case 0: {
			struct ipc_kludge tmp;
			if (!ptr)
				return -EINVAL;

			if (copy_from_user(&tmp,
					   (struct ipc_kludge __user *) ptr,
					   sizeof(tmp)))
				return -EFAULT;
			return sys_msgrcv(first, tmp.msgp, second,
					  tmp.msgtyp, third);
		}
		default:
			return sys_msgrcv(first,
					  (struct msgbuf __user *) ptr,
					   second, fifth, third);
		}
	case MSGGET:
		return sys_msgget((key_t) first, second);
	case MSGCTL:
		return sys_msgctl(first, second,
				   (struct msqid_ds __user *) ptr);

	case SHMAT:
		switch (version) {
		default: {
			ulong raddr;
			ret = do_shmat(first, (char __user *) ptr, second,
				       &raddr);
			if (ret)
				return ret;
			return put_user(raddr, (ulong *) third);
		}
		case 1:	/* iBCS2 emulator entry point */
			if (!segment_eq(get_fs(), get_ds()))
				return -EINVAL;
			return do_shmat(first, (char __user *) ptr, second,
					(ulong *) third);
		}
	case SHMDT:
		return sys_shmdt((char __user *)ptr);
	case SHMGET:
		return sys_shmget(first, second, third);
	case SHMCTL:
		return sys_shmctl(first, second,
				  (struct shmid_ds __user *) ptr);
	default:
		return -EINVAL;
	}
}
