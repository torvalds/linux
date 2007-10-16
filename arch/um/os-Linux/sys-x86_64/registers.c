/*
 * Copyright (C) 2006-2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <errno.h>
#include <sys/ptrace.h>
#define __FRAME_OFFSETS
#include <asm/ptrace.h>
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

unsigned long get_thread_reg(int reg, jmp_buf *buf)
{
	switch(reg){
	case RIP: return buf[0]->__rip;
	case RSP: return buf[0]->__rsp;
	case RBP: return buf[0]->__rbp;
	default:
		printk("get_thread_regs - unknown register %d\n", reg);
		return 0;
	}
}
