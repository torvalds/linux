/*
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <asm/page.h>
#include <asm/unistd.h>
#include <asm/ptrace.h>
#include "init.h"
#include "sysdep/ptrace.h"
#include "sigcontext.h"
#include "sysdep/sigcontext.h"
#include "irq_user.h"
#include "time_user.h"
#include "task.h"
#include "mode.h"
#include "choose-mode.h"
#include "kern_util.h"
#include "user_util.h"
#include "os.h"

void segv_handler(int sig, union uml_pt_regs *regs)
{
        struct faultinfo * fi = UPT_FAULTINFO(regs);

        if(UPT_IS_USER(regs) && !SEGV_IS_FIXABLE(fi)){
                bad_segv(*fi, UPT_IP(regs));
		return;
	}
        segv(*fi, UPT_IP(regs), UPT_IS_USER(regs), regs);
}

void usr2_handler(int sig, union uml_pt_regs *regs)
{
	CHOOSE_MODE(syscall_handler_tt(sig, regs), (void) 0);
}

void (*sig_info[NSIG])(int, union uml_pt_regs *);

void os_fill_handlinfo(struct kern_handlers h)
{
	sig_info[SIGTRAP] = h.relay_signal;
	sig_info[SIGFPE] = h.relay_signal;
	sig_info[SIGILL] = h.relay_signal;
	sig_info[SIGWINCH] = h.winch;
	sig_info[SIGBUS] = h.bus_handler;
	sig_info[SIGSEGV] = h.page_fault;
	sig_info[SIGIO] = h.sigio_handler;
	sig_info[SIGVTALRM] = h.timer_handler;
	sig_info[SIGALRM] = h.timer_handler;
	sig_info[SIGUSR2] = usr2_handler;
}
