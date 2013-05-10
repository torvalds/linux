/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#include <errno.h>
#include "ptrace_user.h"

int ptrace_getregs(long pid, unsigned long *regs_out)
{
	if (ptrace(PTRACE_GETREGS, pid, 0, regs_out) < 0)
		return -errno;
	return(0);
}

int ptrace_setregs(long pid, unsigned long *regs_out)
{
	if (ptrace(PTRACE_SETREGS, pid, 0, regs_out) < 0)
		return -errno;
	return(0);
}
