/*
 * Copyright (C) 2004 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <signal.h>
#include "as-layout.h"
#include "sysdep/stub.h"
#include "sysdep/faultinfo.h"
#include "sysdep/sigcontext.h"

void __attribute__ ((__section__ (".__syscall_stub")))
stub_segv_handler(int sig)
{
	struct ucontext *uc;

	__asm__ __volatile__("movq %%rdx, %0" : "=g" (uc) :);
	GET_FAULTINFO_FROM_SC(*((struct faultinfo *) STUB_DATA),
			      &uc->uc_mcontext);
	trap_myself();
}

