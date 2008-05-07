/*
 * linux/arch/m32r/kernel/sys_m32r.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/M32R platform.
 *
 * Taken from i386 version.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/syscalls.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/ipc.h>

#include <asm/uaccess.h>
#include <asm/cachectl.h>
#include <asm/cacheflush.h>
#include <asm/syscall.h>
#include <asm/unistd.h>

/*
 * sys_tas() - test-and-set
 */
asmlinkage int sys_tas(int __user *addr)
{
	int oldval;

	if (!access_ok(VERIFY_WRITE, addr, sizeof (int)))
		return -EFAULT;

	/* atomic operation:
	 *   oldval = *addr; *addr = 1;
	 */
	__asm__ __volatile__ (
		DCACHE_CLEAR("%0", "r4", "%1")
		"	.fillinsn\n"
		"1:\n"
		"	lock	%0, @%1	    ->	unlock	%2, @%1\n"
		"2:\n"
		/* NOTE:
		 *   The m32r processor can accept interrupts only
		 *   at the 32-bit instruction boundary.
		 *   So, in the above code, the "unlock" instruction
		 *   can be executed continuously after the "lock"
		 *   instruction execution without any interruptions.
		 */
		".section .fixup,\"ax\"\n"
		"	.balign 4\n"
		"3:	ldi	%0, #%3\n"
		"	seth	r14, #high(2b)\n"
		"	or3	r14, r14, #low(2b)\n"
		"	jmp	r14\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.balign 4\n"
		"	.long 1b,3b\n"
		".previous\n"
		: "=&r" (oldval)
		: "r" (addr), "r" (1), "i"(-EFAULT)
		: "r14", "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		  , "r4"
#endif /* CONFIG_CHIP_M32700_TS1 */
	);

	return oldval;
}

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way Unix traditionally does this, though.
 */
asmlinkage int
sys_pipe(unsigned long r0, unsigned long r1, unsigned long r2,
	unsigned long r3, unsigned long r4, unsigned long r5,
	unsigned long r6, struct pt_regs regs)
{
	int fd[2];
	int error;

	error = do_pipe(fd);
	if (!error) {
		if (copy_to_user((void __user *)r0, fd, 2*sizeof(int))) {
			sys_close(fd[0]);
			sys_close(fd[1]);
			error = -EFAULT;
		}
	}
	return error;
}

asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	int error = -EBADF;
	struct file *file = NULL;

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

	switch (call) {
	case SEMOP:
		return sys_semtimedop(first, (struct sembuf __user *)ptr,
				      second, NULL);
	case SEMTIMEDOP:
		return sys_semtimedop(first, (struct sembuf __user *)ptr,
				      second, (const struct timespec __user *)fifth);
	case SEMGET:
		return sys_semget (first, second, third);
	case SEMCTL: {
		union semun fourth;
		if (!ptr)
			return -EINVAL;
		if (get_user(fourth.__pad, (void __user * __user *) ptr))
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
		return sys_msgctl (first, second,
				   (struct msqid_ds __user *) ptr);
	case SHMAT: {
		ulong raddr;

		if (!access_ok(VERIFY_WRITE, (ulong __user *) third,
				      sizeof(ulong)))
			return -EFAULT;
		ret = do_shmat (first, (char __user *) ptr, second, &raddr);
		if (ret)
			return ret;
		return put_user (raddr, (ulong __user *) third);
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

asmlinkage int sys_uname(struct old_utsname __user * name)
{
	int err;
	if (!name)
		return -EFAULT;
	down_read(&uts_sem);
	err = copy_to_user(name, utsname(), sizeof (*name));
	up_read(&uts_sem);
	return err?-EFAULT:0;
}

asmlinkage int sys_cacheflush(void *addr, int bytes, int cache)
{
	/* This should flush more selectively ...  */
	_flush_cache_all();
	return 0;
}

asmlinkage int sys_cachectl(char *addr, int nbytes, int op)
{
	/* Not implemented yet. */
	return -ENOSYS;
}

/*
 * Do a system call from kernel instead of calling sys_execve so we
 * end up with proper pt_regs.
 */
int kernel_execve(const char *filename, char *const argv[], char *const envp[])
{
	register long __scno __asm__ ("r7") = __NR_execve;
	register long __arg3 __asm__ ("r2") = (long)(envp);
	register long __arg2 __asm__ ("r1") = (long)(argv);
	register long __res __asm__ ("r0") = (long)(filename);
	__asm__ __volatile__ (
		"trap #" SYSCALL_VECTOR "|| nop"
		: "=r" (__res)
		: "r" (__scno), "0" (__res), "r" (__arg2),
			"r" (__arg3)
		: "memory");
	return __res;
}
