/*
 * Copyright (C) 2006 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <signal.h>

extern void handle_signal(int sig, struct sigcontext *sc);

void hard_handler(int sig)
{
	struct ucontext *uc;
	asm("movq %%rdx, %0" : "=r" (uc));

	handle_signal(sig, (struct sigcontext *) &uc->uc_mcontext);
}
