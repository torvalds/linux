#ifndef _PPC64_PPC32_H
#define _PPC64_PPC32_H

#include <linux/compat.h>
#include <asm/siginfo.h>
#include <asm/signal.h>

/*
 * Data types and macros for providing 32b PowerPC support.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* These are here to support 32-bit syscalls on a 64-bit kernel. */

typedef struct compat_siginfo {
	int si_signo;
	int si_errno;
	int si_code;

	union {
		int _pad[SI_PAD_SIZE32];

		/* kill() */
		struct {
			compat_pid_t _pid;		/* sender's pid */
			compat_uid_t _uid;		/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			compat_timer_t _tid;			/* timer id */
			int _overrun;			/* overrun count */
			compat_sigval_t _sigval;		/* same as below */
			int _sys_private;		/* not to be passed to user */
		} _timer;

		/* POSIX.1b signals */
		struct {
			compat_pid_t _pid;		/* sender's pid */
			compat_uid_t _uid;		/* sender's uid */
			compat_sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			compat_pid_t _pid;		/* which child */
			compat_uid_t _uid;		/* sender's uid */
			int _status;			/* exit code */
			compat_clock_t _utime;
			compat_clock_t _stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS, SIGEMT */
		struct {
			unsigned int _addr; /* faulting insn/memory ref. */
		} _sigfault;

		/* SIGPOLL */
		struct {
			int _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;
	} _sifields;
} compat_siginfo_t;

#define __old_sigaction32	old_sigaction32

struct __old_sigaction32 {
	compat_uptr_t		sa_handler;
	compat_old_sigset_t  	sa_mask;
	unsigned int    	sa_flags;
	compat_uptr_t		sa_restorer;     /* not used by Linux/SPARC yet */
};



struct sigaction32 {
       compat_uptr_t  sa_handler;	/* Really a pointer, but need to deal with 32 bits */
       unsigned int sa_flags;
       compat_uptr_t sa_restorer;	/* Another 32 bit pointer */
       compat_sigset_t sa_mask;		/* A 32 bit mask */
};

typedef struct sigaltstack_32 {
	unsigned int ss_sp;
	int ss_flags;
	compat_size_t ss_size;
} stack_32_t;

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
};

struct ucontext32 { 
	unsigned int	  	uc_flags;
	unsigned int 	  	uc_link;
	stack_32_t	 	uc_stack;
	int		 	uc_pad[7];
	compat_uptr_t		uc_regs;	/* points to uc_mcontext field */
	compat_sigset_t	 	uc_sigmask;	/* mask last for extensibility */
	/* glibc has 1024-bit signal masks, ours are 64-bit */
	int		 	uc_maskext[30];
	int		 	uc_pad2[3];
	struct mcontext32	uc_mcontext;
};

extern int copy_siginfo_to_user32(struct compat_siginfo __user *d, siginfo_t *s);

#endif  /* _PPC64_PPC32_H */
