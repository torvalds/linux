/*
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <asm/signal.h>
#include <linux/compiler.h>
#include <asm/unistd.h>
#include <asm/ucontext.h>
#include "uml-config.h"
#include "sysdep/sigcontext.h"
#include "sysdep/faultinfo.h"
#include <stddef.h>

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

	__asm__("movq %%rdx, %0" : "=g" (uc) :);
	GET_FAULTINFO_FROM_SC(*((struct faultinfo *) UML_CONFIG_STUB_DATA),
			      &uc->uc_mcontext);

	__asm__("movq %0, %%rax ; syscall": : "g" (__NR_getpid));	
	__asm__("movq %%rax, %%rdi ; movq %0, %%rax ; movq %1, %%rsi ;"
		"syscall": : "g" (__NR_kill), "g" (SIGUSR1) : 
		"%rdi", "%rax", "%rsi");
	/* sys_sigreturn expects that the stack pointer will be 8 bytes into
	 * the signal frame.  So, we use the ucontext pointer, which we know
	 * already, to get the signal frame pointer, and add 8 to that.
	 */
	__asm__("movq %0, %%rsp": : 
		"g" ((unsigned long) container_of(uc, struct rt_sigframe, 
						  uc) + 8));
	__asm__("movq %0, %%rax ; syscall" : : "g" (__NR_rt_sigreturn));
}
