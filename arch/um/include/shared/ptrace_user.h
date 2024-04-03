/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __PTRACE_USER_H__
#define __PTRACE_USER_H__

#include <sys/ptrace.h>
#include <sysdep/ptrace_user.h>

extern int ptrace_getregs(long pid, unsigned long *regs_out);
extern int ptrace_setregs(long pid, unsigned long *regs_in);

#endif
