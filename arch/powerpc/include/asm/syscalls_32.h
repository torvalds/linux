/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_SYSCALLS_32_H
#define _ASM_POWERPC_SYSCALLS_32_H

#include <linux/compat.h>
#include <asm/siginfo.h>
#include <asm/signal.h>

/*
 * Data types and macros for providing 32b PowerPC support.
 */

/* These are here to support 32-bit syscalls on a 64-bit kernel. */

struct pt_regs32 {
	unsigned int gpr[32];
	unsigned int nip;
	unsigned int msr;
	unsigned int orig_gpr3;		/* Used for restarting system calls */
	unsigned int ctr;
	unsigned int link;
	unsigned int xer;
	unsigned int ccr;
	unsigned int mq;		/* 601 only (not used at present) */
	unsigned int trap;		/* Reason for being here */
	unsigned int dar;		/* Fault registers */
	unsigned int dsisr;
	unsigned int result;		/* Result of a system call */
};

struct sigcontext32 {
	unsigned int	_unused[4];
	int		signal;
	compat_uptr_t	handler;
	unsigned int	oldmask;
	compat_uptr_t	regs;  /* 4 byte pointer to the pt_regs32 structure. */
};

struct mcontext32 {
	elf_gregset_t32		mc_gregs;
	elf_fpregset_t		mc_fregs;
	unsigned int		mc_pad[2];
	elf_vrregset_t32	mc_vregs __attribute__((__aligned__(16)));
	elf_vsrreghalf_t32      mc_vsregs __attribute__((__aligned__(16)));
};

struct ucontext32 { 
	unsigned int	  	uc_flags;
	unsigned int 	  	uc_link;
	compat_stack_t	 	uc_stack;
	int		 	uc_pad[7];
	compat_uptr_t		uc_regs;	/* points to uc_mcontext field */
	compat_sigset_t	 	uc_sigmask;	/* mask last for extensibility */
	/* glibc has 1024-bit signal masks, ours are 64-bit */
	int		 	uc_maskext[30];
	int		 	uc_pad2[3];
	struct mcontext32	uc_mcontext;
};

#endif  // _ASM_POWERPC_SYSCALLS_32_H
