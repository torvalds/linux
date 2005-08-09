/* 
 * Copyright (C) 2002 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <signal.h>
#include <errno.h>
#include "signal_user.h"
#include "user_util.h"
#include "kern_util.h"
#include "task.h"
#include "sigcontext.h"
#include "skas.h"
#include "ptrace_user.h"
#include "sysdep/ptrace.h"
#include "sysdep/ptrace_user.h"

void sig_handler_common_skas(int sig, void *sc_ptr)
{
	struct sigcontext *sc = sc_ptr;
	struct skas_regs *r;
	struct signal_info *info;
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
	info = &sig_info[sig];
	if(!info->is_irq) unblock_signals();

	(*info->handler)(sig, (union uml_pt_regs *) r);

	errno = save_errno;
	r->is_user = save_user;
}

extern int ptrace_faultinfo;

void user_signal(int sig, union uml_pt_regs *regs, int pid)
{
	struct signal_info *info;
        int segv = ((sig == SIGFPE) || (sig == SIGSEGV) || (sig == SIGBUS) ||
                    (sig == SIGILL) || (sig == SIGTRAP));

	if (segv)
		get_skas_faultinfo(pid, &regs->skas.faultinfo);
	info = &sig_info[sig];
	(*info->handler)(sig, regs);

	unblock_signals();
}

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
