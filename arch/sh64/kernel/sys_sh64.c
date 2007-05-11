/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * arch/sh64/kernel/sys_sh64.c
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/SH5
 * platform.
 *
 * Mostly taken from i386 version.
 *
 */

#include <linux/errno.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/syscalls.h>
#include <asm/uaccess.h>
#include <asm/ipc.h>
#include <asm/ptrace.h>
#include <asm/unistd.h>

#define REG_3	3

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way Unix traditionally does this, though.
 */
#ifdef NEW_PIPE_IMPLEMENTATION
asmlinkage int sys_pipe(unsigned long * fildes,
			unsigned long   dummy_r3,
			unsigned long   dummy_r4,
			unsigned long   dummy_r5,
			unsigned long   dummy_r6,
			unsigned long   dummy_r7,
			struct pt_regs * regs)	   /* r8 = pt_regs  forced by entry.S */
{
	int fd[2];
	int ret;

	ret = do_pipe(fd);
	if (ret == 0)
		/*
		 ***********************************************************************
		 *   To avoid the copy_to_user we prefer to break the ABIs convention, *
		 *   packing the valid pair of file IDs into a single register (r3);   *
		 *   while r2 is the return code as defined by the sh5-ABIs.	       *
		 *   BE CAREFUL: pipe stub, into glibc, must be aware of this solution *
		 ***********************************************************************

#ifdef __LITTLE_ENDIAN__
		regs->regs[REG_3] = (((unsigned long long) fd[1]) << 32) | ((unsigned long long) fd[0]);
#else
		regs->regs[REG_3] = (((unsigned long long) fd[0]) << 32) | ((unsigned long long) fd[1]);
#endif

		*/
	       /* although not very clever this is endianess independent */
		regs->regs[REG_3] = (unsigned long long) *((unsigned long long *) fd);

	return ret;
}

#else
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

#endif

/*
 * To avoid cache alias, we map the shard page with same color.
 */
#define COLOUR_ALIGN(addr)	(((addr)+SHMLBA-1)&~(SHMLBA-1))

unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr,
	unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct vm_area_struct *vma;

	if (flags & MAP_FIXED) {
		/* We do not accept a shared mapping if it would violate
		 * cache aliasing constraints.
		 */
		if ((flags & MAP_SHARED) && (addr & (SHMLBA - 1)))
			return -EINVAL;
		return addr;
	}

	if (len > TASK_SIZE)
		return -ENOMEM;
	if (!addr)
		addr = TASK_UNMAPPED_BASE;

	if (flags & MAP_PRIVATE)
		addr = PAGE_ALIGN(addr);
	else
		addr = COLOUR_ALIGN(addr);

	for (vma = find_vma(current->mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (TASK_SIZE - len < addr)
			return -ENOMEM;
		if (!vma || addr + len <= vma->vm_start)
			return addr;
		addr = vma->vm_end;
		if (!(flags & MAP_PRIVATE))
			addr = COLOUR_ALIGN(addr);
	}
}

/* common code for old and new mmaps */
static inline long do_mmap2(
	unsigned long addr, unsigned long len,
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

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
out:
	return error;
}

asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	return do_mmap2(addr, len, prot, flags, fd, pgoff);
}

asmlinkage int old_mmap(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	int fd, unsigned long off)
{
	if (off & ~PAGE_MASK)
		return -EINVAL;
	return do_mmap2(addr, len, prot, flags, fd, off>>PAGE_SHIFT);
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage int sys_ipc(uint call, int first, int second,
		       int third, void __user *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	if (call <= SEMCTL)
		switch (call) {
		case SEMOP:
			return sys_semtimedop(first, (struct sembuf __user *)ptr,
					      second, NULL);
		case SEMTIMEDOP:
			return sys_semtimedop(first, (struct sembuf __user *)ptr,
					      second,
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
		default:
			return -EINVAL;
		}

	if (call <= MSGCTL)
		switch (call) {
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
			return sys_msgctl (first, second,
					   (struct msqid_ds __user *) ptr);
		default:
			return -EINVAL;
		}
	if (call <= SHMCTL)
		switch (call) {
		case SHMAT:
			switch (version) {
			default: {
				ulong raddr;
				ret = do_shmat (first, (char __user *) ptr,
						 second, &raddr);
				if (ret)
					return ret;
				return put_user (raddr, (ulong __user *) third);
			}
			case 1:	/* iBCS2 emulator entry point */
				if (!segment_eq(get_fs(), get_ds()))
					return -EINVAL;
				return do_shmat (first, (char __user *) ptr,
						  second, (ulong *) third);
			}
		case SHMDT:
			return sys_shmdt ((char __user *)ptr);
		case SHMGET:
			return sys_shmget (first, second, third);
		case SHMCTL:
			return sys_shmctl (first, second,
					   (struct shmid_ds __user *) ptr);
		default:
			return -EINVAL;
		}

	return -EINVAL;
}

asmlinkage int sys_uname(struct old_utsname * name)
{
	int err;
	if (!name)
		return -EFAULT;
	down_read(&uts_sem);
	err = copy_to_user(name, utsname(), sizeof (*name));
	up_read(&uts_sem);
	return err?-EFAULT:0;
}

/*
 * Do a system call from kernel instead of calling sys_execve so we
 * end up with proper pt_regs.
 */
int kernel_execve(const char *filename, char *const argv[], char *const envp[])
{
	register unsigned long __sc0 __asm__ ("r9") = ((0x13 << 16) | __NR_execve);
	register unsigned long __sc2 __asm__ ("r2") = (unsigned long) filename;
	register unsigned long __sc3 __asm__ ("r3") = (unsigned long) argv;
	register unsigned long __sc4 __asm__ ("r4") = (unsigned long) envp;
	__asm__ __volatile__ ("trapa	%1 !\t\t\t execve(%2,%3,%4)"
	: "=r" (__sc0)
	: "r" (__sc0), "r" (__sc2), "r" (__sc3), "r" (__sc4) );
	__asm__ __volatile__ ("!dummy	%0 %1 %2 %3"
	: : "r" (__sc0), "r" (__sc2), "r" (__sc3), "r" (__sc4) : "memory");
	return __sc0;
}
