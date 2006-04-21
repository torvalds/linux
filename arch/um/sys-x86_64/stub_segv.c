/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <stddef.h>
#include <signal.h>
#include <linux/compiler.h>
#include <asm/unistd.h>
#include "uml-config.h"
#include "sysdep/sigcontext.h"
#include "sysdep/faultinfo.h"
#include "sysdep/stub.h"

/* Copied from sys-x86_64/signal.c - Can't find an equivalent definition
 * in the libc headers anywhere.
 */
struct rt_sigframe
{
	char *pretcode;
	struct ucontext uc;
	struct siginfo info;
};

/* Copied here from <linux/kernel.h> - we're userspace. */
#define container_of(ptr, type, member) ({                   \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})

void __attribute__ ((__section__ (".__syscall_stub")))
stub_segv_handler(int sig)
{
	struct ucontext *uc;
        int pid;

	__asm__ __volatile__("movq %%rdx, %0" : "=g" (uc) :);
	GET_FAULTINFO_FROM_SC(*((struct faultinfo *) UML_CONFIG_STUB_DATA),
			      &uc->uc_mcontext);

	pid = stub_syscall0(__NR_getpid);
	stub_syscall2(__NR_kill, pid, SIGUSR1);

	/* sys_sigreturn expects that the stack pointer will be 8 bytes into
	 * the signal frame.  So, we use the ucontext pointer, which we know
	 * already, to get the signal frame pointer, and add 8 to that.
	 */
	__asm__ __volatile__("movq %0, %%rsp; movq %1, %%rax ; syscall": :
                             "g" ((unsigned long)
                                  container_of(uc, struct rt_sigframe, uc) + 8),
                             "g" (__NR_rt_sigreturn));
}
