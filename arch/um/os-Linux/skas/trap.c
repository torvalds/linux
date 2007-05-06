/*
 * Copyright (C) 2002 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <signal.h>
#include <errno.h>
#include "kern_util.h"
#include "as-layout.h"
#include "task.h"
#include "sigcontext.h"
#include "skas.h"
#include "ptrace_user.h"
#include "sysdep/ptrace.h"
#include "sysdep/ptrace_user.h"
#include "os.h"

static union uml_pt_regs ksig_regs[UM_NR_CPUS];

void sig_handler_common_skas(int sig, void *sc_ptr)
{
	struct sigcontext *sc = sc_ptr;
	union uml_pt_regs *r;
	void (*handler)(int, union uml_pt_regs *);
	int save_user, save_errno = errno;

	/* This is done because to allow SIGSEGV to be delivered inside a SEGV
	 * handler.  This can happen in copy_user, and if SEGV is disabled,
	 * the process will die.
	 * XXX Figure out why this is better than SA_NODEFER
	 */
	if(sig == SIGSEGV) {
		change_sig(SIGSEGV, 1);
		/* For segfaults, we want the data from the
		 * sigcontext.  In this case, we don't want to mangle
		 * the process registers, so use a static set of
		 * registers.  For other signals, the process
		 * registers are OK.
		 */
		r = &ksig_regs[cpu()];
		copy_sc(r, sc_ptr);
	}
	else r = TASK_REGS(get_current());

	save_user = r->skas.is_user;
	r->skas.is_user = 0;
	if ( sig == SIGFPE || sig == SIGSEGV ||
	     sig == SIGBUS || sig == SIGILL ||
	     sig == SIGTRAP ) {
		GET_FAULTINFO_FROM_SC(r->skas.faultinfo, sc);
	}

	change_sig(SIGUSR1, 1);

	handler = sig_info[sig];

	/* unblock SIGALRM, SIGVTALRM, SIGIO if sig isn't IRQ signal */
	if (sig != SIGIO && sig != SIGWINCH &&
	    sig != SIGVTALRM && sig != SIGALRM)
		unblock_signals();

	handler(sig, r);

	errno = save_errno;
	r->skas.is_user = save_user;
}

extern int ptrace_faultinfo;

void user_signal(int sig, union uml_pt_regs *regs, int pid)
{
	void (*handler)(int, union uml_pt_regs *);
	int segv = ((sig == SIGFPE) || (sig == SIGSEGV) || (sig == SIGBUS) ||
		    (sig == SIGILL) || (sig == SIGTRAP));

	if (segv)
		get_skas_faultinfo(pid, &regs->skas.faultinfo);

	handler = sig_info[sig];
	handler(sig, (union uml_pt_regs *) regs);

	unblock_signals();
}
