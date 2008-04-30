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
#include <linux/init.h>
#include <linux/personality.h>

#include <asm/uaccess.h>
#include <asm/syscalls.h>
#include <asm/time.h>
#include <asm/unistd.h>

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
int sys_ipc(uint call, int first, unsigned long second, long third,
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
		ret = sys_msgget((key_t)first, (int)second);
		break;
	case MSGCTL:
		ret = sys_msgctl(first, (int)second,
				  (struct msqid_ds __user *)ptr);
		break;
	case SHMAT: {
		ulong raddr;
		ret = do_shmat(first, (char __user *)ptr, (int)second, &raddr);
		if (ret)
			break;
		ret = put_user(raddr, (ulong __user *) third);
		break;
	}
	case SHMDT:
		ret = sys_shmdt((char __user *)ptr);
		break;
	case SHMGET:
		ret = sys_shmget(first, (size_t)second, third);
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

static inline unsigned long do_mmap2(unsigned long addr, size_t len,
			unsigned long prot, unsigned long flags,
			unsigned long fd, unsigned long off, int shift)
{
	struct file * file = NULL;
	unsigned long ret = -EINVAL;

	if (shift) {
		if (off & ((1 << shift) - 1))
			goto out;
		off >>= shift;
	}
		
	ret = -EBADF;
	if (!(flags & MAP_ANONYMOUS)) {
		if (!(file = fget(fd)))
			goto out;
	}

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	down_write(&current->mm->mmap_sem);
	ret = do_mmap_pgoff(file, addr, len, prot, flags, off);
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
	return do_mmap2(addr, len, prot, flags, fd, pgoff, PAGE_SHIFT-12);
}

unsigned long sys_mmap(unsigned long addr, size_t len,
		       unsigned long prot, unsigned long flags,
		       unsigned long fd, off_t offset)
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
#endif

#ifdef CONFIG_PPC64
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
#endif

#ifdef CONFIG_PPC64
#define OVERRIDE_MACHINE    (personality(current->personality) == PER_LINUX32)
#else
#define OVERRIDE_MACHINE    0
#endif

static inline int override_machine(char __user *mach)
{
	if (OVERRIDE_MACHINE) {
		/* change ppc64 to ppc */
		if (__put_user(0, mach+3) || __put_user(0, mach+4))
			return -EFAULT;
	}
	return 0;
}

long ppc_newuname(struct new_utsname __user * name)
{
	int err = 0;

	down_read(&uts_sem);
	if (copy_to_user(name, utsname(), sizeof(*name)))
		err = -EFAULT;
	up_read(&uts_sem);
	if (!err)
		err = override_machine(name->machine);
	return err;
}

int sys_uname(struct old_utsname __user *name)
{
	int err = 0;
	
	down_read(&uts_sem);
	if (copy_to_user(name, utsname(), sizeof(*name)))
		err = -EFAULT;
	up_read(&uts_sem);
	if (!err)
		err = override_machine(name->machine);
	return err;
}

int sys_olduname(struct oldold_utsname __user *name)
{
	int error;

	if (!access_ok(VERIFY_WRITE, name, sizeof(struct oldold_utsname)))
		return -EFAULT;
  
	down_read(&uts_sem);
	error = __copy_to_user(&name->sysname, &utsname()->sysname,
			       __OLD_UTS_LEN);
	error |= __put_user(0, name->sysname + __OLD_UTS_LEN);
	error |= __copy_to_user(&name->nodename, &utsname()->nodename,
				__OLD_UTS_LEN);
	error |= __put_user(0, name->nodename + __OLD_UTS_LEN);
	error |= __copy_to_user(&name->release, &utsname()->release,
				__OLD_UTS_LEN);
	error |= __put_user(0, name->release + __OLD_UTS_LEN);
	error |= __copy_to_user(&name->version, &utsname()->version,
				__OLD_UTS_LEN);
	error |= __put_user(0, name->version + __OLD_UTS_LEN);
	error |= __copy_to_user(&name->machine, &utsname()->machine,
				__OLD_UTS_LEN);
	error |= override_machine(name->machine);
	up_read(&uts_sem);

	return error? -EFAULT: 0;
}

long ppc_fadvise64_64(int fd, int advice, u32 offset_high, u32 offset_low,
		      u32 len_high, u32 len_low)
{
	return sys_fadvise64(fd, (u64)offset_high << 32 | offset_low,
			     (u64)len_high << 32 | len_low, advice);
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
