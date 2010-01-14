/*
 * arch/score/kernel/syscall.c
 *
 * Score Processor version.
 *
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/unistd.h>
#include <linux/syscalls.h>
#include <asm/syscalls.h>

asmlinkage long 
sys_mmap2(unsigned long addr, unsigned long len, unsigned long prot,
	  unsigned long flags, unsigned long fd, unsigned long pgoff)
{
	return sys_mmap_pgoff(addr, len, prot, flags, fd, pgoff);
}

asmlinkage long
sys_mmap(unsigned long addr, unsigned long len, unsigned long prot,
	unsigned long flags, unsigned long fd, off_t offset)
{
	if (unlikely(offset & ~PAGE_MASK))
		return -EINVAL;
	return sys_mmap_pgoff(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
}

asmlinkage long
score_fork(struct pt_regs *regs)
{
	return do_fork(SIGCHLD, regs->regs[0], regs, 0, NULL, NULL);
}

/*
 * Clone a task - this clones the calling program thread.
 * This is called indirectly via a small wrapper
 */
asmlinkage long
score_clone(struct pt_regs *regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	int __user *parent_tidptr, *child_tidptr;

	clone_flags = regs->regs[4];
	newsp = regs->regs[5];
	if (!newsp)
		newsp = regs->regs[0];
	parent_tidptr = (int __user *)regs->regs[6];
	child_tidptr = (int __user *)regs->regs[8];

	return do_fork(clone_flags, newsp, regs, 0,
			parent_tidptr, child_tidptr);
}

asmlinkage long
score_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD,
			regs->regs[0], regs, 0, NULL, NULL);
}

/*
 * sys_execve() executes a new program.
 * This is called indirectly via a small wrapper
 */
asmlinkage long
score_execve(struct pt_regs *regs)
{
	int error;
	char *filename;

	filename = getname((char __user*)regs->regs[4]);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		return error;

	error = do_execve(filename, (char __user *__user*)regs->regs[5],
			  (char __user *__user *) regs->regs[6], regs);

	putname(filename);
	return error;
}

/*
 * Do a system call from kernel instead of calling sys_execve so we
 * end up with proper pt_regs.
 */
int kernel_execve(const char *filename, char *const argv[], char *const envp[])
{
	register unsigned long __r4 asm("r4") = (unsigned long) filename;
	register unsigned long __r5 asm("r5") = (unsigned long) argv;
	register unsigned long __r6 asm("r6") = (unsigned long) envp;
	register unsigned long __r7 asm("r7");

	__asm__ __volatile__ ("	\n"
		"ldi	r27, %5		\n"
		"syscall		\n"
		"mv	%0, r4		\n"
		"mv	%1, r7		\n"
		: "=&r" (__r4), "=r" (__r7)
		: "r" (__r4), "r" (__r5), "r" (__r6), "i" (__NR_execve)
		: "r8", "r9", "r10", "r11", "r22", "r23", "r24", "r25",
		  "r26", "r27", "memory");

	if (__r7 == 0)
		return __r4;

	return -__r4;
}
