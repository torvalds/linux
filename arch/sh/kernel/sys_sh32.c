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
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/ipc.h>
#include <asm/cacheflush.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/syscalls.h>

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way Unix traditionally does this, though.
 */
asmlinkage int sys_pipe(unsigned long r4, unsigned long r5,
	unsigned long r6, unsigned long r7,
	struct pt_regs __regs)
{
	struct pt_regs *regs = RELOC_HIDE(&__regs, 0);
	int fd[2];
	int error;

	error = do_pipe_flags(fd, 0);
	if (!error) {
		regs->regs[1] = fd[1];
		return fd[0];
	}
	return error;
}

asmlinkage ssize_t sys_pread_wrapper(unsigned int fd, char __user *buf,
			     size_t count, long dummy, loff_t pos)
{
	return sys_pread64(fd, buf, count, pos);
}

asmlinkage ssize_t sys_pwrite_wrapper(unsigned int fd, const char __user *buf,
			      size_t count, long dummy, loff_t pos)
{
	return sys_pwrite64(fd, buf, count, pos);
}

asmlinkage int sys_fadvise64_64_wrapper(int fd, u32 offset0, u32 offset1,
				u32 len0, u32 len1, int advice)
{
#ifdef  __LITTLE_ENDIAN__
	return sys_fadvise64_64(fd, (u64)offset1 << 32 | offset0,
				(u64)len1 << 32 | len0,	advice);
#else
	return sys_fadvise64_64(fd, (u64)offset0 << 32 | offset1,
				(u64)len0 << 32 | len1,	advice);
#endif
}

#if defined(CONFIG_CPU_SH2) || defined(CONFIG_CPU_SH2A)
#define SYSCALL_ARG3	"trapa #0x23"
#else
#define SYSCALL_ARG3	"trapa #0x13"
#endif

/*
 * Do a system call from kernel instead of calling sys_execve so we
 * end up with proper pt_regs.
 */
int kernel_execve(const char *filename, char *const argv[], char *const envp[])
{
	register long __sc0 __asm__ ("r3") = __NR_execve;
	register long __sc4 __asm__ ("r4") = (long) filename;
	register long __sc5 __asm__ ("r5") = (long) argv;
	register long __sc6 __asm__ ("r6") = (long) envp;
	__asm__ __volatile__ (SYSCALL_ARG3 : "=z" (__sc0)
			: "0" (__sc0), "r" (__sc4), "r" (__sc5), "r" (__sc6)
			: "memory");
	return __sc0;
}
