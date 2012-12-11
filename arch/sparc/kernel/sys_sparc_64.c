/* linux/arch/sparc64/kernel/sys_sparc.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/sparc
 * platform.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/utsname.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/ipc.h>
#include <linux/personality.h>
#include <linux/random.h>
#include <linux/export.h>

#include <asm/uaccess.h>
#include <asm/utrap.h>
#include <asm/unistd.h>

#include "entry.h"
#include "systbls.h"

/* #define DEBUG_UNIMP_SYSCALL */

asmlinkage unsigned long sys_getpagesize(void)
{
	return PAGE_SIZE;
}

#define VA_EXCLUDE_START (0x0000080000000000UL - (1UL << 32UL))
#define VA_EXCLUDE_END   (0xfffff80000000000UL + (1UL << 32UL))

/* Does addr --> addr+len fall within 4GB of the VA-space hole or
 * overflow past the end of the 64-bit address space?
 */
static inline int invalid_64bit_range(unsigned long addr, unsigned long len)
{
	unsigned long va_exclude_start, va_exclude_end;

	va_exclude_start = VA_EXCLUDE_START;
	va_exclude_end   = VA_EXCLUDE_END;

	if (unlikely(len >= va_exclude_start))
		return 1;

	if (unlikely((addr + len) < addr))
		return 1;

	if (unlikely((addr >= va_exclude_start && addr < va_exclude_end) ||
		     ((addr + len) >= va_exclude_start &&
		      (addr + len) < va_exclude_end)))
		return 1;

	return 0;
}

/* These functions differ from the default implementations in
 * mm/mmap.c in two ways:
 *
 * 1) For file backed MAP_SHARED mmap()'s we D-cache color align,
 *    for fixed such mappings we just validate what the user gave us.
 * 2) For 64-bit tasks we avoid mapping anything within 4GB of
 *    the spitfire/niagara VA-hole.
 */

static inline unsigned long COLOUR_ALIGN(unsigned long addr,
					 unsigned long pgoff)
{
	unsigned long base = (addr+SHMLBA-1)&~(SHMLBA-1);
	unsigned long off = (pgoff<<PAGE_SHIFT) & (SHMLBA-1);

	return base + off;
}

static inline unsigned long COLOUR_ALIGN_DOWN(unsigned long addr,
					      unsigned long pgoff)
{
	unsigned long base = addr & ~(SHMLBA-1);
	unsigned long off = (pgoff<<PAGE_SHIFT) & (SHMLBA-1);

	if (base + off <= addr)
		return base + off;
	return base - off;
}

unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr, unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct * vma;
	unsigned long task_size = TASK_SIZE;
	unsigned long start_addr;
	int do_color_align;

	if (flags & MAP_FIXED) {
		/* We do not accept a shared mapping if it would violate
		 * cache aliasing constraints.
		 */
		if ((flags & MAP_SHARED) &&
		    ((addr - (pgoff << PAGE_SHIFT)) & (SHMLBA - 1)))
			return -EINVAL;
		return addr;
	}

	if (test_thread_flag(TIF_32BIT))
		task_size = STACK_TOP32;
	if (unlikely(len > task_size || len >= VA_EXCLUDE_START))
		return -ENOMEM;

	do_color_align = 0;
	if (filp || (flags & MAP_SHARED))
		do_color_align = 1;

	if (addr) {
		if (do_color_align)
			addr = COLOUR_ALIGN(addr, pgoff);
		else
			addr = PAGE_ALIGN(addr);

		vma = find_vma(mm, addr);
		if (task_size - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}

	if (len > mm->cached_hole_size) {
	        start_addr = addr = mm->free_area_cache;
	} else {
	        start_addr = addr = TASK_UNMAPPED_BASE;
	        mm->cached_hole_size = 0;
	}

	task_size -= len;

full_search:
	if (do_color_align)
		addr = COLOUR_ALIGN(addr, pgoff);
	else
		addr = PAGE_ALIGN(addr);

	for (vma = find_vma(mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (addr < VA_EXCLUDE_START &&
		    (addr + len) >= VA_EXCLUDE_START) {
			addr = VA_EXCLUDE_END;
			vma = find_vma(mm, VA_EXCLUDE_END);
		}
		if (unlikely(task_size < addr)) {
			if (start_addr != TASK_UNMAPPED_BASE) {
				start_addr = addr = TASK_UNMAPPED_BASE;
				mm->cached_hole_size = 0;
				goto full_search;
			}
			return -ENOMEM;
		}
		if (likely(!vma || addr + len <= vma->vm_start)) {
			/*
			 * Remember the place where we stopped the search:
			 */
			mm->free_area_cache = addr + len;
			return addr;
		}
		if (addr + mm->cached_hole_size < vma->vm_start)
		        mm->cached_hole_size = vma->vm_start - addr;

		addr = vma->vm_end;
		if (do_color_align)
			addr = COLOUR_ALIGN(addr, pgoff);
	}
}

unsigned long
arch_get_unmapped_area_topdown(struct file *filp, const unsigned long addr0,
			  const unsigned long len, const unsigned long pgoff,
			  const unsigned long flags)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	unsigned long task_size = STACK_TOP32;
	unsigned long addr = addr0;
	int do_color_align;

	/* This should only ever run for 32-bit processes.  */
	BUG_ON(!test_thread_flag(TIF_32BIT));

	if (flags & MAP_FIXED) {
		/* We do not accept a shared mapping if it would violate
		 * cache aliasing constraints.
		 */
		if ((flags & MAP_SHARED) &&
		    ((addr - (pgoff << PAGE_SHIFT)) & (SHMLBA - 1)))
			return -EINVAL;
		return addr;
	}

	if (unlikely(len > task_size))
		return -ENOMEM;

	do_color_align = 0;
	if (filp || (flags & MAP_SHARED))
		do_color_align = 1;

	/* requesting a specific address */
	if (addr) {
		if (do_color_align)
			addr = COLOUR_ALIGN(addr, pgoff);
		else
			addr = PAGE_ALIGN(addr);

		vma = find_vma(mm, addr);
		if (task_size - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}

	/* check if free_area_cache is useful for us */
	if (len <= mm->cached_hole_size) {
 	        mm->cached_hole_size = 0;
 		mm->free_area_cache = mm->mmap_base;
 	}

	/* either no address requested or can't fit in requested address hole */
	addr = mm->free_area_cache;
	if (do_color_align) {
		unsigned long base = COLOUR_ALIGN_DOWN(addr-len, pgoff);

		addr = base + len;
	}

	/* make sure it can fit in the remaining address space */
	if (likely(addr > len)) {
		vma = find_vma(mm, addr-len);
		if (!vma || addr <= vma->vm_start) {
			/* remember the address as a hint for next time */
			return (mm->free_area_cache = addr-len);
		}
	}

	if (unlikely(mm->mmap_base < len))
		goto bottomup;

	addr = mm->mmap_base-len;
	if (do_color_align)
		addr = COLOUR_ALIGN_DOWN(addr, pgoff);

	do {
		/*
		 * Lookup failure means no vma is above this address,
		 * else if new region fits below vma->vm_start,
		 * return with success:
		 */
		vma = find_vma(mm, addr);
		if (likely(!vma || addr+len <= vma->vm_start)) {
			/* remember the address as a hint for next time */
			return (mm->free_area_cache = addr);
		}

 		/* remember the largest hole we saw so far */
 		if (addr + mm->cached_hole_size < vma->vm_start)
 		        mm->cached_hole_size = vma->vm_start - addr;

		/* try just below the current vma->vm_start */
		addr = vma->vm_start-len;
		if (do_color_align)
			addr = COLOUR_ALIGN_DOWN(addr, pgoff);
	} while (likely(len < vma->vm_start));

bottomup:
	/*
	 * A failed mmap() very likely causes application failure,
	 * so fall back to the bottom-up function here. This scenario
	 * can happen with large stack limits and large mmap()
	 * allocations.
	 */
	mm->cached_hole_size = ~0UL;
  	mm->free_area_cache = TASK_UNMAPPED_BASE;
	addr = arch_get_unmapped_area(filp, addr0, len, pgoff, flags);
	/*
	 * Restore the topdown base:
	 */
	mm->free_area_cache = mm->mmap_base;
	mm->cached_hole_size = ~0UL;

	return addr;
}

/* Try to align mapping such that we align it as much as possible. */
unsigned long get_fb_unmapped_area(struct file *filp, unsigned long orig_addr, unsigned long len, unsigned long pgoff, unsigned long flags)
{
	unsigned long align_goal, addr = -ENOMEM;
	unsigned long (*get_area)(struct file *, unsigned long,
				  unsigned long, unsigned long, unsigned long);

	get_area = current->mm->get_unmapped_area;

	if (flags & MAP_FIXED) {
		/* Ok, don't mess with it. */
		return get_area(NULL, orig_addr, len, pgoff, flags);
	}
	flags &= ~MAP_SHARED;

	align_goal = PAGE_SIZE;
	if (len >= (4UL * 1024 * 1024))
		align_goal = (4UL * 1024 * 1024);
	else if (len >= (512UL * 1024))
		align_goal = (512UL * 1024);
	else if (len >= (64UL * 1024))
		align_goal = (64UL * 1024);

	do {
		addr = get_area(NULL, orig_addr, len + (align_goal - PAGE_SIZE), pgoff, flags);
		if (!(addr & ~PAGE_MASK)) {
			addr = (addr + (align_goal - 1UL)) & ~(align_goal - 1UL);
			break;
		}

		if (align_goal == (4UL * 1024 * 1024))
			align_goal = (512UL * 1024);
		else if (align_goal == (512UL * 1024))
			align_goal = (64UL * 1024);
		else
			align_goal = PAGE_SIZE;
	} while ((addr & ~PAGE_MASK) && align_goal > PAGE_SIZE);

	/* Mapping is smaller than 64K or larger areas could not
	 * be obtained.
	 */
	if (addr & ~PAGE_MASK)
		addr = get_area(NULL, orig_addr, len, pgoff, flags);

	return addr;
}
EXPORT_SYMBOL(get_fb_unmapped_area);

/* Essentially the same as PowerPC.  */
static unsigned long mmap_rnd(void)
{
	unsigned long rnd = 0UL;

	if (current->flags & PF_RANDOMIZE) {
		unsigned long val = get_random_int();
		if (test_thread_flag(TIF_32BIT))
			rnd = (val % (1UL << (23UL-PAGE_SHIFT)));
		else
			rnd = (val % (1UL << (30UL-PAGE_SHIFT)));
	}
	return rnd << PAGE_SHIFT;
}

void arch_pick_mmap_layout(struct mm_struct *mm)
{
	unsigned long random_factor = mmap_rnd();
	unsigned long gap;

	/*
	 * Fall back to the standard layout if the personality
	 * bit is set, or if the expected stack growth is unlimited:
	 */
	gap = rlimit(RLIMIT_STACK);
	if (!test_thread_flag(TIF_32BIT) ||
	    (current->personality & ADDR_COMPAT_LAYOUT) ||
	    gap == RLIM_INFINITY ||
	    sysctl_legacy_va_layout) {
		mm->mmap_base = TASK_UNMAPPED_BASE + random_factor;
		mm->get_unmapped_area = arch_get_unmapped_area;
		mm->unmap_area = arch_unmap_area;
	} else {
		/* We know it's 32-bit */
		unsigned long task_size = STACK_TOP32;

		if (gap < 128 * 1024 * 1024)
			gap = 128 * 1024 * 1024;
		if (gap > (task_size / 6 * 5))
			gap = (task_size / 6 * 5);

		mm->mmap_base = PAGE_ALIGN(task_size - gap - random_factor);
		mm->get_unmapped_area = arch_get_unmapped_area_topdown;
		mm->unmap_area = arch_unmap_area_topdown;
	}
}

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
SYSCALL_DEFINE1(sparc_pipe_real, struct pt_regs *, regs)
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

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */

SYSCALL_DEFINE6(sparc_ipc, unsigned int, call, int, first, unsigned long, second,
		unsigned long, third, void __user *, ptr, long, fifth)
{
	long err;

	/* No need for backward compatibility. We can start fresh... */
	if (call <= SEMCTL) {
		switch (call) {
		case SEMOP:
			err = sys_semtimedop(first, ptr,
					     (unsigned)second, NULL);
			goto out;
		case SEMTIMEDOP:
			err = sys_semtimedop(first, ptr, (unsigned)second,
				(const struct timespec __user *)
					     (unsigned long) fifth);
			goto out;
		case SEMGET:
			err = sys_semget(first, (int)second, (int)third);
			goto out;
		case SEMCTL: {
			err = sys_semctl(first, second,
					 (int)third | IPC_64,
					 (union semun) ptr);
			goto out;
		}
		default:
			err = -ENOSYS;
			goto out;
		}
	}
	if (call <= MSGCTL) {
		switch (call) {
		case MSGSND:
			err = sys_msgsnd(first, ptr, (size_t)second,
					 (int)third);
			goto out;
		case MSGRCV:
			err = sys_msgrcv(first, ptr, (size_t)second, fifth,
					 (int)third);
			goto out;
		case MSGGET:
			err = sys_msgget((key_t)first, (int)second);
			goto out;
		case MSGCTL:
			err = sys_msgctl(first, (int)second | IPC_64, ptr);
			goto out;
		default:
			err = -ENOSYS;
			goto out;
		}
	}
	if (call <= SHMCTL) {
		switch (call) {
		case SHMAT: {
			ulong raddr;
			err = do_shmat(first, ptr, (int)second, &raddr, SHMLBA);
			if (!err) {
				if (put_user(raddr,
					     (ulong __user *) third))
					err = -EFAULT;
			}
			goto out;
		}
		case SHMDT:
			err = sys_shmdt(ptr);
			goto out;
		case SHMGET:
			err = sys_shmget(first, (size_t)second, (int)third);
			goto out;
		case SHMCTL:
			err = sys_shmctl(first, (int)second | IPC_64, ptr);
			goto out;
		default:
			err = -ENOSYS;
			goto out;
		}
	} else {
		err = -ENOSYS;
	}
out:
	return err;
}

SYSCALL_DEFINE1(sparc64_personality, unsigned long, personality)
{
	int ret;

	if (personality(current->personality) == PER_LINUX32 &&
	    personality(personality) == PER_LINUX)
		personality |= PER_LINUX32;
	ret = sys_personality(personality);
	if (personality(ret) == PER_LINUX32)
		ret &= ~PER_LINUX32;

	return ret;
}

int sparc_mmap_check(unsigned long addr, unsigned long len)
{
	if (test_thread_flag(TIF_32BIT)) {
		if (len >= STACK_TOP32)
			return -EINVAL;

		if (addr > STACK_TOP32 - len)
			return -EINVAL;
	} else {
		if (len >= VA_EXCLUDE_START)
			return -EINVAL;

		if (invalid_64bit_range(addr, len))
			return -EINVAL;
	}

	return 0;
}

/* Linux version of mmap */
SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags, unsigned long, fd,
		unsigned long, off)
{
	unsigned long retval = -EINVAL;

	if ((off + PAGE_ALIGN(len)) < off)
		goto out;
	if (off & ~PAGE_MASK)
		goto out;
	retval = sys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
out:
	return retval;
}

