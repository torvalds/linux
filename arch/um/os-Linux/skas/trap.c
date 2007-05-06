/*
 * Copyright (C) 2002 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <signal.h>
#include <errno.h>
#include "user_util.h"
#include "kern_util.h"
#include "as-layout.h"
#include "task.h"
#include "sigcontext.h"
#include "skas.h"
#include "ptrace_user.h"
#include "sysdep/ptrace.h"
#include "sysdep/ptrace_user.h"
#include "os.h"

void sig_handler_common_skas(int sig, void *sc_ptr)
{
	struct sigcontext *sc = sc_ptr;
	struct skas_regs *r;
	void (*handler)(int, union uml_pt_regs *);
	int save_errno = errno;
	int save_user;

	/* This is done because to allow SIGSEGV to be delivered inside a SEGV
	 * handler.  This can happen in copy_user, and if SEGV is disabled,
	 * the process will die.
	 * XXX Figure out why this is better than SA_NODEFER
	 */
	if(sig == SIGSEGV)
		change_sig(SIGSEGV, 1);

	r = &TASK_REGS(get_current())->skas;
	save_user = r->is_user;
	r->is_user = 0;
	if ( sig == SIGFPE || sig == SIGSEGV ||
	     sig == SIGBUS || sig == SIGILL ||
	     sig == SIGTRAP ) {
		GET_FAULTINFO_FROM_SC(r->faultinfo, sc);
	}

	change_sig(SIGUSR1, 1);

	handler = sig_info[sig];

	/* unblock SIGALRM, SIGVTALRM, SIGIO if sig isn't IRQ signal */
	if (sig != SIGIO && sig != SIGWINCH &&
	    sig != SIGVTALRM && sig != SIGALRM)
		unblock_signals();

	handler(sig, (union uml_pt_regs *) r);

	errno = save_errno;
	r->is_user = save_user;
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
