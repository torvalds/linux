/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <signal.h>
#include <sys/select.h> /* The only way I can see to get sigset_t */
#include <asm/unistd.h>
#include "uml-config.h"
#include "sysdep/stub.h"
#include "sysdep/sigcontext.h"
#include "sysdep/faultinfo.h"

void __attribute__ ((__section__ (".__syscall_stub")))
stub_segv_handler(int sig)
{
	struct sigcontext *sc = (struct sigcontext *) (&sig + 1);
	int pid;

	GET_FAULTINFO_FROM_SC(*((struct faultinfo *) UML_CONFIG_STUB_DATA),
			      sc);

	pid = stub_syscall0(__NR_getpid);
	stub_syscall2(__NR_kill, pid, SIGUSR1);

	/* Load pointer to sigcontext into esp, since we need to leave
	 * the stack in its original form when we do the sigreturn here, by
	 * hand.
	 */
	__asm__ __volatile__("mov %0,%%esp ; movl %1, %%eax ; "
			     "int $0x80" : : "a" (sc), "g" (__NR_sigreturn));
}
