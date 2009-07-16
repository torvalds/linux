/*
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2007 John Williams <john.williams@petalogix.com>
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 *	Yasushi SHOJI <yashi@atmark-techno.com>
 *	Tetsuya OHKAWA <tetsuya@atmark-techno.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
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
#include <linux/module.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>

#include <asm/syscalls.h>
/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly. This will be remove with new toolchain.
 */
asmlinkage long
sys_ipc(uint call, int first, int second, int third, void *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	ret = -EINVAL;
	switch (call) {
	case SEMOP:
		ret = sys_semop(first, (struct sembuf *)ptr, second);
		break;
	case SEMGET:
		ret = sys_semget(first, second, third);
		break;
	case SEMCTL:
	{
		union semun fourth;

		if (!ptr)
			break;
		ret = (access_ok(VERIFY_READ, ptr, sizeof(long)) ? 0 : -EFAULT)
				|| (get_user(fourth.__pad, (void **)ptr)) ;
		if (ret)
			break;
		ret = sys_semctl(first, second, third, fourth);
		break;
	}
	case MSGSND:
		ret = sys_msgsnd(first, (struct msgbuf *) ptr, second, third);
		break;
	case MSGRCV:
		switch (version) {
		case 0: {
			struct ipc_kludge tmp;

			if (!ptr)
				break;
			ret = (access_ok(VERIFY_READ, ptr, sizeof(tmp))
				? 0 : -EFAULT) || copy_from_user(&tmp,
				(struct ipc_kludge *) ptr, sizeof(tmp));
			if (ret)
				break;
			ret = sys_msgrcv(first, tmp.msgp, second, tmp.msgtyp,
					third);
			break;
			}
		default:
			ret = sys_msgrcv(first, (struct msgbuf *) ptr,
					second, fifth, third);
			break;
		}
		break;
	case MSGGET:
		ret = sys_msgget((key_t) first, second);
		break;
	case MSGCTL:
		ret = sys_msgctl(first, second, (struct msqid_ds *) ptr);
		break;
	case SHMAT:
		switch (version) {
		default: {
			ulong raddr;
			ret = access_ok(VERIFY_WRITE, (ulong *) third,
					sizeof(ulong)) ? 0 : -EFAULT;
			if (ret)
				break;
			ret = do_shmat(first, (char *) ptr, second, &raddr);
			if (ret)
				break;
			ret = put_user(raddr, (ulong *) third);
			break;
			}
		case 1:	/* iBCS2 emulator entry point */
			if (!segment_eq(get_fs(), get_ds()))
				break;
			ret = do_shmat(first, (char *) ptr, second,
					(ulong *) third);
			break;
		}
		break;
	case SHMDT:
		ret = sys_shmdt((char *)ptr);
		break;
	case SHMGET:
		ret = sys_shmget(first, second, third);
		break;
	case SHMCTL:
		ret = sys_shmctl(first, second, (struct shmid_ds *) ptr);
		break;
	}
	return ret;
}

asmlinkage long microblaze_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->r1,
						regs, 0, NULL, NULL);
}

asmlinkage long microblaze_clone(int flags, unsigned long stack, struct pt_regs *regs)
{
	if (!stack)
		stack = regs->r1;
	return do_fork(flags, stack, regs, 0, NULL, NULL);
}

asmlinkage long microblaze_execve(char __user *filenamei, char __user *__user *argv,
			char __user *__user *envp, struct pt_regs *regs)
{
	int error;
	char *filename;

	filename = getname(filenamei);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
out:
	return error;
}

asmlinkage long
sys_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	struct file *file = NULL;
	int ret = -EBADF;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file) {
			printk(KERN_INFO "no fd in mmap\r\n");
			goto out;
		}
	}

	down_write(&current->mm->mmap_sem);
	ret = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);
	if (file)
		fput(file);
out:
	return ret;
}

asmlinkage long sys_mmap(unsigned long addr, unsigned long len,
			unsigned long prot, unsigned long flags,
			unsigned long fd, off_t pgoff)
{
	int err = -EINVAL;

	if (pgoff & ~PAGE_MASK) {
		printk(KERN_INFO "no pagemask in mmap\r\n");
		goto out;
	}

	err = sys_mmap2(addr, len, prot, flags, fd, pgoff >> PAGE_SHIFT);
out:
	return err;
}

/*
 * Do a system call from kernel instead of calling sys_execve so we
 * end up with proper pt_regs.
 */
int kernel_execve(const char *filename, char *const argv[], char *const envp[])
{
	register const char *__a __asm__("r5") = filename;
	register const void *__b __asm__("r6") = argv;
	register const void *__c __asm__("r7") = envp;
	register unsigned long __syscall __asm__("r12") = __NR_execve;
	register unsigned long __ret __asm__("r3");
	__asm__ __volatile__ ("brki r14, 0x8"
			: "=r" (__ret), "=r" (__syscall)
			: "1" (__syscall), "r" (__a), "r" (__b), "r" (__c)
			: "r4", "r8", "r9",
			"r10", "r11", "r14", "cc", "memory");
	return __ret;
}
