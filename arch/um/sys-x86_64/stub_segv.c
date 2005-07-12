/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <signal.h>
#include <linux/compiler.h>
#include <asm/unistd.h>
#include "uml-config.h"
#include "sysdep/sigcontext.h"
#include "sysdep/faultinfo.h"

void __attribute__ ((__section__ (".__syscall_stub")))
stub_segv_handler(int sig)
{
	struct ucontext *uc;

	__asm__("movq %%rdx, %0" : "=g" (uc) :);
        GET_FAULTINFO_FROM_SC(*((struct faultinfo *) UML_CONFIG_STUB_DATA),
                              &uc->uc_mcontext);

	__asm__("movq %0, %%rax ; syscall": : "g" (__NR_getpid));
	__asm__("movq %%rax, %%rdi ; movq %0, %%rax ; movq %1, %%rsi ;"
		"syscall": : "g" (__NR_kill), "g" (SIGUSR1));
	/* Two popqs to restore the stack to the state just before entering
	 * the handler, one pops the return address, the other pops the frame
	 * pointer.
	 */
	__asm__("popq %%rax ; popq %%rax ; movq %0, %%rax ; syscall" : : "g"
		(__NR_rt_sigreturn));
}
