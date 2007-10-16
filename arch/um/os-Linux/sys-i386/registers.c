/*
 * Copyright (C) 2004 PathScale, Inc
 * Licensed under the GPL
 */

#include <errno.h>
#include <sysdep/ptrace_user.h>
#include "longjmp.h"
#include "user.h"

int save_fp_registers(int pid, unsigned long *fp_regs)
{
	if(ptrace(PTRACE_GETFPREGS, pid, 0, fp_regs) < 0)
		return -errno;
	return 0;
}

int restore_fp_registers(int pid, unsigned long *fp_regs)
{
	if(ptrace(PTRACE_SETFPREGS, pid, 0, fp_regs) < 0)
		return -errno;
	return 0;
}

int save_fpx_registers(int pid, unsigned long *fp_regs)
{
	if(ptrace(PTRACE_GETFPXREGS, pid, 0, fp_regs) < 0)
		return -errno;
	return 0;
}

int restore_fpx_registers(int pid, unsigned long *fp_regs)
{
	if(ptrace(PTRACE_SETFPXREGS, pid, 0, fp_regs) < 0)
		return -errno;
	return 0;
}

unsigned long get_thread_reg(int reg, jmp_buf *buf)
{
	switch(reg){
	case EIP: return buf[0]->__eip;
	case UESP: return buf[0]->__esp;
	case EBP: return buf[0]->__ebp;
	default:
		printk("get_thread_regs - unknown register %d\n", reg);
		return 0;
	}
}

int have_fpx_regs = 1;

void arch_init_registers(int pid)
{
	unsigned long fpx_regs[HOST_XFP_SIZE];
	int err;

	err = ptrace(PTRACE_GETFPXREGS, pid, 0, fpx_regs);
	if(!err)
		return;

	if(errno != EIO)
		panic("check_ptrace : PTRACE_GETFPXREGS failed, errno = %d",
		      errno);

	have_fpx_regs = 0;
}
