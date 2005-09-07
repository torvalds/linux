/*
 * linux/arch/ppc64/kernel/sys_ppc.c
 *
 *  PowerPC version 
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
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/sys.h>
#include <linux/ipc.h>
#include <linux/utsname.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/personality.h>

#include <asm/uaccess.h>
#include <asm/ipc.h>
#include <asm/semaphore.h>
#include <asm/time.h>
#include <asm/unistd.h>

extern unsigned long wall_jiffies;


/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage int 
sys_ipc (uint call, int first, unsigned long second, long third,
	 void __user *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	ret = -ENOSYS;
	switch (call) {
	case SEMOP:
		ret = sys_semtimedop(first, (struct sembuf __user *)ptr,
				      (unsigned)second, NULL);
		break;
	case SEMTIMEDOP:
		ret = sys_semtimedop(first, (struct sembuf __user *)ptr,
				      (unsigned)second,
				      (const struct timespec __user *) fifth);
		break;
	case SEMGET:
		ret = sys_semget (first, (int)second, third);
		break;
	case SEMCTL: {
		union semun fourth;

		ret = -EINVAL;
		if (!ptr)
			break;
		if ((ret = get_user(fourth.__pad, (void __user * __user *)ptr)))
			break;
		ret = sys_semctl(first, (int)second, third, fourth);
		break;
	}
	case MSGSND:
		ret = sys_msgsnd(first, (struct msgbuf __user *)ptr,
				  (size_t)second, third);
		break;
	case MSGRCV:
		switch (version) {
		case 0: {
			struct ipc_kludge tmp;

			ret = -EINVAL;
			if (!ptr)
				break;
			if ((ret = copy_from_user(&tmp,
						(struct ipc_kludge __user *) ptr,
						sizeof (tmp)) ? -EFAULT : 0))
				break;
			ret = sys_msgrcv(first, tmp.msgp, (size_t) second,
					  tmp.msgtyp, third);
			break;
		}
		default:
			ret = sys_msgrcv (first, (struct msgbuf __user *) ptr,
					  (size_t)second, fifth, third);
			break;
		}
		break;
	case MSGGET:
		ret = sys_msgget ((key_t)first, (int)second);
		break;
	case MSGCTL:
		ret = sys_msgctl(first, (int)second,
				  (struct msqid_ds __user *)ptr);
		break;
	case SHMAT:
		switch (version) {
		default: {
			ulong raddr;
			ret = do_shmat(first, (char __user *) ptr,
					(int)second, &raddr);
			if (ret)
				break;
			ret = put_user (raddr, (ulong __user *) third);
			break;
		}
		case 1:	/* iBCS2 emulator entry point */
			ret = -EINVAL;
			if (!segment_eq(get_fs(), get_ds()))
				break;
			ret = do_shmat(first, (char __user *)ptr,
					(int)second, (ulong *)third);
			break;
		}
		break;
	case SHMDT: 
		ret = sys_shmdt ((char __user *)ptr);
		break;
	case SHMGET:
		ret = sys_shmget (first, (size_t)second, third);
		break;
	case SHMCTL:
		ret = sys_shmctl(first, (int)second,
				  (struct shmid_ds __user *)ptr);
		break;
	}

	return ret;
}

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage int sys_pipe(int __user *fildes)
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

unsigned long sys_mmap(unsigned long addr, size_t len,
		       unsigned long prot, unsigned long flags,
		       unsigned long fd, off_t offset)
{
	struct file * file = NULL;
	unsigned long ret = -EBADF;

	if (!(flags & MAP_ANONYMOUS)) {
		if (!(file = fget(fd)))
			goto out;
	}

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	down_write(&current->mm->mmap_sem);
	ret = do_mmap(file, addr, len, prot, flags, offset);
	up_write(&current->mm->mmap_sem);
	if (file)
		fput(file);

out:
	return ret;
}

long ppc64_personality(unsigned long personality)
{
	long ret;

	if (personality(current->personality) == PER_LINUX32
	    && personality == PER_LINUX)
		personality = PER_LINUX32;
	ret = sys_personality(personality);
	if (ret == PER_LINUX32)
		ret = PER_LINUX;
	return ret;
}

long ppc64_newuname(struct new_utsname __user * name)
{
	int err = 0;

	down_read(&uts_sem);
	if (copy_to_user(name, &system_utsname, sizeof(*name)))
		err = -EFAULT;
	up_read(&uts_sem);
	if (!err && personality(current->personality) == PER_LINUX32) {
		/* change ppc64 to ppc */
		if (__put_user(0, name->machine + 3)
		    || __put_user(0, name->machine + 4))
			err = -EFAULT;
	}
	return err;
}

asmlinkage time_t sys64_time(time_t __user * tloc)
{
	time_t secs;
	time_t usecs;

	long tb_delta = tb_ticks_since(tb_last_stamp);
	tb_delta += (jiffies - wall_jiffies) * tb_ticks_per_jiffy;

	secs  = xtime.tv_sec;  
	usecs = (xtime.tv_nsec/1000) + tb_delta / tb_ticks_per_usec;
	while (usecs >= USEC_PER_SEC) {
		++secs;
		usecs -= USEC_PER_SEC;
	}

	if (tloc) {
		if (put_user(secs,tloc))
			secs = -EFAULT;
	}

	return secs;
}

void do_show_syscall(unsigned long r3, unsigned long r4, unsigned long r5,
		     unsigned long r6, unsigned long r7, unsigned long r8,
		     struct pt_regs *regs)
{
	printk("syscall %ld(%lx, %lx, %lx, %lx, %lx, %lx) regs=%p current=%p"
	       " cpu=%d\n", regs->gpr[0], r3, r4, r5, r6, r7, r8, regs,
	       current, smp_processor_id());
}

void do_show_syscall_exit(unsigned long r3)
{
	printk(" -> %lx, current=%p cpu=%d\n", r3, current, smp_processor_id());
}
