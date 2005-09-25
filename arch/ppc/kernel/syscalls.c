/*
 * arch/ppc/kernel/sys_ppc.c
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
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/syscalls.h>
#include <linux/mman.h>
#include <linux/sys.h>
#include <linux/ipc.h>
#include <linux/utsname.h>
#include <linux/file.h>
#include <linux/unistd.h>

#include <asm/uaccess.h>
#include <asm/ipc.h>
#include <asm/semaphore.h>


/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
int
sys_ipc (uint call, int first, int second, int third, void __user *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	ret = -ENOSYS;
	switch (call) {
	case SEMOP:
		ret = sys_semtimedop (first, (struct sembuf __user *)ptr,
				      second, NULL);
		break;
	case SEMTIMEDOP:
		ret = sys_semtimedop (first, (struct sembuf __user *)ptr,
				      second, (const struct timespec __user *) fifth);
		break;
	case SEMGET:
		ret = sys_semget (first, second, third);
		break;
	case SEMCTL: {
		union semun fourth;

		if (!ptr)
			break;
		if ((ret = access_ok(VERIFY_READ, ptr, sizeof(long)) ? 0 : -EFAULT)
		    || (ret = get_user(fourth.__pad, (void __user *__user *)ptr)))
			break;
		ret = sys_semctl (first, second, third, fourth);
		break;
		}
	case MSGSND:
		ret = sys_msgsnd (first, (struct msgbuf __user *) ptr, second, third);
		break;
	case MSGRCV:
		switch (version) {
		case 0: {
			struct ipc_kludge tmp;

			if (!ptr)
				break;
			if ((ret = access_ok(VERIFY_READ, ptr, sizeof(tmp)) ? 0 : -EFAULT)
			    || (ret = copy_from_user(&tmp,
					(struct ipc_kludge __user *) ptr,
					sizeof (tmp)) ? -EFAULT : 0))
				break;
			ret = sys_msgrcv (first, tmp.msgp, second, tmp.msgtyp,
					  third);
			break;
			}
		default:
			ret = sys_msgrcv (first, (struct msgbuf __user *) ptr,
					  second, fifth, third);
			break;
		}
		break;
	case MSGGET:
		ret = sys_msgget ((key_t) first, second);
		break;
	case MSGCTL:
		ret = sys_msgctl (first, second, (struct msqid_ds __user *) ptr);
		break;
	case SHMAT: {
		ulong raddr;

		if ((ret = access_ok(VERIFY_WRITE, (ulong __user *) third,
				       sizeof(ulong)) ? 0 : -EFAULT))
			break;
		ret = do_shmat (first, (char __user *) ptr, second, &raddr);
		if (ret)
			break;
		ret = put_user (raddr, (ulong __user *) third);
		break;
		}
	case SHMDT:
		ret = sys_shmdt ((char __user *)ptr);
		break;
	case SHMGET:
		ret = sys_shmget (first, second, third);
		break;
	case SHMCTL:
		ret = sys_shmctl (first, second, (struct shmid_ds __user *) ptr);
		break;
	}

	return ret;
}

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
int sys_pipe(int __user *fildes)
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

static inline unsigned long
do_mmap2(unsigned long addr, size_t len,
	 unsigned long prot, unsigned long flags,
	 unsigned long fd, unsigned long pgoff)
{
	struct file * file = NULL;
	int ret = -EBADF;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		if (!(file = fget(fd)))
			goto out;
	}

	down_write(&current->mm->mmap_sem);
	ret = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);
	if (file)
		fput(file);
out:
	return ret;
}

unsigned long sys_mmap2(unsigned long addr, size_t len,
			unsigned long prot, unsigned long flags,
			unsigned long fd, unsigned long pgoff)
{
	return do_mmap2(addr, len, prot, flags, fd, pgoff);
}

unsigned long sys_mmap(unsigned long addr, size_t len,
		       unsigned long prot, unsigned long flags,
		       unsigned long fd, off_t offset)
{
	int err = -EINVAL;

	if (offset & ~PAGE_MASK)
		goto out;

	err = do_mmap2(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
out:
	return err;
}

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
		if (!access_ok(VERIFY_READ, buffer, 5*sizeof(unsigned long))
		    || __get_user(n, buffer)
		    || __get_user(inp, ((fd_set __user * __user *)(buffer+1)))
		    || __get_user(outp, ((fd_set  __user * __user *)(buffer+2)))
		    || __get_user(exp, ((fd_set  __user * __user *)(buffer+3)))
		    || __get_user(tvp, ((struct timeval  __user * __user *)(buffer+4))))
			return -EFAULT;
	}
	return sys_select(n, inp, outp, exp, tvp);
}

int sys_uname(struct old_utsname __user * name)
{
	int err = -EFAULT;

	down_read(&uts_sem);
	if (name && !copy_to_user(name, &system_utsname, sizeof (*name)))
		err = 0;
	up_read(&uts_sem);
	return err;
}

int sys_olduname(struct oldold_utsname __user * name)
{
	int error;

	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE,name,sizeof(struct oldold_utsname)))
		return -EFAULT;

	down_read(&uts_sem);
	error = __copy_to_user(&name->sysname,&system_utsname.sysname,__OLD_UTS_LEN);
	error -= __put_user(0,name->sysname+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->nodename,&system_utsname.nodename,__OLD_UTS_LEN);
	error -= __put_user(0,name->nodename+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->release,&system_utsname.release,__OLD_UTS_LEN);
	error -= __put_user(0,name->release+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->version,&system_utsname.version,__OLD_UTS_LEN);
	error -= __put_user(0,name->version+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->machine,&system_utsname.machine,__OLD_UTS_LEN);
	error = __put_user(0,name->machine+__OLD_UTS_LEN);
	up_read(&uts_sem);

	error = error ? -EFAULT : 0;
	return error;
}

/*
 * We put the arguments in a different order so we only use 6
 * registers for arguments, rather than 7 as sys_fadvise64_64 needs
 * (because `offset' goes in r5/r6).
 */
long ppc_fadvise64_64(int fd, int advice, loff_t offset, loff_t len)
{
	return sys_fadvise64_64(fd, offset, len, advice);
}
