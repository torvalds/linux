/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <signal.h>
#include "os.h"
#include "sysdep/ptrace.h"

/* Initialized from linux_main() */
void (*sig_info[NSIG])(int, struct uml_pt_regs *);

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
}
