/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include "ptrace_user.h"
/* Grr, asm/user.h includes asm/ptrace.h, so has to follow ptrace_user.h */
#include <asm/user.h>
#include "kern_util.h"
#include "sysdep/thread.h"
#include "user.h"
#include "os.h"
#include "uml-config.h"

int ptrace_getregs(long pid, unsigned long *regs_out)
{
	if (ptrace(PTRACE_GETREGS, pid, 0, regs_out) < 0)
		return -errno;
	return 0;
}

int ptrace_setregs(long pid, unsigned long *regs)
{
	if (ptrace(PTRACE_SETREGS, pid, 0, regs) < 0)
		return -errno;
	return 0;
}

int ptrace_getfpregs(long pid, unsigned long *regs)
{
	if (ptrace(PTRACE_GETFPREGS, pid, 0, regs) < 0)
		return -errno;
	return 0;
}

int ptrace_setfpregs(long pid, unsigned long *regs)
{
	if (ptrace(PTRACE_SETFPREGS, pid, 0, regs) < 0)
		return -errno;
	return 0;
}
