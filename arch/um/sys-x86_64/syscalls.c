/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#include "linux/linkage.h"
#include "linux/slab.h"
#include "linux/shm.h"
#include "linux/utsname.h"
#include "linux/personality.h"
#include "asm/uaccess.h"
#define __FRAME_OFFSETS
#include "asm/ptrace.h"
#include "asm/unistd.h"
#include "asm/prctl.h" /* XXX This should get the constants from libc */
#include "choose-mode.h"
#include "kern.h"

asmlinkage long sys_uname64(struct new_utsname __user * name)
{
	int err;
	down_read(&uts_sem);
	err = copy_to_user(name, &system_utsname, sizeof (*name));
	up_read(&uts_sem);
	if (personality(current->personality) == PER_LINUX32)
		err |= copy_to_user(&name->machine, "i686", 5);
	return err ? -EFAULT : 0;
}

#ifdef CONFIG_MODE_TT
extern long arch_prctl(int code, unsigned long addr);

static long arch_prctl_tt(int code, unsigned long addr)
{
	unsigned long tmp;
	long ret;

	switch(code){
	case ARCH_SET_GS:
	case ARCH_SET_FS:
		ret = arch_prctl(code, addr);
		break;
	case ARCH_GET_FS:
	case ARCH_GET_GS:
		ret = arch_prctl(code, (unsigned long) &tmp);
		if(!ret)
			ret = put_user(tmp, (long __user *)addr);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return(ret);
}
#endif

#ifdef CONFIG_MODE_SKAS

/* XXX: Must also call arch_prctl in the host, beside saving the segment bases! */
static long arch_prctl_skas(int code, unsigned long addr)
{
	long ret = 0;

	switch(code){
	case ARCH_SET_FS:
		current->thread.regs.regs.skas.regs[FS_BASE / sizeof(unsigned long)] = addr;
		break;
	case ARCH_SET_GS:
		current->thread.regs.regs.skas.regs[GS_BASE / sizeof(unsigned long)] = addr;
		break;
	case ARCH_GET_FS:
		ret = put_user(current->thread.regs.regs.skas.
				regs[FS_BASE / sizeof(unsigned long)],
				(unsigned long __user *)addr);
	        break;
	case ARCH_GET_GS:
		ret = put_user(current->thread.regs.regs.skas.
				regs[GS_BASE / sizeof(unsigned long)],
				(unsigned long __user *)addr);
	        break;
	default:
		ret = -EINVAL;
		break;
	}

	return(ret);
}
#endif

long sys_arch_prctl(int code, unsigned long addr)
{
	return(CHOOSE_MODE_PROC(arch_prctl_tt, arch_prctl_skas, code, addr));
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
	return(ret);
}
