/*
 * arch/v850/kernel/syscalls.c -- Various system-call definitions not
 * 	defined in machine-independent code
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * This file was derived the ppc version, arch/ppc/kernel/syscalls.c
 * ... which was derived from "arch/i386/kernel/sys_i386.c" by Gary Thomas;
 *     modified by Cort Dougan (cort@cs.nmt.edu)
 *     and Paul Mackerras (paulus@cs.anu.edu.au).
 */

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/syscalls.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/sys.h>
#include <linux/ipc.h>
#include <linux/utsname.h>
#include <linux/file.h>

#include <asm/uaccess.h>
#include <asm/ipc.h>
#include <asm/semaphore.h>
#include <asm/unistd.h>

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
int
sys_ipc (uint call, int first, int second, int third, void *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	ret = -EINVAL;
	switch (call) {
	case SEMOP:
		ret = sys_semop (first, (struct sembuf *)ptr, second);
		break;
	case SEMGET:
		ret = sys_semget (first, second, third);
		break;
	case SEMCTL:
	{
		union semun fourth;

		if (!ptr)
			break;
		if ((ret = access_ok(VERIFY_READ, ptr, sizeof(long)) ? 0 : -EFAULT)
		    || (ret = get_user(fourth.__pad, (void **)ptr)))
			break;
		ret = sys_semctl (first, second, third, fourth);
		break;
	}
	case MSGSND:
		ret = sys_msgsnd (first, (struct msgbuf *) ptr, second, third);
		break;
	case MSGRCV:
		switch (version) {
		case 0: {
			struct ipc_kludge tmp;

			if (!ptr)
				break;
			if ((ret = access_ok(VERIFY_READ, ptr, sizeof(tmp)) ? 0 : -EFAULT)
			    || (ret = copy_from_user(&tmp,
						(struct ipc_kludge *) ptr,
						sizeof (tmp))))
				break;
			ret = sys_msgrcv (first, tmp.msgp, second, tmp.msgtyp,
					  third);
			break;
			}
		default:
			ret = sys_msgrcv (first, (struct msgbuf *) ptr,
					  second, fifth, third);
			break;
		}
		break;
	case MSGGET:
		ret = sys_msgget ((key_t) first, second);
		break;
	case MSGCTL:
		ret = sys_msgctl (first, second, (struct msqid_ds *) ptr);
		break;
	case SHMAT:
		switch (version) {
		default: {
			ulong raddr;

			if ((ret = access_ok(VERIFY_WRITE, (ulong*) third,
					       sizeof(ulong)) ? 0 : -EFAULT))
				break;
			ret = do_shmat (first, (char *) ptr, second, &raddr);
			if (ret)
				break;
			ret = put_user (raddr, (ulong *) third);
			break;
			}
		case 1:	/* iBCS2 emulator entry point */
			if (!segment_eq(get_fs(), get_ds()))
				break;
			ret = do_shmat (first, (char *) ptr, second,
					 (ulong *) third);
			break;
		}
		break;
	case SHMDT: 
		ret = sys_shmdt ((char *)ptr);
		break;
	case SHMGET:
		ret = sys_shmget (first, second, third);
		break;
	case SHMCTL:
		ret = sys_shmctl (first, second, (struct shmid_ds *) ptr);
		break;
	}

	return ret;
}

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
int sys_pipe (int *fildes)
{
	int fd[2];
	int error;

	error = do_pipe (fd);
	if (!error) {
		if (copy_to_user (fildes, fd, 2*sizeof (int)))
			error = -EFAULT;
	}
	return error;
}

static inline unsigned long
do_mmap2 (unsigned long addr, size_t len,
	 unsigned long prot, unsigned long flags,
	 unsigned long fd, unsigned long pgoff)
{
	struct file * file = NULL;
	int ret = -EBADF;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (! (flags & MAP_ANONYMOUS)) {
		if (!(file = fget (fd)))
			goto out;
	}
	
	down_write (&current->mm->mmap_sem);
	ret = do_mmap_pgoff (file, addr, len, prot, flags, pgoff);
	up_write (&current->mm->mmap_sem);
	if (file)
		fput (file);
out:
	return ret;
}

unsigned long sys_mmap2 (unsigned long addr, size_t len,
			unsigned long prot, unsigned long flags,
			unsigned long fd, unsigned long pgoff)
{
	return do_mmap2 (addr, len, prot, flags, fd, pgoff);
}

unsigned long sys_mmap (unsigned long addr, size_t len,
		       unsigned long prot, unsigned long flags,
		       unsigned long fd, off_t offset)
{
	int err = -EINVAL;

	if (offset & ~PAGE_MASK)
		goto out;

	err = do_mmap2 (addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
out:
	return err;
}

/*
 * Do a system call from kernel instead of calling sys_execve so we
 * end up with proper pt_regs.
 */
int kernel_execve(const char *filename, char *const argv[], char *const envp[])
{
	register char *__a __asm__ ("r6") = filename;
	register void *__b __asm__ ("r7") = argv;
	register void *__c __asm__ ("r8") = envp;
	register unsigned long __syscall __asm__ ("r12") = __NR_execve;
	register unsigned long __ret __asm__ ("r10");
	__asm__ __volatile__ ("trap 0"
			: "=r" (__ret), "=r" (__syscall)
			: "1" (__syscall), "r" (__a), "r" (__b), "r" (__c)
			: "r1", "r5", "r11", "r13", "r14",
			  "r15", "r16", "r17", "r18", "r19");
	return __ret;
}
