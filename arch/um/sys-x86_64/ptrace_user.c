/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#include <stddef.h>
#include <errno.h>
#include "ptrace_user.h"
#include "user.h"
#include "kern_constants.h"

int ptrace_getregs(long pid, unsigned long *regs_out)
{
	if(ptrace(PTRACE_GETREGS, pid, 0, regs_out) < 0)
		return(-errno);
	return(0);
}

int ptrace_setregs(long pid, unsigned long *regs)
{
	if(ptrace(PTRACE_SETREGS, pid, 0, regs) < 0)
		return(-errno);
	return(0);
}

void ptrace_pokeuser(unsigned long addr, unsigned long data)
{
	panic("ptrace_pokeuser");
}

#define DS 184
#define ES 192
#define __USER_DS     0x2b

void arch_enter_kernel(void *task, int pid)
{
}

void arch_leave_kernel(void *task, int pid)
{
#ifdef UM_USER_CS
        if(ptrace(PTRACE_POKEUSR, pid, CS, UM_USER_CS) < 0)
                printk("POKEUSR CS failed");
#endif

        if(ptrace(PTRACE_POKEUSR, pid, DS, __USER_DS) < 0)
                printk("POKEUSR DS failed");
        if(ptrace(PTRACE_POKEUSR, pid, ES, __USER_DS) < 0)
                printk("POKEUSR ES failed");
}
