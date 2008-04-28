/* linux/arch/sparc/kernel/sys_sparc.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/sparc
 * platform.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/sched.h>
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
#include <linux/smp_lock.h>
#include <linux/ipc.h>

#include <asm/uaccess.h>
#include <asm/unistd.h>

/* #define DEBUG_UNIMP_SYSCALL */

/* XXX Make this per-binary type, this way we can detect the type of
 * XXX a binary.  Every Sparc executable calls this very early on.
 */
asmlinkage unsigned long sys_getpagesize(void)
{
	return PAGE_SIZE; /* Possibly older binaries want 8192 on sun4's? */
}

#define COLOUR_ALIGN(addr)      (((addr)+SHMLBA-1)&~(SHMLBA-1))

unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr, unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct vm_area_struct * vmm;

	if (flags & MAP_FIXED) {
		/* We do not accept a shared mapping if it would violate
		 * cache aliasing constraints.
		 */
		if ((flags & MAP_SHARED) && (addr & (SHMLBA - 1)))
			return -EINVAL;
		return addr;
	}

	/* See asm-sparc/uaccess.h */
	if (len > TASK_SIZE - PAGE_SIZE)
		return -ENOMEM;
	if (ARCH_SUN4C_SUN4 && len > 0x20000000)
		return -ENOMEM;
	if (!addr)
		addr = TASK_UNMAPPED_BASE;

	if (flags & MAP_SHARED)
		addr = COLOUR_ALIGN(addr);
	else
		addr = PAGE_ALIGN(addr);

	for (vmm = find_vma(current->mm, addr); ; vmm = vmm->vm_next) {
		/* At this point:  (!vmm || addr < vmm->vm_end). */
		if (ARCH_SUN4C_SUN4 && addr < 0xe0000000 && 0x20000000 - len < addr) {
			addr = PAGE_OFFSET;
			vmm = find_vma(current->mm, PAGE_OFFSET);
		}
		if (TASK_SIZE - PAGE_SIZE - len < addr)
			return -ENOMEM;
		if (!vmm || addr + len <= vmm->vm_start)
			return addr;
		addr = vmm->vm_end;
		if (flags & MAP_SHARED)
			addr = COLOUR_ALIGN(addr);
	}
}

asmlinkage unsigned long sparc_brk(unsigned long brk)
{
	if(ARCH_SUN4C_SUN4) {
		if ((brk & 0xe0000000) != (current->mm->brk & 0xe0000000))
			return current->mm->brk;
	}
	return sys_brk(brk);
}

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage int sparc_pipe(struct pt_regs *regs)
{
	int fd[2];
	int error;

	error = do_pipe(fd);
	if (error)
		goto out;
	regs->u_regs[UREG_I1] = fd[1];
	error = fd[0];
out:
	return error;
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */

asmlinkage int sys_ipc (uint call, int first, int second, int third, void __user *ptr, long fifth)
{
	int version, err;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	if (call <= SEMCTL)
		switch (call) {
		case SEMOP:
			err = sys_semtimedop (first, (struct sembuf __user *)ptr, second, NULL);
			goto out;
		case SEMTIMEDOP:
			err = sys_semtimedop (first, (struct sembuf __user *)ptr, second, (const struct timespec __user *) fifth);
			goto out;
		case SEMGET:
			err = sys_semget (first, second, third);
			goto out;
		case SEMCTL: {
			union semun fourth;
			err = -EINVAL;
			if (!ptr)
				goto out;
			err = -EFAULT;
			if (get_user(fourth.__pad,
				     (void __user * __user *)ptr))
				goto out;
			err = sys_semctl (first, second, third, fourth);
			goto out;
			}
		default:
			err = -ENOSYS;
			goto out;
		}
	if (call <= MSGCTL) 
		switch (call) {
		case MSGSND:
			err = sys_msgsnd (first, (struct msgbuf __user *) ptr, 
					  second, third);
			goto out;
		case MSGRCV:
			switch (version) {
			case 0: {
				struct ipc_kludge tmp;
				err = -EINVAL;
				if (!ptr)
					goto out;
				err = -EFAULT;
				if (copy_from_user(&tmp, (struct ipc_kludge __user *) ptr, sizeof (tmp)))
					goto out;
				err = sys_msgrcv (first, tmp.msgp, second, tmp.msgtyp, third);
				goto out;
				}
			case 1: default:
				err = sys_msgrcv (first,
						  (struct msgbuf __user *) ptr,
						  second, fifth, third);
				goto out;
			}
		case MSGGET:
			err = sys_msgget ((key_t) first, second);
			goto out;
		case MSGCTL:
			err = sys_msgctl (first, second, (struct msqid_ds __user *) ptr);
			goto out;
		default:
			err = -ENOSYS;
			goto out;
		}
	if (call <= SHMCTL) 
		switch (call) {
		case SHMAT:
			switch (version) {
			case 0: default: {
				ulong raddr;
				err = do_shmat (first, (char __user *) ptr, second, &raddr);
				if (err)
					goto out;
				err = -EFAULT;
				if (put_user (raddr, (ulong __user *) third))
					goto out;
				err = 0;
				goto out;
				}
			case 1:	/* iBCS2 emulator entry point */
				err = -EINVAL;
				goto out;
			}
		case SHMDT: 
			err = sys_shmdt ((char __user *)ptr);
			goto out;
		case SHMGET:
			err = sys_shmget (first, second, third);
			goto out;
		case SHMCTL:
			err = sys_shmctl (first, second, (struct shmid_ds __user *) ptr);
			goto out;
		default:
			err = -ENOSYS;
			goto out;
		}
	else
		err = -ENOSYS;
out:
	return err;
}

int sparc_mmap_check(unsigned long addr, unsigned long len, unsigned long flags)
{
	if (ARCH_SUN4C_SUN4 &&
	    (len > 0x20000000 ||
	     ((flags & MAP_FIXED) &&
	      addr < 0xe0000000 && addr + len > 0x20000000)))
		return -EINVAL;

	/* See asm-sparc/uaccess.h */
	if (len > TASK_SIZE - PAGE_SIZE || addr + len > TASK_SIZE - PAGE_SIZE)
		return -EINVAL;

	return 0;
}

/* Linux version of mmap */
static unsigned long do_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long fd,
	unsigned long pgoff)
{
	struct file * file = NULL;
	unsigned long retval = -EBADF;

	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	len = PAGE_ALIGN(len);
	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	down_write(&current->mm->mmap_sem);
	retval = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
out:
	return retval;
}

