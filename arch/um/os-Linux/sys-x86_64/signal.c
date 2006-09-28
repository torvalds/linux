/*
 * Copyright (C) 2006 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <signal.h>

extern void (*handlers[])(int sig, struct sigcontext *sc);

void hard_handler(int sig)
{
	struct ucontext *uc;
	asm("movq %%rdx, %0" : "=r" (uc));

	(*handlers[sig])(sig, (struct sigcontext *) &uc->uc_mcontext);
}
