/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __SYS_SIGCONTEXT_I386_H
#define __SYS_SIGCONTEXT_I386_H

#include "uml-config.h"
#include <sysdep/sc.h>

#define IP_RESTART_SYSCALL(ip) ((ip) -= 2)

#define SC_RESTART_SYSCALL(sc) IP_RESTART_SYSCALL(SC_IP(sc))
#define SC_SET_SYSCALL_RETURN(sc, result) SC_EAX(sc) = (result)

#define GET_FAULTINFO_FROM_SC(fi,sc) \
	{ \
		(fi).cr2 = SC_CR2(sc); \
		(fi).error_code = SC_ERR(sc); \
		(fi).trap_no = SC_TRAPNO(sc); \
	}

/* ptrace expects that, at the start of a system call, %eax contains
 * -ENOSYS, so this makes it so.
 */
#define SC_START_SYSCALL(sc) do SC_EAX(sc) = -ENOSYS; while(0)

/* This is Page Fault */
#define SEGV_IS_FIXABLE(fi)	((fi)->trap_no == 14)

/* SKAS3 has no trap_no on i386, but get_skas_faultinfo() sets it to 0. */
#define SEGV_MAYBE_FIXABLE(fi)	((fi)->trap_no == 0 && ptrace_faultinfo)

extern unsigned long *sc_sigmask(void *sc_ptr);
extern int sc_get_fpregs(unsigned long buf, void *sc_ptr);

#endif
/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
