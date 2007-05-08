/* $Id: sys_sparc.c,v 1.57 2002/02/09 19:49:30 davem Exp $
 * linux/arch/sparc64/kernel/sys_sparc.c
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

#include <asm/uaccess.h>
#include <asm/ipc.h>
#include <asm/utrap.h>
#include <asm/perfctr.h>
#include <asm/a.out.h>
#include <asm/unistd.h>

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

/* Does start,end straddle the VA-space hole?  */
static inline int straddles_64bit_va_hole(unsigned long start, unsigned long end)
{
	unsigned long va_exclude_start, va_exclude_end;

	va_exclude_start = VA_EXCLUDE_START;
	va_exclude_end   = VA_EXCLUDE_END;

	if (likely(start < va_exclude_start && end < va_exclude_start))
		return 0;

	if (likely(start >= va_exclude_end && end >= va_exclude_end))
		return 0;

	return 1;
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

	if (flags & MAP_FIXED) {
		/* Ok, don't mess with it. */
		return get_unmapped_area(NULL, addr, len, pgoff, flags);
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
		addr = get_unmapped_area(NULL, orig_addr, len + (align_goal - PAGE_SIZE), pgoff, flags);
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
		addr = get_unmapped_area(NULL, orig_addr, len, pgoff, flags);

	return addr;
}

/* Essentially the same as PowerPC... */
void arch_pick_mmap_layout(struct mm_struct *mm)
{
	unsigned long random_factor = 0UL;

	if (current->flags & PF_RANDOMIZE) {
		random_factor = get_random_int();
		if (test_thread_flag(TIF_32BIT))
			random_factor &= ((1 * 1024 * 1024) - 1);
		else
			random_factor = ((random_factor << PAGE_SHIFT) &
					 0xffffffffUL);
	}

	/*
	 * Fall back to the standard layout if the personality
	 * bit is set, or if the expected stack growth is unlimited:
	 */
	if (!test_thread_flag(TIF_32BIT) ||
	    (current->personality & ADDR_COMPAT_LAYOUT) ||
	    current->signal->rlim[RLIMIT_STACK].rlim_cur == RLIM_INFINITY ||
	    sysctl_legacy_va_layout) {
		mm->mmap_base = TASK_UNMAPPED_BASE + random_factor;
		mm->get_unmapped_area = arch_get_unmapped_area;
		mm->unmap_area = arch_unmap_area;
	} else {
		/* We know it's 32-bit */
		unsigned long task_size = STACK_TOP32;
		unsigned long gap;

		gap = current->signal->rlim[RLIMIT_STACK].rlim_cur;
		if (gap < 128 * 1024 * 1024)
			gap = 128 * 1024 * 1024;
		if (gap > (task_size / 6 * 5))
			gap = (task_size / 6 * 5);

		mm->mmap_base = PAGE_ALIGN(task_size - gap - random_factor);
		mm->get_unmapped_area = arch_get_unmapped_area_topdown;
		mm->unmap_area = arch_unmap_area_topdown;
	}
}

asmlinkage unsigned long sparc_brk(unsigned long brk)
{
	/* People could try to be nasty and use ta 0x6d in 32bit programs */
	if (test_thread_flag(TIF_32BIT) && brk >= STACK_TOP32)
		return current->mm->brk;

	if (unlikely(straddles_64bit_va_hole(current->mm->brk, brk)))
		return current->mm->brk;

	return sys_brk(brk);
}
                                                                
/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage long sparc_pipe(struct pt_regs *regs)
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

asmlinkage long sys_ipc(unsigned int call, int first, unsigned long second,
			unsigned long third, void __user *ptr, long fifth)
{
	int err;

	/* No need for backward compatibility. We can start fresh... */
	if (call <= SEMCTL) {
		switch (call) {
		case SEMOP:
			err = sys_semtimedop(first, ptr,
					     (unsigned)second, NULL);
			goto out;
		case SEMTIMEDOP:
			err = sys_semtimedop(first, ptr, (unsigned)second,
				(const struct timespec __user *) fifth);
			goto out;
		case SEMGET:
			err = sys_semget(first, (int)second, (int)third);
			goto out;
		case SEMCTL: {
			union semun fourth;
			err = -EINVAL;
			if (!ptr)
				goto out;
			err = -EFAULT;
			if (get_user(fourth.__pad,
				     (void __user * __user *) ptr))
				goto out;
			err = sys_semctl(first, (int)second | IPC_64,
					 (int)third, fourth);
			goto out;
		}
		default:
			err = -ENOSYS;
			goto out;
		};
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
		};
	}
	if (call <= SHMCTL) {
		switch (call) {
		case SHMAT: {
			ulong raddr;
			err = do_shmat(first, ptr, (int)second, &raddr);
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
		};
	} else {
		err = -ENOSYS;
	}
out:
	return err;
}

asmlinkage long sparc64_newuname(struct new_utsname __user *name)
{
	int ret = sys_newuname(name);
	
	if (current->personality == PER_LINUX32 && !ret) {
		ret = (copy_to_user(name->machine, "sparc\0\0", 8)
		       ? -EFAULT : 0);
	}
	return ret;
}

asmlinkage long sparc64_personality(unsigned long personality)
{
	int ret;

	if (current->personality == PER_LINUX32 &&
	    personality == PER_LINUX)
		personality = PER_LINUX32;
	ret = sys_personality(personality);
	if (ret == PER_LINUX32)
		ret = PER_LINUX;

	return ret;
}

int sparc64_mmap_check(unsigned long addr, unsigned long len,
		unsigned long flags)
{
	if (test_thread_flag(TIF_32BIT)) {
		if (len >= STACK_TOP32)
			return -EINVAL;

		if ((flags & MAP_FIXED) && addr > STACK_TOP32 - len)
			return -EINVAL;
	} else {
		if (len >= VA_EXCLUDE_START)
			return -EINVAL;

		if ((flags & MAP_FIXED) && invalid_64bit_range(addr, len))
			return -EINVAL;
	}

	return 0;
}

/* Linux version of mmap */
asmlinkage unsigned long sys_mmap(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long fd,
	unsigned long off)
{
	struct file * file = NULL;
	unsigned long retval = -EBADF;

	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}
	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	len = PAGE_ALIGN(len);

	down_write(&current->mm->mmap_sem);
	retval = do_mmap(file, addr, len, prot, flags, off);
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
out:
	return retval;
}

