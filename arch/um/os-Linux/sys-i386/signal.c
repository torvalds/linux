/*
 * Copyright (C) 2006 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <signal.h>

extern void (*handlers[])(int sig, struct sigcontext *sc);

void hard_handler(int sig)
{
	struct sigcontext *sc = (struct sigcontext *) (&sig + 1);

	(*handlers[sig])(sig, sc);
}
