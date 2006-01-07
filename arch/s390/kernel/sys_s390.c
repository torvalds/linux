/*
 *  arch/s390/kernel/sys_s390.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Thomas Spatzier (tspat@de.ibm.com)
 *
 *  Derived from "arch/i386/kernel/sys_i386.c"
 *
 *  This file contains various random system calls that
 *  have a non-standard calling sequence on the Linux/s390
 *  platform.
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
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/personality.h>

#include <asm/uaccess.h>
#include <asm/ipc.h>

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way Unix traditionally does this, though.
 */
asmlinkage long sys_pipe(unsigned long __user *fildes)
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
static inline long do_mmap2(
	unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	long error = -EBADF;
	struct file * file = NULL;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
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

/*
 * Perform the select(nd, in, out, ex, tv) and mmap() system
 * calls. Linux for S/390 isn't able to handle more than 5
 * system call parameters, so these system calls used a memory
 * block for parameter passing..
 */

struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

asmlinkage long sys_mmap2(struct mmap_arg_struct __user  *arg)
{
	struct mmap_arg_struct a;
	int error = -EFAULT;

	if (copy_from_user(&a, arg, sizeof(a)))
		goto out;
	error = do_mmap2(a.addr, a.len, a.prot, a.flags, a.fd, a.offset);
out:
	return error;
}

asmlinkage long old_mmap(struct mmap_arg_struct __user *arg)
{
	struct mmap_arg_struct a;
	long error = -EFAULT;

	if (copy_from_user(&a, arg, sizeof(a)))
		goto out;

	error = -EINVAL;
	if (a.offset & ~PAGE_MASK)
		goto out;

	error = do_mmap2(a.addr, a.len, a.prot, a.flags, a.fd, a.offset >> PAGE_SHIFT);
out:
	return error;
}

#ifndef CONFIG_64BIT
struct sel_arg_struct {
	unsigned long n;
	fd_set *inp, *outp, *exp;
	struct timeval *tvp;
};

asmlinkage long old_select(struct sel_arg_struct __user *arg)
{
	struct sel_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	/* sys_select() does the appropriate kernel locking */
	return sys_select(a.n, a.inp, a.outp, a.exp, a.tvp);

}
#endif /* CONFIG_64BIT */

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage long sys_ipc(uint call, int first, unsigned long second,
				  unsigned long third, void __user *ptr)
{
        struct ipc_kludge tmp;
	int ret;

        switch (call) {
        case SEMOP:
		return sys_semtimedop(first, (struct sembuf __user *)ptr,
				       (unsigned)second, NULL);
	case SEMTIMEDOP:
		return sys_semtimedop(first, (struct sembuf __user *)ptr,
				       (unsigned)second,
				       (const struct timespec __user *) third);
        case SEMGET:
                return sys_semget(first, (int)second, third);
        case SEMCTL: {
                union semun fourth;
                if (!ptr)
                        return -EINVAL;
                if (get_user(fourth.__pad, (void __user * __user *) ptr))
                        return -EFAULT;
                return sys_semctl(first, (int)second, third, fourth);
        }
        case MSGSND:
		return sys_msgsnd (first, (struct msgbuf __user *) ptr,
                                   (size_t)second, third);
		break;
        case MSGRCV:
                if (!ptr)
                        return -EINVAL;
                if (copy_from_user (&tmp, (struct ipc_kludge __user *) ptr,
                                    sizeof (struct ipc_kludge)))
                        return -EFAULT;
                return sys_msgrcv (first, tmp.msgp,
                                   (size_t)second, tmp.msgtyp, third);
        case MSGGET:
                return sys_msgget((key_t)first, (int)second);
        case MSGCTL:
                return sys_msgctl(first, (int)second,
				   (struct msqid_ds __user *)ptr);

	case SHMAT: {
		ulong raddr;
		ret = do_shmat(first, (char __user *)ptr,
				(int)second, &raddr);
		if (ret)
			return ret;
		return put_user (raddr, (ulong __user *) third);
		break;
        }
	case SHMDT:
		return sys_shmdt ((char __user *)ptr);
	case SHMGET:
		return sys_shmget(first, (size_t)second, third);
	case SHMCTL:
		return sys_shmctl(first, (int)second,
                                   (struct shmid_ds __user *) ptr);
	default:
		return -ENOSYS;

	}

	return -EINVAL;
}

#ifdef CONFIG_64BIT
asmlinkage long s390x_newuname(struct new_utsname __user *name)
{
	int ret = sys_newuname(name);

	if (current->personality == PER_LINUX32 && !ret) {
		ret = copy_to_user(name->machine, "s390\0\0\0\0", 8);
		if (ret) ret = -EFAULT;
	}
	return ret;
}

asmlinkage long s390x_personality(unsigned long personality)
{
	int ret;

	if (current->personality == PER_LINUX32 && personality == PER_LINUX)
		personality = PER_LINUX32;
	ret = sys_personality(personality);
	if (ret == PER_LINUX32)
		ret = PER_LINUX;

	return ret;
}
#endif /* CONFIG_64BIT */

/*
 * Wrapper function for sys_fadvise64/fadvise64_64
 */
#ifndef CONFIG_64BIT

asmlinkage long
s390_fadvise64(int fd, u32 offset_high, u32 offset_low, size_t len, int advice)
{
	return sys_fadvise64(fd, (u64) offset_high << 32 | offset_low,
			len, advice);
}

#endif

struct fadvise64_64_args {
	int fd;
	long long offset;
	long long len;
	int advice;
};

asmlinkage long
s390_fadvise64_64(struct fadvise64_64_args __user *args)
{
	struct fadvise64_64_args a;

	if ( copy_from_user(&a, args, sizeof(a)) )
		return -EFAULT;
	return sys_fadvise64_64(a.fd, a.offset, a.len, a.advice);
}