asmlinkage long sys64_munmap(unsigned long addr, size_t len)
{
	long ret;

	if (invalid_64bit_range(addr, len))
		return -EINVAL;

	down_write(&current->mm->mmap_sem);
	ret = do_munmap(current->mm, addr, len);
	up_write(&current->mm->mmap_sem);
	return ret;
}

extern unsigned long do_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, unsigned long new_addr);
                
asmlinkage unsigned long sys64_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, unsigned long new_addr)
{
	struct vm_area_struct *vma;
	unsigned long ret = -EINVAL;

	if (test_thread_flag(TIF_32BIT))
		goto out;
	if (unlikely(new_len >= VA_EXCLUDE_START))
		goto out;
	if (unlikely(invalid_64bit_range(addr, old_len)))
		goto out;

	down_write(&current->mm->mmap_sem);
	if (flags & MREMAP_FIXED) {
		if (invalid_64bit_range(new_addr, new_len))
			goto out_sem;
	} else if (invalid_64bit_range(addr, new_len)) {
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

		/* MREMAP_FIXED checked above. */
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

asmlinkage long solaris_syscall(struct pt_regs *regs)
{
	static int count;

	regs->tpc = regs->tnpc;
	regs->tnpc += 4;
	if (test_thread_flag(TIF_32BIT)) {
		regs->tpc &= 0xffffffff;
		regs->tnpc &= 0xffffffff;
	}
	if (++count <= 5) {
		printk ("For Solaris binary emulation you need solaris module loaded\n");
		show_regs (regs);
	}
	send_sig(SIGSEGV, current, 1);

	return -ENOSYS;
}

#ifndef CONFIG_SUNOS_EMUL
asmlinkage long sunos_syscall(struct pt_regs *regs)
{
	static int count;

	regs->tpc = regs->tnpc;
	regs->tnpc += 4;
	if (test_thread_flag(TIF_32BIT)) {
		regs->tpc &= 0xffffffff;
		regs->tnpc &= 0xffffffff;
	}
	if (++count <= 20)
		printk ("SunOS binary emulation not compiled in\n");
	force_sig(SIGSEGV, current);

	return -ENOSYS;
}
#endif

asmlinkage long sys_utrap_install(utrap_entry_t type,
				  utrap_handler_t new_p,
				  utrap_handler_t new_d,
				  utrap_handler_t __user *old_p,
				  utrap_handler_t __user *old_d)
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
			long *p = current_thread_info()->utraps;

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

long sparc_memory_ordering(unsigned long model, struct pt_regs *regs)
{
	if (model >= 3)
		return -EINVAL;
	regs->tstate = (regs->tstate & ~TSTATE_MM) | (model << 14);
	return 0;
}

asmlinkage long sys_rt_sigaction(int sig,
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

/* Invoked by rtrap code to update performance counters in
 * user space.
 */
asmlinkage void update_perfctrs(void)
{
	unsigned long pic, tmp;

	read_pic(pic);
	tmp = (current_thread_info()->kernel_cntd0 += (unsigned int)pic);
	__put_user(tmp, current_thread_info()->user_cntd0);
	tmp = (current_thread_info()->kernel_cntd1 += (pic >> 32));
	__put_user(tmp, current_thread_info()->user_cntd1);
	reset_pic();
}

asmlinkage long sys_perfctr(int opcode, unsigned long arg0, unsigned long arg1, unsigned long arg2)
{
	int err = 0;

	switch(opcode) {
	case PERFCTR_ON:
		current_thread_info()->pcr_reg = arg2;
		current_thread_info()->user_cntd0 = (u64 __user *) arg0;
		current_thread_info()->user_cntd1 = (u64 __user *) arg1;
		current_thread_info()->kernel_cntd0 =
			current_thread_info()->kernel_cntd1 = 0;
		write_pcr(arg2);
		reset_pic();
		set_thread_flag(TIF_PERFCTR);
		break;

	case PERFCTR_OFF:
		err = -EINVAL;
		if (test_thread_flag(TIF_PERFCTR)) {
			current_thread_info()->user_cntd0 =
				current_thread_info()->user_cntd1 = NULL;
			current_thread_info()->pcr_reg = 0;
			write_pcr(0);
			clear_thread_flag(TIF_PERFCTR);
			err = 0;
		}
		break;

	case PERFCTR_READ: {
		unsigned long pic, tmp;

		if (!test_thread_flag(TIF_PERFCTR)) {
			err = -EINVAL;
			break;
		}
		read_pic(pic);
		tmp = (current_thread_info()->kernel_cntd0 += (unsigned int)pic);
		err |= __put_user(tmp, current_thread_info()->user_cntd0);
		tmp = (current_thread_info()->kernel_cntd1 += (pic >> 32));
		err |= __put_user(tmp, current_thread_info()->user_cntd1);
		reset_pic();
		break;
	}

	case PERFCTR_CLRPIC:
		if (!test_thread_flag(TIF_PERFCTR)) {
			err = -EINVAL;
			break;
		}
		current_thread_info()->kernel_cntd0 =
			current_thread_info()->kernel_cntd1 = 0;
		reset_pic();
		break;

	case PERFCTR_SETPCR: {
		u64 __user *user_pcr = (u64 __user *)arg0;

		if (!test_thread_flag(TIF_PERFCTR)) {
			err = -EINVAL;
			break;
		}
		err |= __get_user(current_thread_info()->pcr_reg, user_pcr);
		write_pcr(current_thread_info()->pcr_reg);
		current_thread_info()->kernel_cntd0 =
			current_thread_info()->kernel_cntd1 = 0;
		reset_pic();
		break;
	}

	case PERFCTR_GETPCR: {
		u64 __user *user_pcr = (u64 __user *)arg0;

		if (!test_thread_flag(TIF_PERFCTR)) {
			err = -EINVAL;
			break;
		}
		err |= __put_user(current_thread_info()->pcr_reg, user_pcr);
		break;
	}

	default:
		err = -EINVAL;
		break;
	};
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
	asm volatile ("t 0x6d\n\t"
		      "sub %%g0, %%o0, %0\n\t"
		      "movcc %%xcc, %%o0, %0\n\t"
		      : "=r" (__res), "=&r" (__o0)
		      : "1" (__o0), "r" (__o1), "r" (__o2), "r" (__g1)
		      : "cc");
	return __res;
}
