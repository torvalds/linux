/*
 * Copyright (C) 2007 Jeff Dike (jdike@{addtoit.com,linux.intel.com})
 * Licensed under the GPL
 */

#include <sys/ptrace.h>
#include <asm/ptrace.h>

int os_arch_prctl(int pid, int option, unsigned long *arg2)
{
	return ptrace(PTRACE_ARCH_PRCTL, pid, (unsigned long) arg2, option);
}
