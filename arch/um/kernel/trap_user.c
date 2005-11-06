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
#include "signal_user.h"
#include "time_user.h"
#include "task.h"
#include "mode.h"
#include "choose-mode.h"
#include "kern_util.h"
#include "user_util.h"
#include "os.h"

void kill_child_dead(int pid)
{
	kill(pid, SIGKILL);
	kill(pid, SIGCONT);
	do {
		int n;
		CATCH_EINTR(n = waitpid(pid, NULL, 0));
		if (n > 0)
			kill(pid, SIGCONT);
		else
			break;
	} while(1);
}

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

struct signal_info sig_info[] = {
	[ SIGTRAP ] { .handler 		= relay_signal,
		      .is_irq 		= 0 },
	[ SIGFPE ] { .handler 		= relay_signal,
		     .is_irq 		= 0 },
	[ SIGILL ] { .handler 		= relay_signal,
		     .is_irq 		= 0 },
	[ SIGWINCH ] { .handler		= winch,
		       .is_irq		= 1 },
	[ SIGBUS ] { .handler 		= bus_handler,
		     .is_irq 		= 0 },
	[ SIGSEGV] { .handler 		= segv_handler,
		     .is_irq 		= 0 },
	[ SIGIO ] { .handler 		= sigio_handler,
		    .is_irq 		= 1 },
	[ SIGVTALRM ] { .handler 	= timer_handler,
			.is_irq 	= 1 },
        [ SIGALRM ] { .handler          = timer_handler,
                      .is_irq           = 1 },
	[ SIGUSR2 ] { .handler 		= usr2_handler,
		      .is_irq 		= 0 },
};

void do_longjmp(void *b, int val)
{
	sigjmp_buf *buf = b;

	siglongjmp(*buf, val);
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
