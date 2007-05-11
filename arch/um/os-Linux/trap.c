/*
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <signal.h>
#include "kern_util.h"
#include "os.h"
#include "mode.h"
#include "longjmp.h"

void usr2_handler(int sig, union uml_pt_regs *regs)
{
	CHOOSE_MODE(syscall_handler_tt(sig, regs), (void) 0);
}

/* Initialized from linux_main() */
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

void do_longjmp(void *b, int val)
{
	jmp_buf *buf = b;

	UML_LONGJMP(buf, val);
}
