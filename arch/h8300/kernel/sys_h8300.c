/*
 * linux/arch/h8300/kernel/sys_h8300.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the H8/300
 * platform.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/syscalls.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/fs.h>
#include <linux/ipc.h>

#include <asm/setup.h>
#include <asm/uaccess.h>
#include <asm/cachectl.h>
#include <asm/traps.h>
#include <asm/unistd.h>

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

/*
 * Perform the select(nd, in, out, ex, tv) and mmap() system
 * calls. Linux/m68k cloned Linux/i386, which didn't use to be able to
 * handle more than 4 system call parameters, so these system calls
 * used a memory block for parameter passing..
 */

struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

asmlinkage int old_mmap(struct mmap_arg_struct *arg)
{
	struct mmap_arg_struct a;
	int error = -EFAULT;

	if (copy_from_user(&a, arg, sizeof(a)))
		goto out;

	error = -EINVAL;
	if (a.offset & ~PAGE_MASK)
		goto out;

	a.flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	error = do_mmap2(a.addr, a.len, a.prot, a.flags, a.fd, a.offset >> PAGE_SHIFT);
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

struct sel_arg_struct {
	unsigned long n;
	fd_set *inp, *outp, *exp;
	struct timeval *tvp;
};

asmlinkage int old_select(struct sel_arg_struct *arg)
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
asmlinkage int sys_ipc (uint call, int first, int second,
			int third, void *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	if (call <= SEMCTL)
		switch (call) {
		case SEMOP:
			return sys_semop (first, (struct sembuf *)ptr, second);
		case SEMGET:
			return sys_semget (first, second, third);
		case SEMCTL: {
			union semun fourth;
			if (!ptr)
				return -EINVAL;
			if (get_user(fourth.__pad, (void **) ptr))
				return -EFAULT;
			return sys_semctl (first, second, third, fourth);
			}
		default:
			return -EINVAL;
		}
	if (call <= MSGCTL) 
		switch (call) {
		case MSGSND:
			return sys_msgsnd (first, (struct msgbuf *) ptr, 
					  second, third);
		case MSGRCV:
			switch (version) {
			case 0: {
				struct ipc_kludge tmp;
				if (!ptr)
					return -EINVAL;
				if (copy_from_user (&tmp,
						    (struct ipc_kludge *)ptr,
						    sizeof (tmp)))
					return -EFAULT;
				return sys_msgrcv (first, tmp.msgp, second,
						   tmp.msgtyp, third);
				}
			default:
				return sys_msgrcv (first,
						   (struct msgbuf *) ptr,
						   second, fifth, third);
			}
		case MSGGET:
			return sys_msgget ((key_t) first, second);
		case MSGCTL:
			return sys_msgctl (first, second,
					   (struct msqid_ds *) ptr);
		default:
			return -EINVAL;
		}
	if (call <= SHMCTL) 
		switch (call) {
		case SHMAT:
			switch (version) {
			default: {
				ulong raddr;
				ret = do_shmat (first, (char *) ptr,
						 second, &raddr);
				if (ret)
					return ret;
				return put_user (raddr, (ulong *) third);
			}
			}
		case SHMDT: 
			return sys_shmdt ((char *)ptr);
		case SHMGET:
			return sys_shmget (first, second, third);
		case SHMCTL:
			return sys_shmctl (first, second,
					   (struct shmid_ds *) ptr);
		default:
			return -EINVAL;
		}

	return -EINVAL;
}

/* sys_cacheflush -- no support.  */
asmlinkage int
sys_cacheflush (unsigned long addr, int scope, int cache, unsigned long len)
{
	return -EINVAL;
}

asmlinkage int sys_getpagesize(void)
{
	return PAGE_SIZE;
}

#if defined(CONFIG_SYSCALL_PRINT)
asmlinkage void syscall_print(void *dummy,...)
{
	struct pt_regs *regs = (struct pt_regs *) ((unsigned char *)&dummy-4);
	printk("call %06lx:%ld 1:%08lx,2:%08lx,3:%08lx,ret:%08lx\n",
               ((regs->pc)&0xffffff)-2,regs->orig_er0,regs->er1,regs->er2,regs->er3,regs->er0);
}
#endif

/*
 * Do a system call from kernel instead of calling sys_execve so we
 * end up with proper pt_regs.
 */
int kernel_execve(const char *filename, char *const argv[], char *const envp[])
{
	register long res __asm__("er0");
	register char *const *_c __asm__("er3") = envp;
	register char *const *_b __asm__("er2") = argv;
	register const char * _a __asm__("er1") = filename;
	__asm__ __volatile__ ("mov.l %1,er0\n\t"
			"trapa	#0\n\t"
			: "=r" (res)
			: "g" (__NR_execve),
			  "g" (_a),
			  "g" (_b),
			  "g" (_c)
			: "cc", "memory");
	return res;
}


