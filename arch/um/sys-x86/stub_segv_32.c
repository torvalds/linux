/*
 * Copyright (C) 2004 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include "sysdep/stub.h"
#include "sysdep/sigcontext.h"

void __attribute__ ((__section__ (".__syscall_stub")))
stub_segv_handler(int sig)
{
	struct sigcontext *sc = (struct sigcontext *) (&sig + 1);

	GET_FAULTINFO_FROM_SC(*((struct faultinfo *) STUB_DATA), sc);

	trap_myself();
}
