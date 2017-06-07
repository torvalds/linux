/* linux/arch/sparc/kernel/sys_sparc.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/sparc
 * platform.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/sched/debug.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/syscalls.h>
#include <linux/mman.h>
#include <linux/utsname.h>
#include <linux/smp.h>
#include <linux/ipc.h>

#include <linux/uaccess.h>
#include <asm/unistd.h>

#include "systbls.h"

/* #define DEBUG_UNIMP_SYSCALL */

/* XXX Make this per-binary type, this way we can detect the type of
 * XXX a binary.  Every Sparc executable calls this very early on.
 */
asmlinkage unsigned long sys_getpagesize(void)
{
	return PAGE_SIZE; /* Possibly older binaries want 8192 on sun4's? */
}

unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr, unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct vm_unmapped_area_info info;

	if (flags & MAP_FIXED) {
		/* We do not accept a shared mapping if it would violate
		 * cache aliasing constraints.
		 */
		if ((flags & MAP_SHARED) &&
		    ((addr - (pgoff << PAGE_SHIFT)) & (SHMLBA - 1)))
			return -EINVAL;
		return addr;
	}

	/* See asm-sparc/uaccess.h */
	if (len > TASK_SIZE - PAGE_SIZE)
		return -ENOMEM;
	if (!addr)
		addr = TASK_UNMAPPED_BASE;

	info.flags = 0;
	info.length = len;
	info.low_limit = addr;
	info.high_limit = TASK_SIZE;
	info.align_mask = (flags & MAP_SHARED) ?
		(PAGE_MASK & (SHMLBA - 1)) : 0;
	info.align_offset = pgoff << PAGE_SHIFT;
	return vm_unmapped_area(&info);
}

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage long sparc_pipe(struct pt_regs *regs)
{
	int fd[2];
	int error;

	error = do_pipe_flags(fd, 0);
	if (error)
		goto out;
	regs->u_regs[UREG_I1] = fd[1];
	error = fd[0];
out:
	return error;
}

int sparc_mmap_check(unsigned long addr, unsigned long len)
{
	/* See asm-sparc/uaccess.h */
	if (len > TASK_SIZE - PAGE_SIZE || addr + len > TASK_SIZE - PAGE_SIZE)
		return -EINVAL;

	return 0;
}

/* Linux version of mmap */

asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long fd,
	unsigned long pgoff)
{
	/* Make sure the shift for mmap2 is constant (12), no matter what PAGE_SIZE
	   we have. */
	return sys_mmap_pgoff(addr, len, prot, flags, fd,
			      pgoff >> (PAGE_SHIFT - 12));
}

asmlinkage long sys_mmap(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long fd,
	unsigned long off)
{
	/* no alignment check? */
	return sys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
}

long sparc_remap_file_pages(unsigned long start, unsigned long size,
			   unsigned long prot, unsigned long pgoff,
			   unsigned long flags)
{
	/* This works on an existing mmap so we don't need to validate
	 * the range as that was done at the original mmap call.
	 */
	return sys_remap_file_pages(start, size, prot,
				    (pgoff >> (PAGE_SHIFT - 12)), flags);
}

/* we come to here via sys_nis_syscall so it can setup the regs argument */
asmlinkage unsigned long
c_sys_nis_syscall (struct pt_regs *regs)
{
	static int count = 0;

	if (count++ > 5)
		return -ENOSYS;
	printk ("%s[%d]: Unimplemented SPARC system call %d\n",
		current->comm, task_pid_nr(current), (int)regs->u_regs[1]);
#ifdef DEBUG_UNIMP_SYSCALL	
	show_regs (regs);
#endif
	return -ENOSYS;
}

/* #define DEBUG_SPARC_BREAKPOINT */

asmlinkage void
sparc_breakpoint (struct pt_regs *regs)
{
	siginfo_t info;

#ifdef DEBUG_SPARC_BREAKPOINT
        printk ("TRAP: Entering kernel PC=%x, nPC=%x\n", regs->pc, regs->npc);
#endif
	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code = TRAP_BRKPT;
	info.si_addr = (void __user *)regs->pc;
	info.si_trapno = 0;
	force_sig_info(SIGTRAP, &info, current);

#ifdef DEBUG_SPARC_BREAKPOINT
	printk ("TRAP: Returning to space: PC=%x nPC=%x\n", regs->pc, regs->npc);
#endif
}

SYSCALL_DEFINE3(sparc_sigaction, int, sig,
		struct old_sigaction __user *,act,
		struct old_sigaction __user *,oact)
{
	WARN_ON_ONCE(sig >= 0);
	return sys_sigaction(-sig, act, oact);
}

SYSCALL_DEFINE5(rt_sigaction, int, sig,
		 const struct sigaction __user *, act,
		 struct sigaction __user *, oact,
		 void __user *, restorer,
		 size_t, sigsetsize)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (act) {
		new_ka.ka_restorer = restorer;
		if (copy_from_user(&new_ka.sa, act, sizeof(*act)))
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (copy_to_user(oact, &old_ka.sa, sizeof(*oact)))
			return -EFAULT;
	}

	return ret;
}

asmlinkage long sys_getdomainname(char __user *name, int len)
{
 	int nlen, err;
 	
	if (len < 0)
		return -EINVAL;

 	down_read(&uts_sem);
 	
	nlen = strlen(utsname()->domainname) + 1;
	err = -EINVAL;
	if (nlen > len)
		goto out;

	err = -EFAULT;
	if (!copy_to_user(name, utsname()->domainname, nlen))
		err = 0;

out:
	up_read(&uts_sem);
	return err;
}