SYSCALL_DEFINE2(64_munmap, unsigned long, addr, size_t, len)
{
	if (invalid_64bit_range(addr, len))
		return -EINVAL;

	return vm_munmap(addr, len);
}

extern unsigned long do_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, unsigned long new_addr);
                
SYSCALL_DEFINE5(64_mremap, unsigned long, addr,	unsigned long, old_len,
		unsigned long, new_len, unsigned long, flags,
		unsigned long, new_addr)
{
	if (test_thread_flag(TIF_32BIT))
		return -EINVAL;
	return sys_mremap(addr, old_len, new_len, flags, new_addr);
}

/* we come to here via sys_nis_syscall so it can setup the regs argument */
asmlinkage unsigned long c_sys_nis_syscall(struct pt_regs *regs)
{
	static int count;
	
	/* Don't make the system unusable, if someone goes stuck */
	if (count++ > 5)
		return -ENOSYS;

	printk ("Unimplemented SPARC system call %ld\n",regs->u_regs[1]);
#ifdef DEBUG_UNIMP_SYSCALL	
	show_regs (regs);
#endif

	return -ENOSYS;
}

/* #define DEBUG_SPARC_BREAKPOINT */

asmlinkage void sparc_breakpoint(struct pt_regs *regs)
{
	siginfo_t info;

	if (test_thread_flag(TIF_32BIT)) {
		regs->tpc &= 0xffffffff;
		regs->tnpc &= 0xffffffff;
	}
#ifdef DEBUG_SPARC_BREAKPOINT
        printk ("TRAP: Entering kernel PC=%lx, nPC=%lx\n", regs->tpc, regs->tnpc);
#endif
	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code = TRAP_BRKPT;
	info.si_addr = (void __user *)regs->tpc;
	info.si_trapno = 0;
	force_sig_info(SIGTRAP, &info, current);
#ifdef DEBUG_SPARC_BREAKPOINT
	printk ("TRAP: Returning to space: PC=%lx nPC=%lx\n", regs->tpc, regs->tnpc);
#endif
}

