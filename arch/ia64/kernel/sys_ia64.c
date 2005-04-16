/*
 * This file contains various system calls that have different calling
 * conventions on different platforms.
 *
 * Copyright (C) 1999-2000, 2002-2003, 2005 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/shm.h>
#include <linux/file.h>		/* doh, must come after sched.h... */
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/syscalls.h>
#include <linux/highuid.h>
#include <linux/hugetlb.h>

#include <asm/shmparam.h>
#include <asm/uaccess.h>

unsigned long
arch_get_unmapped_area (struct file *filp, unsigned long addr, unsigned long len,
			unsigned long pgoff, unsigned long flags)
{
	long map_shared = (flags & MAP_SHARED);
	unsigned long start_addr, align_mask = PAGE_SIZE - 1;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	if (len > RGN_MAP_LIMIT)
		return -ENOMEM;

#ifdef CONFIG_HUGETLB_PAGE
	if (REGION_NUMBER(addr) == REGION_HPAGE)
		addr = 0;
#endif
	if (!addr)
		addr = mm->free_area_cache;

	if (map_shared && (TASK_SIZE > 0xfffffffful))
		/*
		 * For 64-bit tasks, align shared segments to 1MB to avoid potential
		 * performance penalty due to virtual aliasing (see ASDM).  For 32-bit
		 * tasks, we prefer to avoid exhausting the address space too quickly by
		 * limiting alignment to a single page.
		 */
		align_mask = SHMLBA - 1;

  full_search:
	start_addr = addr = (addr + align_mask) & ~align_mask;

	for (vma = find_vma(mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (TASK_SIZE - len < addr || RGN_MAP_LIMIT - len < REGION_OFFSET(addr)) {
			if (start_addr != TASK_UNMAPPED_BASE) {
				/* Start a new search --- just in case we missed some holes.  */
				addr = TASK_UNMAPPED_BASE;
				goto full_search;
			}
			return -ENOMEM;
		}
		if (!vma || addr + len <= vma->vm_start) {
			/* Remember the address where we stopped this search:  */
			mm->free_area_cache = addr + len;
			return addr;
		}
		addr = (vma->vm_end + align_mask) & ~align_mask;
	}
}

asmlinkage long
ia64_getpriority (int which, int who)
{
	long prio;

	prio = sys_getpriority(which, who);
	if (prio >= 0) {
		force_successful_syscall_return();
		prio = 20 - prio;
	}
	return prio;
}

/* XXX obsolete, but leave it here until the old libc is gone... */
asmlinkage unsigned long
sys_getpagesize (void)
{
	return PAGE_SIZE;
}

asmlinkage unsigned long
ia64_shmat (int shmid, void __user *shmaddr, int shmflg)
{
	unsigned long raddr;
	int retval;

	retval = do_shmat(shmid, shmaddr, shmflg, &raddr);
	if (retval < 0)
		return retval;

	force_successful_syscall_return();
	return raddr;
}

asmlinkage unsigned long
ia64_brk (unsigned long brk)
{
	unsigned long rlim, retval, newbrk, oldbrk;
	struct mm_struct *mm = current->mm;

	/*
	 * Most of this replicates the code in sys_brk() except for an additional safety
	 * check and the clearing of r8.  However, we can't call sys_brk() because we need
	 * to acquire the mmap_sem before we can do the test...
	 */
	down_write(&mm->mmap_sem);

	if (brk < mm->end_code)
		goto out;
	newbrk = PAGE_ALIGN(brk);
	oldbrk = PAGE_ALIGN(mm->brk);
	if (oldbrk == newbrk)
		goto set_brk;

	/* Always allow shrinking brk. */
	if (brk <= mm->brk) {
		if (!do_munmap(mm, newbrk, oldbrk-newbrk))
			goto set_brk;
		goto out;
	}

	/* Check against unimplemented/unmapped addresses: */
	if ((newbrk - oldbrk) > RGN_MAP_LIMIT || REGION_OFFSET(newbrk) > RGN_MAP_LIMIT)
		goto out;

	/* Check against rlimit.. */
	rlim = current->signal->rlim[RLIMIT_DATA].rlim_cur;
	if (rlim < RLIM_INFINITY && brk - mm->start_data > rlim)
		goto out;

	/* Check against existing mmap mappings. */
	if (find_vma_intersection(mm, oldbrk, newbrk+PAGE_SIZE))
		goto out;

	/* Ok, looks good - let it rip. */
	if (do_brk(oldbrk, newbrk-oldbrk) != oldbrk)
		goto out;
set_brk:
	mm->brk = brk;
out:
	retval = mm->brk;
	up_write(&mm->mmap_sem);
	force_successful_syscall_return();
	return retval;
}

/*
 * On IA-64, we return the two file descriptors in ret0 and ret1 (r8
 * and r9) as this is faster than doing a copy_to_user().
 */
asmlinkage long
sys_pipe (void)
{
	struct pt_regs *regs = ia64_task_regs(current);
	int fd[2];
	int retval;

	retval = do_pipe(fd);
	if (retval)
		goto out;
	retval = fd[0];
	regs->r9 = fd[1];
  out:
	return retval;
}

static inline unsigned long
do_mmap2 (unsigned long addr, unsigned long len, int prot, int flags, int fd, unsigned long pgoff)
{
	unsigned long roff;
	struct file *file = NULL;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			return -EBADF;

		if (!file->f_op || !file->f_op->mmap) {
			addr = -ENODEV;
			goto out;
		}
	}

	/*
	 * A zero mmap always succeeds in Linux, independent of whether or not the
	 * remaining arguments are valid.
	 */
	if (len == 0)
		goto out;

	/* Careful about overflows.. */
	len = PAGE_ALIGN(len);
	if (!len || len > TASK_SIZE) {
		addr = -EINVAL;
		goto out;
	}

	/*
	 * Don't permit mappings into unmapped space, the virtual page table of a region,
	 * or across a region boundary.  Note: RGN_MAP_LIMIT is equal to 2^n-PAGE_SIZE
	 * (for some integer n <= 61) and len > 0.
	 */
	roff = REGION_OFFSET(addr);
	if ((len > RGN_MAP_LIMIT) || (roff > (RGN_MAP_LIMIT - len))) {
		addr = -EINVAL;
		goto out;
	}

	down_write(&current->mm->mmap_sem);
	addr = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);

