// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains various system calls that have different calling
 * conventions on different platforms.
 *
 * Copyright (C) 1999-2000, 2002-2003, 2005 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task_stack.h>
#include <linux/shm.h>
#include <linux/file.h>		/* doh, must come after sched.h... */
#include <linux/smp.h>
#include <linux/syscalls.h>
#include <linux/highuid.h>
#include <linux/hugetlb.h>

#include <asm/shmparam.h>
#include <linux/uaccess.h>

unsigned long
arch_get_unmapped_area (struct file *filp, unsigned long addr, unsigned long len,
			unsigned long pgoff, unsigned long flags)
{
	long map_shared = (flags & MAP_SHARED);
	unsigned long align_mask = 0;
	struct mm_struct *mm = current->mm;
	struct vm_unmapped_area_info info;

	if (len > RGN_MAP_LIMIT)
		return -ENOMEM;

	/* handle fixed mapping: prevent overlap with huge pages */
	if (flags & MAP_FIXED) {
		if (is_hugepage_only_range(mm, addr, len))
			return -EINVAL;
		return addr;
	}

#ifdef CONFIG_HUGETLB_PAGE
	if (REGION_NUMBER(addr) == RGN_HPAGE)
		addr = 0;
#endif
	if (!addr)
		addr = TASK_UNMAPPED_BASE;

	if (map_shared && (TASK_SIZE > 0xfffffffful))
		/*
		 * For 64-bit tasks, align shared segments to 1MB to avoid potential
		 * performance penalty due to virtual aliasing (see ASDM).  For 32-bit
		 * tasks, we prefer to avoid exhausting the address space too quickly by
		 * limiting alignment to a single page.
		 */
		align_mask = PAGE_MASK & (SHMLBA - 1);

	info.flags = 0;
	info.length = len;
	info.low_limit = addr;
	info.high_limit = TASK_SIZE;
	info.align_mask = align_mask;
	info.align_offset = 0;
	return vm_unmapped_area(&info);
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
ia64_brk (unsigned long brk)
{
	unsigned long retval = sys_brk(brk);
	force_successful_syscall_return();
	return retval;
}

/*
 * On IA-64, we return the two file descriptors in ret0 and ret1 (r8
 * and r9) as this is faster than doing a copy_to_user().
 */
asmlinkage long
sys_ia64_pipe (void)
{
	struct pt_regs *regs = task_pt_regs(current);
	int fd[2];
	int retval;

	retval = do_pipe_flags(fd, 0);
	if (retval)
		goto out;
	retval = fd[0];
	regs->r9 = fd[1];
  out:
	return retval;
}

int ia64_mmap_check(unsigned long addr, unsigned long len,
		unsigned long flags)
{
	unsigned long roff;

	/*
	 * Don't permit mappings into unmapped space, the virtual page table
	 * of a region, or across a region boundary.  Note: RGN_MAP_LIMIT is
	 * equal to 2^n-PAGE_SIZE (for some integer n <= 61) and len > 0.
	 */
	roff = REGION_OFFSET(addr);
	if ((len > RGN_MAP_LIMIT) || (roff > (RGN_MAP_LIMIT - len)))
		return -EINVAL;
	return 0;
}

/*
 * mmap2() is like mmap() except that the offset is expressed in units
 * of PAGE_SIZE (instead of bytes).  This allows to mmap2() (pieces
 * of) files that are larger than the address space of the CPU.
 */
asmlinkage unsigned long
sys_mmap2 (unsigned long addr, unsigned long len, int prot, int flags, int fd, long pgoff)
{
	addr = ksys_mmap_pgoff(addr, len, prot, flags, fd, pgoff);
	if (!IS_ERR((void *) addr))
		force_successful_syscall_return();
	return addr;
}

asmlinkage unsigned long
sys_mmap (unsigned long addr, unsigned long len, int prot, int flags, int fd, long off)
{
	if (offset_in_page(off) != 0)
		return -EINVAL;

	addr = ksys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
	if (!IS_ERR((void *) addr))
		force_successful_syscall_return();
	return addr;
}

asmlinkage unsigned long
ia64_mremap (unsigned long addr, unsigned long old_len, unsigned long new_len, unsigned long flags,
	     unsigned long new_addr)
{
	addr = sys_mremap(addr, old_len, new_len, flags, new_addr);
	if (!IS_ERR((void *) addr))
		force_successful_syscall_return();
	return addr;
}

asmlinkage long
ia64_clock_getres(const clockid_t which_clock, struct __kernel_timespec __user *tp)
{
	struct timespec64 rtn_tp;
	s64 tick_ns;

	/*
	 * ia64's clock_gettime() syscall is implemented as a vdso call
	 * fsys_clock_gettime(). Currently it handles only
	 * CLOCK_REALTIME and CLOCK_MONOTONIC. Both are based on
	 * 'ar.itc' counter which gets incremented at a constant
	 * frequency. It's usually 400MHz, ~2.5x times slower than CPU
	 * clock frequency. Which is almost a 1ns hrtimer, but not quite.
	 *
	 * Let's special-case these timers to report correct precision
	 * based on ITC frequency and not HZ frequency for supported
	 * clocks.
	 */
	switch (which_clock) {
	case CLOCK_REALTIME:
	case CLOCK_MONOTONIC:
		tick_ns = DIV_ROUND_UP(NSEC_PER_SEC, local_cpu_data->itc_freq);
		rtn_tp = ns_to_timespec64(tick_ns);
		return put_timespec64(&rtn_tp, tp);
	}

	return sys_clock_getres(which_clock, tp);
}
