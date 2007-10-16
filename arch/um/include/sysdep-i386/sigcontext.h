/* 
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __SYS_SIGCONTEXT_I386_H
#define __SYS_SIGCONTEXT_I386_H

#include "sysdep/sc.h"

#define IP_RESTART_SYSCALL(ip) ((ip) -= 2)

#define GET_FAULTINFO_FROM_SC(fi,sc) \
	{ \
		(fi).cr2 = SC_CR2(sc); \
		(fi).error_code = SC_ERR(sc); \
		(fi).trap_no = SC_TRAPNO(sc); \
	}

/* This is Page Fault */
#define SEGV_IS_FIXABLE(fi)	((fi)->trap_no == 14)

/* SKAS3 has no trap_no on i386, but get_skas_faultinfo() sets it to 0. */
#define SEGV_MAYBE_FIXABLE(fi)	((fi)->trap_no == 0 && ptrace_faultinfo)

#endif
