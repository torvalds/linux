/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#ifndef __SYSDEP_X86_64_SIGCONTEXT_H
#define __SYSDEP_X86_64_SIGCONTEXT_H

#include <sysdep/sc.h>

#define IP_RESTART_SYSCALL(ip) ((ip) -= 2)

#define GET_FAULTINFO_FROM_SC(fi, sc) \
	{ \
		(fi).cr2 = SC_CR2(sc); \
		(fi).error_code = SC_ERR(sc); \
		(fi).trap_no = SC_TRAPNO(sc); \
	}

/* This is Page Fault */
#define SEGV_IS_FIXABLE(fi)	((fi)->trap_no == 14)

/* No broken SKAS API, which doesn't pass trap_no, here. */
#define SEGV_MAYBE_FIXABLE(fi)	0

#endif
