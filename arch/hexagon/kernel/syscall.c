/*
 * Hexagon system calls
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <asm/mman.h>
#include <asm/registers.h>

/*
 * System calls with architecture-specific wrappers.
 * See signal.c for signal-related system call wrappers.
 */

asmlinkage int sys_execve(char __user *ufilename,
			  const char __user *const __user *argv,
			  const char __user *const __user *envp)
{
	struct pt_regs *pregs = current_thread_info()->regs;
	struct filename *filename;
	int retval;

	filename = getname(ufilename);
	retval = PTR_ERR(filename);
	if (IS_ERR(filename))
		return retval;

	retval = do_execve(filename->name, argv, envp, pregs);
	putname(filename);

	return retval;
}

asmlinkage int sys_clone(unsigned long clone_flags, unsigned long newsp,
			 unsigned long parent_tidp, unsigned long child_tidp)
{
	struct pt_regs *pregs = current_thread_info()->regs;

	if (!newsp)
		newsp = pregs->SP;
	return do_fork(clone_flags, newsp, pregs, 0, (int __user *)parent_tidp,
		       (int __user *)child_tidp);
}

/*
 * Do a system call from the kernel, so as to have a proper pt_regs
 * and recycle the sys_execvpe infrustructure.
 */
int kernel_execve(const char *filename,
		  const char *const argv[], const char *const envp[])
{
	register unsigned long __a0 asm("r0") = (unsigned long) filename;
	register unsigned long __a1 asm("r1") = (unsigned long) argv;
	register unsigned long __a2 asm("r2") = (unsigned long) envp;
	int retval;

	__asm__ volatile(
		"	R6 = #%4;\n"
		"	trap0(#1);\n"
		"	%0 = R0;\n"
		: "=r" (retval)
		: "r" (__a0), "r" (__a1), "r" (__a2), "i" (__NR_execve)
	);

	return retval;
}
