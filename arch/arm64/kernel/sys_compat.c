/*
 * Based on arch/arm/kernel/sys_arm.c
 *
 * Copyright (C) People who wrote linux/arch/i386/kernel/sys_i386.c
 * Copyright (C) 1995, 1996 Russell King.
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define __SYSCALL_COMPAT

#include <linux/compat.h>
#include <linux/personality.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>
#include <asm/unistd.h>

asmlinkage int compat_sys_fork(struct pt_regs *regs)
{
	return do_fork(SIGCHLD, regs->compat_sp, regs, 0, NULL, NULL);
}

asmlinkage int compat_sys_clone(unsigned long clone_flags, unsigned long newsp,
			  int __user *parent_tidptr, int tls_val,
			  int __user *child_tidptr, struct pt_regs *regs)
{
	if (!newsp)
		newsp = regs->compat_sp;

	return do_fork(clone_flags, newsp, regs, 0, parent_tidptr, child_tidptr);
}

asmlinkage int compat_sys_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->compat_sp,
		       regs, 0, NULL, NULL);
}

asmlinkage int compat_sys_execve(const char __user *filenamei,
				 compat_uptr_t argv, compat_uptr_t envp,
				 struct pt_regs *regs)
{
	int error;
	char * filename;

	filename = getname(filenamei);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = compat_do_execve(filename, compat_ptr(argv), compat_ptr(envp),
				 regs);
	putname(filename);
out:
	return error;
}

asmlinkage int compat_sys_sched_rr_get_interval(compat_pid_t pid,
						struct compat_timespec __user *interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_sched_rr_get_interval(pid, (struct timespec __user *)&t);
	set_fs(old_fs);
	if (put_compat_timespec(&t, interval))
		return -EFAULT;
	return ret;
}

static inline void
do_compat_cache_op(unsigned long start, unsigned long end, int flags)
{
	struct mm_struct *mm = current->active_mm;
	struct vm_area_struct *vma;

	if (end < start || flags)
		return;

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, start);
	if (vma && vma->vm_start < end) {
		if (start < vma->vm_start)
			start = vma->vm_start;
		if (end > vma->vm_end)
			end = vma->vm_end;
		up_read(&mm->mmap_sem);
		__flush_cache_user_range(start & PAGE_MASK, PAGE_ALIGN(end));
		return;
	}
	up_read(&mm->mmap_sem);
}

/*
 * Handle all unrecognised system calls.
 */
long compat_arm_syscall(struct pt_regs *regs)
{
	unsigned int no = regs->regs[7];

	switch (no) {
	/*
	 * Flush a region from virtual address 'r0' to virtual address 'r1'
	 * _exclusive_.  There is no alignment requirement on either address;
	 * user space does not need to know the hardware cache layout.
	 *
	 * r2 contains flags.  It should ALWAYS be passed as ZERO until it
	 * is defined to be something else.  For now we ignore it, but may
	 * the fires of hell burn in your belly if you break this rule. ;)
	 *
	 * (at a later date, we may want to allow this call to not flush
	 * various aspects of the cache.  Passing '0' will guarantee that
	 * everything necessary gets flushed to maintain consistency in
	 * the specified region).
	 */
	case __ARM_NR_compat_cacheflush:
		do_compat_cache_op(regs->regs[0], regs->regs[1], regs->regs[2]);
		return 0;

	case __ARM_NR_compat_set_tls:
		current->thread.tp_value = regs->regs[0];
		asm ("msr tpidrro_el0, %0" : : "r" (regs->regs[0]));
		return 0;

	default:
		return -ENOSYS;
	}
}
