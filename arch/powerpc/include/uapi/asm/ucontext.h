/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_POWERPC_UCONTEXT_H
#define _ASM_POWERPC_UCONTEXT_H

#ifdef __powerpc64__
#include <asm/sigcontext.h>
#else
#include <asm/elf.h>
#endif
#include <asm/signal.h>

#ifndef __powerpc64__
struct mcontext {
	elf_gregset_t	mc_gregs;
	elf_fpregset_t	mc_fregs;
	unsigned long	mc_pad[2];
	elf_vrregset_t	mc_vregs __attribute__((__aligned__(16)));
};
#endif

struct ucontext {
	unsigned long	uc_flags;
	struct ucontext __user *uc_link;
	stack_t		uc_stack;
#ifndef __powerpc64__
	int		uc_pad[7];
	struct mcontext	__user *uc_regs;/* points to uc_mcontext field */
#endif
	sigset_t	uc_sigmask;
	/* glibc has 1024-bit signal masks, ours are 64-bit */
#ifdef __powerpc64__
	sigset_t	__unused[15];	/* Allow for uc_sigmask growth */
	struct sigcontext uc_mcontext;	/* last for extensibility */
#else
	int		uc_maskext[30];
	int		uc_pad2[3];
	struct mcontext	uc_mcontext;
#endif
};

#endif /* _ASM_POWERPC_UCONTEXT_H */
