/*
 * Copyright (C) 2003 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#include "linux/linkage.h"
#include "linux/personality.h"
#include "linux/utsname.h"
#include "asm/prctl.h" /* XXX This should get the constants from libc */
#include "asm/uaccess.h"
#include "os.h"

asmlinkage long sys_uname64(struct new_utsname __user * name)
{
	int err;

	down_read(&uts_sem);
	err = copy_to_user(name, utsname(), sizeof (*name));
	up_read(&uts_sem);

	if (personality(current->personality) == PER_LINUX32)
		err |= copy_to_user(&name->machine, "i686", 5);

	return err ? -EFAULT : 0;
}

long arch_prctl(struct task_struct *task, int code, unsigned long __user *addr)
{
	unsigned long *ptr = addr, tmp;
	long ret;
	int pid = task->mm->context.skas.id.u.pid;

	/*
	 * With ARCH_SET_FS (and ARCH_SET_GS is treated similarly to
	 * be safe), we need to call arch_prctl on the host because
	 * setting %fs may result in something else happening (like a
	 * GDT or thread.fs being set instead).  So, we let the host
	 * fiddle the registers and thread struct and restore the
	 * registers afterwards.
	 *
	 * So, the saved registers are stored to the process (this
	 * needed because a stub may have been the last thing to run),
	 * arch_prctl is run on the host, then the registers are read
	 * back.
	 */
	switch (code) {
	case ARCH_SET_FS:
	case ARCH_SET_GS:
		restore_registers(pid, &current->thread.regs.regs);
		break;
	case ARCH_GET_FS:
	case ARCH_GET_GS:
		/*
		 * With these two, we read to a local pointer and
		 * put_user it to the userspace pointer that we were
		 * given.  If addr isn't valid (because it hasn't been
		 * faulted in or is just bogus), we want put_user to
		 * fault it in (or return -EFAULT) instead of having
		 * the host return -EFAULT.
		 */
		ptr = &tmp;
	}

	ret = os_arch_prctl(pid, code, ptr);
	if (ret)
		return ret;

	switch (code) {
	case ARCH_SET_FS:
		current->thread.arch.fs = (unsigned long) ptr;
		save_registers(pid, &current->thread.regs.regs);
		break;
	case ARCH_SET_GS:
		save_registers(pid, &current->thread.regs.regs);
		break;
	case ARCH_GET_FS:
		ret = put_user(tmp, addr);
		break;
	case ARCH_GET_GS:
		ret = put_user(tmp, addr);
		break;
	}

	return ret;
}

long sys_arch_prctl(int code, unsigned long addr)
{
	return arch_prctl(current, code, (unsigned long __user *) addr);
}

long sys_clone(unsigned long clone_flags, unsigned long newsp,
	       void __user *parent_tid, void __user *child_tid)
{
	long ret;

	if (!newsp)
		newsp = UPT_SP(&current->thread.regs.regs);
	current->thread.forking = 1;
	ret = do_fork(clone_flags, newsp, &current->thread.regs, 0, parent_tid,
		      child_tid);
	current->thread.forking = 0;
	return ret;
}

void arch_switch_to(struct task_struct *from, struct task_struct *to)
{
	if ((to->thread.arch.fs == 0) || (to->mm == NULL))
		return;

	arch_prctl(to, ARCH_SET_FS, (void __user *) to->thread.arch.fs);
}
