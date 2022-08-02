/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Modified 1998-2001, 2003
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co
 *
 * Unfortunately, this file is being included by bits/signal.h in
 * glibc-2.x.  Hence the #ifdef __KERNEL__ ugliness.
 */
#ifndef _UAPI_ASM_IA64_SIGNAL_H
#define _UAPI_ASM_IA64_SIGNAL_H


#define SIGHUP		 1
#define SIGINT		 2
#define SIGQUIT		 3
#define SIGILL		 4
#define SIGTRAP		 5
#define SIGABRT		 6
#define SIGIOT		 6
#define SIGBUS		 7
#define SIGFPE		 8
#define SIGKILL		 9
#define SIGUSR1		10
#define SIGSEGV		11
#define SIGUSR2		12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGSTKFLT	16
#define SIGCHLD		17
#define SIGCONT		18
#define SIGSTOP		19
#define SIGTSTP		20
#define SIGTTIN		21
#define SIGTTOU		22
#define SIGURG		23
#define SIGXCPU		24
#define SIGXFSZ		25
#define SIGVTALRM	26
#define SIGPROF		27
#define SIGWINCH	28
#define SIGIO		29
#define SIGPOLL		SIGIO
/*
#define SIGLOST		29
*/
#define SIGPWR		30
#define SIGSYS		31
/* signal 31 is no longer "unused", but the SIGUNUSED macro remains for backwards compatibility */
#define	SIGUNUSED	31

/* These should not be considered constants from userland.  */
#define SIGRTMIN	32
#define SIGRTMAX	_NSIG

#define SA_RESTORER	0x04000000

/*
 * The minimum stack size needs to be fairly large because we want to
 * be sure that an app compiled for today's CPUs will continue to run
 * on all future CPU models.  The CPU model matters because the signal
 * frame needs to have space for the complete machine state, including
 * all physical stacked registers.  The number of physical stacked
 * registers is CPU model dependent, but given that the width of
 * ar.rsc.loadrs is 14 bits, we can assume that they'll never take up
 * more than 16KB of space.
 */
#if 1
  /*
   * This is a stupid typo: the value was _meant_ to be 131072 (0x20000), but I typed it
   * in wrong. ;-(  To preserve backwards compatibility, we leave the kernel at the
   * incorrect value and fix libc only.
   */
# define MINSIGSTKSZ	131027	/* min. stack size for sigaltstack() */
#else
# define MINSIGSTKSZ	131072	/* min. stack size for sigaltstack() */
#endif
#define SIGSTKSZ	262144	/* default stack size for sigaltstack() */


#include <asm-generic/signal-defs.h>

# ifndef __ASSEMBLY__

#  include <linux/types.h>

/* Avoid too many header ordering problems.  */
struct siginfo;

typedef struct sigaltstack {
	void __user *ss_sp;
	int ss_flags;
	__kernel_size_t ss_size;
} stack_t;


# endif /* !__ASSEMBLY__ */
#endif /* _UAPI_ASM_IA64_SIGNAL_H */
