/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Based on <asm-i386/siginfo.h>.
 *
 * Modified 1998-2002
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co
 */
#ifndef _UAPI_ASM_IA64_SIGINFO_H
#define _UAPI_ASM_IA64_SIGINFO_H


#define __ARCH_SI_PREAMBLE_SIZE	(4 * sizeof(int))

#define HAVE_ARCH_SIGINFO_T
#define HAVE_ARCH_COPY_SIGINFO_TO_USER

#include <asm-generic/siginfo.h>

typedef struct siginfo {
	int si_signo;
	int si_errno;
	int si_code;
	int __pad0;

	union {
		int _pad[SI_PAD_SIZE];

		/* kill() */
		struct {
			pid_t _pid;		/* sender's pid */
			uid_t _uid;		/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			timer_t _tid;		/* timer id */
			int _overrun;		/* overrun count */
			char _pad[sizeof(__ARCH_SI_UID_T) - sizeof(int)];
			sigval_t _sigval;	/* must overlay ._rt._sigval! */
			int _sys_private;	/* not to be passed to user */
		} _timer;

		/* POSIX.1b signals */
		struct {
			pid_t _pid;		/* sender's pid */
			uid_t _uid;		/* sender's uid */
			sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			pid_t _pid;		/* which child */
			uid_t _uid;		/* sender's uid */
			int _status;		/* exit code */
			clock_t _utime;
			clock_t _stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			void __user *_addr;	/* faulting insn/memory ref. */
			int _imm;		/* immediate value for "break" */
			unsigned int _flags;	/* see below */
			unsigned long _isr;	/* isr */
			short _addr_lsb;	/* lsb of faulting address */
			union {
				/* used when si_code=SEGV_BNDERR */
				struct {
					void __user *_lower;
					void __user *_upper;
				} _addr_bnd;
				/* used when si_code=SEGV_PKUERR */
				__u32 _pkey;
			};
		} _sigfault;

		/* SIGPOLL */
		struct {
			long _band;	/* POLL_IN, POLL_OUT, POLL_MSG (XPG requires a "long") */
			int _fd;
		} _sigpoll;
	} _sifields;
} siginfo_t;

#define si_imm		_sifields._sigfault._imm	/* as per UNIX SysV ABI spec */
#define si_flags	_sifields._sigfault._flags
/*
 * si_isr is valid for SIGILL, SIGFPE, SIGSEGV, SIGBUS, and SIGTRAP provided that
 * si_code is non-zero and __ISR_VALID is set in si_flags.
 */
#define si_isr		_sifields._sigfault._isr

/*
 * Flag values for si_flags:
 */
#define __ISR_VALID_BIT	0
#define __ISR_VALID	(1 << __ISR_VALID_BIT)

/*
 * SIGILL si_codes
 */
#define ILL_BADIADDR	9	/* unimplemented instruction address */
#define __ILL_BREAK	10	/* illegal break */
#define __ILL_BNDMOD	11	/* bundle-update (modification) in progress */
#undef NSIGILL
#define NSIGILL		11

/*
 * SIGFPE si_codes
 */
#ifdef __KERNEL__
#define FPE_FIXME	0	/* Broken dup of SI_USER */
#endif /* __KERNEL__ */
#define __FPE_DECOVF	9	/* decimal overflow */
#define __FPE_DECDIV	10	/* decimal division by zero */
#define __FPE_DECERR	11	/* packed decimal error */
#define __FPE_INVASC	12	/* invalid ASCII digit */
#define __FPE_INVDEC	13	/* invalid decimal digit */
#undef NSIGFPE
#define NSIGFPE		13

/*
 * SIGSEGV si_codes
 */
#define __SEGV_PSTKOVF	4	/* paragraph stack overflow */
#undef NSIGSEGV
#define NSIGSEGV	4

#undef NSIGTRAP
#define NSIGTRAP	4


#endif /* _UAPI_ASM_IA64_SIGINFO_H */
