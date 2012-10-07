/*
 * AArch64-specific system calls implementation
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
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

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/syscalls.h>

/*
 * Clone a task - this clones the calling program thread.
 */
asmlinkage long sys_clone(unsigned long clone_flags, unsigned long newsp,
			  int __user *parent_tidptr, unsigned long tls_val,
			  int __user *child_tidptr, struct pt_regs *regs)
{
	if (!newsp)
		newsp = regs->sp;
	/* 16-byte aligned stack mandatory on AArch64 */
	if (newsp & 15)
		return -EINVAL;
	return do_fork(clone_flags, newsp, regs, 0, parent_tidptr, child_tidptr);
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage long sys_execve(const char __user *filenamei,
			   const char __user *const __user *argv,
			   const char __user *const __user *envp,
			   struct pt_regs *regs)
{
	long error;
	char * filename;

	filename = getname(filenamei);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
out:
	return error;
}

int kernel_execve(const char *filename,
		  const char *const argv[],
		  const char *const envp[])
{
	struct pt_regs regs;
	int ret;

	memset(&regs, 0, sizeof(struct pt_regs));
	ret = do_execve(filename,
			(const char __user *const __user *)argv,
			(const char __user *const __user *)envp, &regs);
	if (ret < 0)
		goto out;

	/*
	 * Save argc to the register structure for userspace.
	 */
	regs.regs[0] = ret;

	/*
	 * We were successful.  We won't be returning to our caller, but
	 * instead to user space by manipulating the kernel stack.
	 */
	asm(	"add	x0, %0, %1\n\t"
		"mov	x1, %2\n\t"
		"mov	x2, %3\n\t"
		"bl	memmove\n\t"	/* copy regs to top of stack */
		"mov	x27, #0\n\t"	/* not a syscall */
		"mov	x28, %0\n\t"	/* thread structure */
		"mov	sp, x0\n\t"	/* reposition stack pointer */
		"b	ret_to_user"
		:
		: "r" (current_thread_info()),
		  "Ir" (THREAD_START_SP - sizeof(regs)),
		  "r" (&regs),
		  "Ir" (sizeof(regs))
		: "x0", "x1", "x2", "x27", "x28", "x30", "memory");

 out:
	return ret;
}
EXPORT_SYMBOL(kernel_execve);

asmlinkage long sys_mmap(unsigned long addr, unsigned long len,
			 unsigned long prot, unsigned long flags,
			 unsigned long fd, off_t off)
{
	if (offset_in_page(off) != 0)
		return -EINVAL;

	return sys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
}

/*
 * Wrappers to pass the pt_regs argument.
 */
#define sys_execve		sys_execve_wrapper
#define sys_clone		sys_clone_wrapper
#define sys_rt_sigreturn	sys_rt_sigreturn_wrapper
#define sys_sigaltstack		sys_sigaltstack_wrapper

#include <asm/syscalls.h>

#undef __SYSCALL
#define __SYSCALL(nr, sym)	[nr] = sym,

/*
 * The sys_call_table array must be 4K aligned to be accessible from
 * kernel/entry.S.
 */
void *sys_call_table[__NR_syscalls] __aligned(4096) = {
	[0 ... __NR_syscalls - 1] = sys_ni_syscall,
#include <asm/unistd.h>
};