extern void check_pending(int signum);

SYSCALL_DEFINE2(getdomainname, char __user *, name, int, len)
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

SYSCALL_DEFINE5(utrap_install, utrap_entry_t, type,
		utrap_handler_t, new_p, utrap_handler_t, new_d,
		utrap_handler_t __user *, old_p,
		utrap_handler_t __user *, old_d)
{
	if (type < UT_INSTRUCTION_EXCEPTION || type > UT_TRAP_INSTRUCTION_31)
		return -EINVAL;
	if (new_p == (utrap_handler_t)(long)UTH_NOCHANGE) {
		if (old_p) {
			if (!current_thread_info()->utraps) {
				if (put_user(NULL, old_p))
					return -EFAULT;
			} else {
				if (put_user((utrap_handler_t)(current_thread_info()->utraps[type]), old_p))
					return -EFAULT;
			}
		}
		if (old_d) {
			if (put_user(NULL, old_d))
				return -EFAULT;
		}
		return 0;
	}
	if (!current_thread_info()->utraps) {
		current_thread_info()->utraps =
			kzalloc((UT_TRAP_INSTRUCTION_31+1)*sizeof(long), GFP_KERNEL);
		if (!current_thread_info()->utraps)
			return -ENOMEM;
		current_thread_info()->utraps[0] = 1;
	} else {
		if ((utrap_handler_t)current_thread_info()->utraps[type] != new_p &&
		    current_thread_info()->utraps[0] > 1) {
			unsigned long *p = current_thread_info()->utraps;

			current_thread_info()->utraps =
				kmalloc((UT_TRAP_INSTRUCTION_31+1)*sizeof(long),
					GFP_KERNEL);
			if (!current_thread_info()->utraps) {
				current_thread_info()->utraps = p;
				return -ENOMEM;
			}
			p[0]--;
			current_thread_info()->utraps[0] = 1;
			memcpy(current_thread_info()->utraps+1, p+1,
			       UT_TRAP_INSTRUCTION_31*sizeof(long));
		}
	}
	if (old_p) {
		if (put_user((utrap_handler_t)(current_thread_info()->utraps[type]), old_p))
			return -EFAULT;
	}
	if (old_d) {
		if (put_user(NULL, old_d))
			return -EFAULT;
	}
	current_thread_info()->utraps[type] = (long)new_p;

	return 0;
}