out:	if (file)
		fput(file);
	return addr;
}

/*
 * mmap2() is like mmap() except that the offset is expressed in units
 * of PAGE_SIZE (instead of bytes).  This allows to mmap2() (pieces
 * of) files that are larger than the address space of the CPU.
 */
asmlinkage unsigned long
sys_mmap2 (unsigned long addr, unsigned long len, int prot, int flags, int fd, long pgoff)
{
	addr = do_mmap2(addr, len, prot, flags, fd, pgoff);
	if (!IS_ERR((void *) addr))
		force_successful_syscall_return();
	return addr;
}

asmlinkage unsigned long
sys_mmap (unsigned long addr, unsigned long len, int prot, int flags, int fd, long off)
{
	if (offset_in_page(off) != 0)
		return -EINVAL;

	addr = do_mmap2(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
	if (!IS_ERR((void *) addr))
		force_successful_syscall_return();
	return addr;
}

asmlinkage unsigned long
ia64_mremap (unsigned long addr, unsigned long old_len, unsigned long new_len, unsigned long flags,
	     unsigned long new_addr)
{
	extern unsigned long do_mremap (unsigned long addr,
					unsigned long old_len,
					unsigned long new_len,
					unsigned long flags,
					unsigned long new_addr);

	down_write(&current->mm->mmap_sem);
	{
		addr = do_mremap(addr, old_len, new_len, flags, new_addr);
	}
	up_write(&current->mm->mmap_sem);

	if (IS_ERR((void *) addr))
		return addr;

	force_successful_syscall_return();
	return addr;
}

#ifndef CONFIG_PCI

asmlinkage long
sys_pciconfig_read (unsigned long bus, unsigned long dfn, unsigned long off, unsigned long len,
		    void *buf)
{
	return -ENOSYS;
}

asmlinkage long
sys_pciconfig_write (unsigned long bus, unsigned long dfn, unsigned long off, unsigned long len,
		     void *buf)
{
	return -ENOSYS;
}

#endif /* CONFIG_PCI */
