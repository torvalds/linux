/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <asm/signal.h>
#include <asm/unistd.h>
#include "uml-config.h"
#include "sysdep/sigcontext.h"
#include "sysdep/faultinfo.h"

void __attribute__ ((__section__ (".__syscall_stub")))
stub_segv_handler(int sig)
{
	struct sigcontext *sc = (struct sigcontext *) (&sig + 1);

	GET_FAULTINFO_FROM_SC(*((struct faultinfo *) UML_CONFIG_STUB_DATA),
			      sc);

	__asm__("movl %0, %%eax ; int $0x80": : "g" (__NR_getpid));
	__asm__("movl %%eax, %%ebx ; movl %0, %%eax ; movl %1, %%ecx ;"
		"int $0x80": : "g" (__NR_kill), "g" (SIGUSR1));
	/* Load pointer to sigcontext into esp, since we need to leave
	 * the stack in its original form when we do the sigreturn here, by
	 * hand.
	 */
	__asm__("mov %0,%%esp ; movl %1, %%eax ; "
		"int $0x80" : : "a" (sc), "g" (__NR_sigreturn));
}
