/* 
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __PTRACE_USER_H__
#define __PTRACE_USER_H__

#include "sysdep/ptrace_user.h"

extern int ptrace_getregs(long pid, unsigned long *regs_out);
extern int ptrace_setregs(long pid, unsigned long *regs_in);

/* syscall emulation path in ptrace */

#ifndef PTRACE_SYSEMU
#define PTRACE_SYSEMU 31
#endif
#ifndef PTRACE_SYSEMU_SINGLESTEP
#define PTRACE_SYSEMU_SINGLESTEP 32
#endif

/* On architectures, that started to support PTRACE_O_TRACESYSGOOD
 * in linux 2.4, there are two different definitions of
 * PTRACE_SETOPTIONS: linux 2.4 uses 21 while linux 2.6 uses 0x4200.
 * For binary compatibility, 2.6 also supports the old "21", named
 * PTRACE_OLDSETOPTION. On these architectures, UML always must use
 * "21", to ensure the kernel runs on 2.4 and 2.6 host without
 * recompilation. So, we use PTRACE_OLDSETOPTIONS in UML.
 * We also want to be able to build the kernel on 2.4, which doesn't
 * have PTRACE_OLDSETOPTIONS. So, if it is missing, we declare
 * PTRACE_OLDSETOPTIONS to to be the same as PTRACE_SETOPTIONS.
 *
 * On architectures, that start to support PTRACE_O_TRACESYSGOOD on
 * linux 2.6, PTRACE_OLDSETOPTIONS never is defined, and also isn't
 * supported by the host kernel. In that case, our trick lets us use
 * the new 0x4200 with the name PTRACE_OLDSETOPTIONS.
 */
#ifndef PTRACE_OLDSETOPTIONS
#define PTRACE_OLDSETOPTIONS PTRACE_SETOPTIONS
#endif

void set_using_sysemu(int value);
int get_using_sysemu(void);
extern int sysemu_supported;

#define SELECT_PTRACE_OPERATION(sysemu_mode, singlestep_mode) \
	(((int[3][3] ) { \
		{ PTRACE_SYSCALL, PTRACE_SYSCALL, PTRACE_SINGLESTEP }, \
		{ PTRACE_SYSEMU, PTRACE_SYSEMU, PTRACE_SINGLESTEP }, \
		{ PTRACE_SYSEMU, PTRACE_SYSEMU_SINGLESTEP, \
		  PTRACE_SYSEMU_SINGLESTEP } }) \
		[sysemu_mode][singlestep_mode])

#endif
