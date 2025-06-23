/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_GENERIC_SIGNAL_DEFS_H
#define __ASM_GENERIC_SIGNAL_DEFS_H



/*
 * SA_FLAGS values:
 *
 * SA_NOCLDSTOP flag to turn off SIGCHLD when children stop.
 * SA_NOCLDWAIT flag on SIGCHLD to inhibit zombies.
 * SA_SIGINFO delivers the signal with SIGINFO structs.
 * SA_ONSTACK indicates that a registered stack_t will be used.
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 * SA_NODEFER prevents the current signal from being masked in the handler.
 * SA_RESETHAND clears the handler when the signal is delivered.
 * SA_UNSUPPORTED is a flag bit that will never be supported. Kernels from
 * before the introduction of SA_UNSUPPORTED did not clear unknown bits from
 * sa_flags when read using the oldact argument to sigaction and rt_sigaction,
 * so this bit allows flag bit support to be detected from userspace while
 * allowing an old kernel to be distinguished from a kernel that supports every
 * flag bit.
 * SA_EXPOSE_TAGBITS exposes an architecture-defined set of tag bits in
 * siginfo.si_addr.
 *
 * SA_ONESHOT and SA_NOMASK are the historical Linux names for the Single
 * Unix names RESETHAND and NODEFER respectively.
 */
#ifndef SA_NOCLDSTOP
#define SA_NOCLDSTOP	0x00000001
#endif
#ifndef SA_NOCLDWAIT
#define SA_NOCLDWAIT	0x00000002
#endif
#ifndef SA_SIGINFO
#define SA_SIGINFO	0x00000004
#endif
/* 0x00000008 used on alpha, mips, parisc */
/* 0x00000010 used on alpha, parisc */
/* 0x00000020 used on alpha, parisc, sparc */
/* 0x00000040 used on alpha, parisc */
/* 0x00000080 used on parisc */
/* 0x00000100 used on sparc */
/* 0x00000200 used on sparc */
#define SA_UNSUPPORTED	0x00000400
#define SA_EXPOSE_TAGBITS	0x00000800
/* 0x00010000 used on mips */
/* 0x01000000 used on x86 */
/* 0x02000000 used on x86 */
/*
 * New architectures should not define the obsolete
 *	SA_RESTORER	0x04000000
 */
#ifndef SA_ONSTACK
#define SA_ONSTACK	0x08000000
#endif
#ifndef SA_RESTART
#define SA_RESTART	0x10000000
#endif
#ifndef SA_NODEFER
#define SA_NODEFER	0x40000000
#endif
#ifndef SA_RESETHAND
#define SA_RESETHAND	0x80000000
#endif

#define SA_NOMASK	SA_NODEFER
#define SA_ONESHOT	SA_RESETHAND

#ifndef SIG_BLOCK
#define SIG_BLOCK          0	/* for blocking signals */
#endif
#ifndef SIG_UNBLOCK
#define SIG_UNBLOCK        1	/* for unblocking signals */
#endif
#ifndef SIG_SETMASK
#define SIG_SETMASK        2	/* for setting the signal mask */
#endif

#ifndef __ASSEMBLY__
typedef void __signalfn_t(int);
typedef __signalfn_t *__sighandler_t;

typedef void __restorefn_t(void);
typedef __restorefn_t *__sigrestore_t;

#define SIG_DFL	((__sighandler_t)0)	/* default signal handling */
#define SIG_IGN	((__sighandler_t)1)	/* ignore signal */
#define SIG_ERR	((__sighandler_t)-1)	/* error return from signal */
#endif

#endif /* __ASM_GENERIC_SIGNAL_DEFS_H */
