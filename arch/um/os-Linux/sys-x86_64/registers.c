/*
 * Copyright (C) 2004 PathScale, Inc
 * Licensed under the GPL
 */

#include <errno.h>
#include <sys/ptrace.h>
#include <string.h>
#include "ptrace_user.h"
#include "uml-config.h"
#include "skas_ptregs.h"
#include "registers.h"
#include "longjmp.h"
#include "user.h"

/* These are set once at boot time and not changed thereafter */

static unsigned long exec_regs[MAX_REG_NR];
static unsigned long exec_fp_regs[HOST_FP_SIZE];

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

void init_thread_registers(union uml_pt_regs *to)
{
	memcpy(to->skas.regs, exec_regs, sizeof(to->skas.regs));
	memcpy(to->skas.fp, exec_fp_regs, sizeof(to->skas.fp));
}

static int move_registers(int pid, int int_op, int fp_op,
			  union uml_pt_regs *regs)
{
	if(ptrace(int_op, pid, 0, regs->skas.regs) < 0)
		return -errno;

	if(ptrace(fp_op, pid, 0, regs->skas.fp) < 0)
		return -errno;

	return 0;
}

void save_registers(int pid, union uml_pt_regs *regs)
{
	int err;

	err = move_registers(pid, PTRACE_GETREGS, PTRACE_GETFPREGS, regs);
	if(err)
		panic("save_registers - saving registers failed, errno = %d\n",
		      -err);
}

void restore_registers(int pid, union uml_pt_regs *regs)
{
	int err;

	err = move_registers(pid, PTRACE_SETREGS, PTRACE_SETFPREGS, regs);
	if(err)
		panic("restore_registers - saving registers failed, "
		      "errno = %d\n", -err);
}

void init_registers(int pid)
{
	int err;

	err = ptrace(PTRACE_GETREGS, pid, 0, exec_regs);
	if(err)
		panic("check_ptrace : PTRACE_GETREGS failed, errno = %d",
		      errno);

	err = ptrace(PTRACE_GETFPREGS, pid, 0, exec_fp_regs);
	if(err)
		panic("check_ptrace : PTRACE_GETFPREGS failed, errno = %d",
		      errno);
}

void get_safe_registers(unsigned long *regs, unsigned long *fp_regs)
{
	memcpy(regs, exec_regs, sizeof(exec_regs));
	if(fp_regs != NULL)
		memcpy(fp_regs, exec_fp_regs,
		       HOST_FP_SIZE * sizeof(unsigned long));
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
