/*
 * Copyright (C) 2004 PathScale, Inc
 * Copyright (C) 2004 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <errno.h>
#include <string.h>
#include <sys/ptrace.h>
#include "sysdep/ptrace.h"
#include "user.h"

/* This is set once at boot time and not changed thereafter */

static unsigned long exec_regs[MAX_REG_NR];

void init_thread_registers(struct uml_pt_regs *to)
{
	memcpy(to->gp, exec_regs, sizeof(to->gp));
}

void save_registers(int pid, struct uml_pt_regs *regs)
{
	int err;

	err = ptrace(PTRACE_GETREGS, pid, 0, regs->gp);
	if (err < 0)
		panic("save_registers - saving registers failed, errno = %d\n",
		      errno);
}

void restore_registers(int pid, struct uml_pt_regs *regs)
{
	int err;

	err = ptrace(PTRACE_SETREGS, pid, 0, regs->gp);
	if (err < 0)
		panic("restore_registers - saving registers failed, "
		      "errno = %d\n", errno);
}

void init_registers(int pid)
{
	int err;

	err = ptrace(PTRACE_GETREGS, pid, 0, exec_regs);
	if (err)
		panic("check_ptrace : PTRACE_GETREGS failed, errno = %d",
		      errno);

	arch_init_registers(pid);
}

void get_safe_registers(unsigned long *regs)
{
	memcpy(regs, exec_regs, sizeof(exec_regs));
}
