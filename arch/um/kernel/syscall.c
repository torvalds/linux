/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include "linux/file.h"
#include "linux/fs.h"
#include "linux/mm.h"
#include "linux/sched.h"
#include "linux/utsname.h"
#include "linux/syscalls.h"
#include "asm/current.h"
#include "asm/mman.h"
#include "asm/uaccess.h"
#include "asm/unistd.h"
#include "internal.h"

long sys_fork(void)
{
	return do_fork(SIGCHLD, UPT_SP(&current->thread.regs.regs),
		      &current->thread.regs, 0, NULL, NULL);
}

long sys_vfork(void)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD,
		      UPT_SP(&current->thread.regs.regs),
		      &current->thread.regs, 0, NULL, NULL);
}

long sys_clone(unsigned long clone_flags, unsigned long newsp,
	       void __user *parent_tid, void __user *child_tid)
{
	if (!newsp)
		newsp = UPT_SP(&current->thread.regs.regs);

	return do_fork(clone_flags, newsp, &current->thread.regs, 0, parent_tid,
		      child_tid);
}

long old_mmap(unsigned long addr, unsigned long len,
	      unsigned long prot, unsigned long flags,
	      unsigned long fd, unsigned long offset)
{
	long err = -EINVAL;
	if (offset & ~PAGE_MASK)
		goto out;

	err = sys_mmap_pgoff(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
 out:
	return err;
}

int kernel_execve(const char *filename,
		  const char *const argv[],
		  const char *const envp[])
{
	mm_segment_t fs;
	int ret;

	fs = get_fs();
	set_fs(KERNEL_DS);
	ret = um_execve(filename, (const char __user *const __user *)argv,
			(const char __user *const __user *) envp);
	set_fs(fs);

	return ret;
}
