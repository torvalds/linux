/*
 * linux/arch/unicore32/kernel/sys.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/syscalls.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/ipc.h>
#include <linux/uaccess.h>

#include <asm/syscalls.h>
#include <asm/cacheflush.h>

/* Clone a task - this clones the calling program thread.
 * This is called indirectly via a small wrapper
 */
asmlinkage long __sys_clone(unsigned long clone_flags, unsigned long newsp,
			 void __user *parent_tid, void __user *child_tid,
			 struct pt_regs *regs)
{
	if (!newsp)
		newsp = regs->UCreg_sp;

	return do_fork(clone_flags, newsp, regs, 0,
			parent_tid, child_tid);
}

/* sys_execve() executes a new program.
 * This is called indirectly via a small wrapper
 */
asmlinkage long __sys_execve(const char __user *filename,
			  const char __user *const __user *argv,
			  const char __user *const __user *envp,
			  struct pt_regs *regs)
{
	int error;
	struct filename *fn;

	fn = getname(filename);
	error = PTR_ERR(fn);
	if (IS_ERR(fn))
		goto out;
	error = do_execve(fn->name, argv, envp, regs);
	putname(fn);
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
	regs.UCreg_00 = ret;

	/*
	 * We were successful.  We won't be returning to our caller, but
	 * instead to user space by manipulating the kernel stack.
	 */
	asm("add	r0, %0, %1\n\t"
		"mov	r1, %2\n\t"
		"mov	r2, %3\n\t"
		"mov	r22, #0\n\t"	/* not a syscall */
		"mov	r23, %0\n\t"	/* thread structure */
		"b.l	memmove\n\t"	/* copy regs to top of stack */
		"mov	sp, r0\n\t"	/* reposition stack pointer */
		"b	ret_to_user"
		:
		: "r" (current_thread_info()),
		  "Ir" (THREAD_START_SP - sizeof(regs)),
		  "r" (&regs),
		  "Ir" (sizeof(regs))
		: "r0", "r1", "r2", "r3", "ip", "lr", "memory");

 out:
	return ret;
}

/* Note: used by the compat code even in 64-bit Linux. */
SYSCALL_DEFINE6(mmap2, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, off_4k)
{
	return sys_mmap_pgoff(addr, len, prot, flags, fd,
			      off_4k);
}

/* Provide the actual syscall number to call mapping. */
#undef __SYSCALL
#define __SYSCALL(nr, call)	[nr] = (call),

/* Note that we don't include <linux/unistd.h> but <asm/unistd.h> */
void *sys_call_table[__NR_syscalls] = {
	[0 ... __NR_syscalls-1] = sys_ni_syscall,
#include <asm/unistd.h>
};
