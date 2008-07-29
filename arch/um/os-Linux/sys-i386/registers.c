/*
 * Copyright (C) 2004 PathScale, Inc
 * Copyright (C) 2004 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <errno.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include "kern_constants.h"
#include "longjmp.h"
#include "user.h"
#include "sysdep/ptrace_user.h"

int save_fp_registers(int pid, unsigned long *fp_regs)
{
	if (ptrace(PTRACE_GETFPREGS, pid, 0, fp_regs) < 0)
		return -errno;
	return 0;
}

int restore_fp_registers(int pid, unsigned long *fp_regs)
{
	if (ptrace(PTRACE_SETFPREGS, pid, 0, fp_regs) < 0)
		return -errno;
	return 0;
}

int save_fpx_registers(int pid, unsigned long *fp_regs)
{
	if (ptrace(PTRACE_GETFPXREGS, pid, 0, fp_regs) < 0)
		return -errno;
	return 0;
}

int restore_fpx_registers(int pid, unsigned long *fp_regs)
{
	if (ptrace(PTRACE_SETFPXREGS, pid, 0, fp_regs) < 0)
		return -errno;
	return 0;
}

unsigned long get_thread_reg(int reg, jmp_buf *buf)
{
	switch (reg) {
	case EIP:
		return buf[0]->__eip;
	case UESP:
		return buf[0]->__esp;
	case EBP:
		return buf[0]->__ebp;
	default:
		printk(UM_KERN_ERR "get_thread_regs - unknown register %d\n",
		       reg);
		return 0;
	}
}

int have_fpx_regs = 1;

int get_fp_registers(int pid, unsigned long *regs)
{
	if (have_fpx_regs)
		return save_fpx_registers(pid, regs);
	else
		return save_fp_registers(pid, regs);
}

int put_fp_registers(int pid, unsigned long *regs)
{
	if (have_fpx_regs)
		return restore_fpx_registers(pid, regs);
	else
		return restore_fp_registers(pid, regs);
}

void arch_init_registers(int pid)
{
	struct user_fpxregs_struct fpx_regs;
	int err;

	err = ptrace(PTRACE_GETFPXREGS, pid, 0, &fpx_regs);
	if (!err)
		return;

	if (errno != EIO)
		panic("check_ptrace : PTRACE_GETFPXREGS failed, errno = %d",
		      errno);

	have_fpx_regs = 0;
}