asmlinkage long sparc_memory_ordering(unsigned long model,
				      struct pt_regs *regs)
{
	if (model >= 3)
		return -EINVAL;
	regs->tstate = (regs->tstate & ~TSTATE_MM) | (model << 14);
	return 0;
}

SYSCALL_DEFINE5(rt_sigaction, int, sig, const struct sigaction __user *, act,
		struct sigaction __user *, oact, void __user *, restorer,
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

/*
 * Do a system call from kernel instead of calling sys_execve so we
 * end up with proper pt_regs.
 */
int kernel_execve(const char *filename,
		  const char *const argv[],
		  const char *const envp[])
{
	long __res;
	register long __g1 __asm__ ("g1") = __NR_execve;
	register long __o0 __asm__ ("o0") = (long)(filename);
	register long __o1 __asm__ ("o1") = (long)(argv);
	register long __o2 __asm__ ("o2") = (long)(envp);
	asm volatile ("t 0x6d\n\t"
		      "sub %%g0, %%o0, %0\n\t"
		      "movcc %%xcc, %%o0, %0\n\t"
		      : "=r" (__res), "=&r" (__o0)
		      : "1" (__o0), "r" (__o1), "r" (__o2), "r" (__g1)
		      : "cc");
	return __res;
}

asmlinkage long sys_kern_features(void)
{
	return KERN_FEATURE_MIXED_MODE_STACK;
}