asmlinkage unsigned long sys_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long fd,
	unsigned long pgoff)
{
	/* Make sure the shift for mmap2 is constant (12), no matter what PAGE_SIZE
	   we have. */
	return do_mmap2(addr, len, prot, flags, fd, pgoff >> (PAGE_SHIFT - 12));
}

asmlinkage unsigned long sys_mmap(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long fd,
	unsigned long off)
{
	return do_mmap2(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
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

extern unsigned long do_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, unsigned long new_addr);
                
asmlinkage unsigned long sparc_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, unsigned long new_addr)
{
	struct vm_area_struct *vma;
	unsigned long ret = -EINVAL;
	if (ARCH_SUN4C_SUN4) {
		if (old_len > 0x20000000 || new_len > 0x20000000)
			goto out;
		if (addr < 0xe0000000 && addr + old_len > 0x20000000)
			goto out;
	}
	if (old_len > TASK_SIZE - PAGE_SIZE ||
	    new_len > TASK_SIZE - PAGE_SIZE)
		goto out;
	down_write(&current->mm->mmap_sem);
	if (flags & MREMAP_FIXED) {
		if (ARCH_SUN4C_SUN4 &&
		    new_addr < 0xe0000000 &&
		    new_addr + new_len > 0x20000000)
			goto out_sem;
		if (new_addr + new_len > TASK_SIZE - PAGE_SIZE)
			goto out_sem;
	} else if ((ARCH_SUN4C_SUN4 && addr < 0xe0000000 &&
		    addr + new_len > 0x20000000) ||
		   addr + new_len > TASK_SIZE - PAGE_SIZE) {
		unsigned long map_flags = 0;
		struct file *file = NULL;

		ret = -ENOMEM;
		if (!(flags & MREMAP_MAYMOVE))
			goto out_sem;

		vma = find_vma(current->mm, addr);
		if (vma) {
			if (vma->vm_flags & VM_SHARED)
				map_flags |= MAP_SHARED;
			file = vma->vm_file;
		}

		new_addr = get_unmapped_area(file, addr, new_len,
				     vma ? vma->vm_pgoff : 0,
				     map_flags);
		ret = new_addr;
		if (new_addr & ~PAGE_MASK)
			goto out_sem;
		flags |= MREMAP_FIXED;
	}
	ret = do_mremap(addr, old_len, new_len, flags, new_addr);
out_sem:
	up_write(&current->mm->mmap_sem);
out:
	return ret;       
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

	lock_kernel();
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
	unlock_kernel();
}

asmlinkage int
sparc_sigaction (int sig, const struct old_sigaction __user *act,
		 struct old_sigaction __user *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	WARN_ON_ONCE(sig >= 0);
	sig = -sig;

	if (act) {
		unsigned long mask;

		if (!access_ok(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
		new_ka.ka_restorer = NULL;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		/* In the clone() case we could copy half consistent
		 * state to the user, however this could sleep and
		 * deadlock us if we held the signal lock on SMP.  So for
		 * now I take the easy way out and do no locking.
		 */
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

asmlinkage long
sys_rt_sigaction(int sig,
		 const struct sigaction __user *act,
		 struct sigaction __user *oact,
		 void __user *restorer,
		 size_t sigsetsize)
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

asmlinkage int sys_getdomainname(char __user *name, int len)
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

/*
 * Do a system call from kernel instead of calling sys_execve so we
 * end up with proper pt_regs.
 */
int kernel_execve(const char *filename, char *const argv[], char *const envp[])
{
	long __res;
	register long __g1 __asm__ ("g1") = __NR_execve;
	register long __o0 __asm__ ("o0") = (long)(filename);
	register long __o1 __asm__ ("o1") = (long)(argv);
	register long __o2 __asm__ ("o2") = (long)(envp);
	asm volatile ("t 0x10\n\t"
		      "bcc 1f\n\t"
		      "mov %%o0, %0\n\t"
		      "sub %%g0, %%o0, %0\n\t"
		      "1:\n\t"
		      : "=r" (__res), "=&r" (__o0)
		      : "1" (__o0), "r" (__o1), "r" (__o2), "r" (__g1)
		      : "cc");
	return __res;
}
