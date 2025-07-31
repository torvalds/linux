// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2004 PathScale, Inc
 * Copyright (C) 2004 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <errno.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sysdep/ptrace.h>
#include <sysdep/ptrace_user.h>
#include <registers.h>
#include <stdlib.h>

/* This is set once at boot time and not changed thereafter */

unsigned long exec_regs[MAX_REG_NR];
unsigned long *exec_fp_regs;

int init_pid_registers(int pid)
{
	int err;

	err = ptrace(PTRACE_GETREGS, pid, 0, exec_regs);
	if (err < 0)
		return -errno;

	err = arch_init_registers(pid);
	if (err < 0)
		return err;

	exec_fp_regs = malloc(host_fp_size);
	get_fp_registers(pid, exec_fp_regs);
	return 0;
}

void get_safe_registers(unsigned long *regs, unsigned long *fp_regs)
{
	memcpy(regs, exec_regs, sizeof(exec_regs));

	if (fp_regs)
		memcpy(fp_regs, exec_fp_regs, host_fp_size);
}
