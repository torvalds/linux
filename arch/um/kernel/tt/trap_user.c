/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include "sysdep/ptrace.h"
#include "sysdep/sigcontext.h"
#include "signal_user.h"
#include "user_util.h"
#include "kern_util.h"
#include "task.h"
#include "tt.h"

void sig_handler_common_tt(int sig, void *sc_ptr)
{
	struct sigcontext *sc = sc_ptr;
	struct tt_regs save_regs, *r;
	struct signal_info *info;
	int save_errno = errno, is_user;

	/* This is done because to allow SIGSEGV to be delivered inside a SEGV
	 * handler.  This can happen in copy_user, and if SEGV is disabled,
	 * the process will die.
	 */
	if(sig == SIGSEGV)
		change_sig(SIGSEGV, 1);

	r = &TASK_REGS(get_current())->tt;
        if ( sig == SIGFPE || sig == SIGSEGV ||
             sig == SIGBUS || sig == SIGILL ||
             sig == SIGTRAP ) {
                GET_FAULTINFO_FROM_SC(r->faultinfo, sc);
        }
	save_regs = *r;
	is_user = user_context(SC_SP(sc));
	r->sc = sc;
	if(sig != SIGUSR2) 
		r->syscall = -1;

	info = &sig_info[sig];
	if(!info->is_irq) unblock_signals();

	(*info->handler)(sig, (union uml_pt_regs *) r);

	if(is_user){
		interrupt_end();
		block_signals();
		set_user_mode(NULL);
	}
	*r = save_regs;
	errno = save_errno;
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
