/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_POWERPC_SIGNAL_H
#define _UAPI_ASM_POWERPC_SIGNAL_H

#include <linux/types.h>

#define _NSIG		64
#ifdef __powerpc64__
#define _NSIG_BPW	64
#else
#define _NSIG_BPW	32
#endif
#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

typedef unsigned long old_sigset_t;		/* at least 32 bits */

typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

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
#define	SIGUNUSED	31

/* These should not be considered constants from userland.  */
#define SIGRTMIN	32
#define SIGRTMAX	_NSIG

#define SA_RESTORER	0x04000000U

#define MINSIGSTKSZ	2048
#define SIGSTKSZ	8192

#include <asm-generic/signal-defs.h>

#ifndef __KERNEL__
struct old_sigaction {
	__sighandler_t sa_handler;
	old_sigset_t sa_mask;
	unsigned long sa_flags;
	__sigrestore_t sa_restorer;
};

struct sigaction {
	__sighandler_t sa_handler;
	unsigned long sa_flags;
	__sigrestore_t sa_restorer;
	sigset_t sa_mask;		/* mask last for extensibility */
};
#endif

typedef struct sigaltstack {
	void __user *ss_sp;
	int ss_flags;
	__kernel_size_t ss_size;
} stack_t;


#ifndef __powerpc64__
/*
 * These are parameters to dbg_sigreturn syscall.  They enable or
 * disable certain debugging things that can be done from signal
 * handlers.  The dbg_sigreturn syscall *must* be called from a
 * SA_SIGINFO signal so the ucontext can be passed to it.  It takes an
 * array of struct sig_dbg_op, which has the debug operations to
 * perform before returning from the signal.
 */
struct sig_dbg_op {
	int dbg_type;
	unsigned long dbg_value;
};

/* Enable or disable single-stepping.  The value sets the state. */
#define SIG_DBG_SINGLE_STEPPING		1

/* Enable or disable branch tracing.  The value sets the state. */
#define SIG_DBG_BRANCH_TRACING		2
#endif /* ! __powerpc64__ */

#endif /* _UAPI_ASM_POWERPC_SIGNAL_H */
