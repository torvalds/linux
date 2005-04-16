/* 
 * Copyright (C) 2002 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <signal.h>
#include <errno.h>
#include "sysdep/ptrace.h"
#include "signal_user.h"
#include "user_util.h"
#include "kern_util.h"
#include "task.h"
#include "sigcontext.h"

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
	r->fault_addr = SC_FAULT_ADDR(sc);
	r->fault_type = SC_FAULT_TYPE(sc);
	r->trap_type = SC_TRAP_TYPE(sc);

	change_sig(SIGUSR1, 1);
	info = &sig_info[sig];
	if(!info->is_irq) unblock_signals();

	(*info->handler)(sig, (union uml_pt_regs *) r);

	errno = save_errno;
	r->is_user = save_user;
}

void user_signal(int sig, union uml_pt_regs *regs)
{
	struct signal_info *info;

	regs->skas.is_user = 1;
	regs->skas.fault_addr = 0;
	regs->skas.fault_type = 0;
	regs->skas.trap_type = 0;
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
