#ifndef __SPARC_SIGNAL_H
#define __SPARC_SIGNAL_H

#include <asm/sigcontext.h>
#include <linux/compiler.h>

#ifdef __KERNEL__
#ifndef __ASSEMBLY__
#include <linux/personality.h>
#include <linux/types.h>
#endif
#endif

/* On the Sparc the signal handlers get passed a 'sub-signal' code
 * for certain signal types, which we document here.
 */
#define SIGHUP		 1
#define SIGINT		 2
#define SIGQUIT		 3
#define SIGILL		 4
#define    SUBSIG_STACK       0
#define    SUBSIG_ILLINST     2
#define    SUBSIG_PRIVINST    3
#define    SUBSIG_BADTRAP(t)  (0x80 + (t))

#define SIGTRAP		 5
#define SIGABRT		 6
#define SIGIOT		 6

#define SIGEMT           7
#define    SUBSIG_TAG    10

#define SIGFPE		 8
#define    SUBSIG_FPDISABLED     0x400
#define    SUBSIG_FPERROR        0x404
#define    SUBSIG_FPINTOVFL      0x001
#define    SUBSIG_FPSTSIG        0x002
#define    SUBSIG_IDIVZERO       0x014
#define    SUBSIG_FPINEXACT      0x0c4
#define    SUBSIG_FPDIVZERO      0x0c8
#define    SUBSIG_FPUNFLOW       0x0cc
#define    SUBSIG_FPOPERROR      0x0d0
#define    SUBSIG_FPOVFLOW       0x0d4

#define SIGKILL		 9
#define SIGBUS          10
#define    SUBSIG_BUSTIMEOUT    1
#define    SUBSIG_ALIGNMENT     2
#define    SUBSIG_MISCERROR     5

#define SIGSEGV		11
#define    SUBSIG_NOMAPPING     3
#define    SUBSIG_PROTECTION    4
#define    SUBSIG_SEGERROR      5

#define SIGSYS		12

#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGURG          16

/* SunOS values which deviate from the Linux/i386 ones */
#define SIGSTOP		17
#define SIGTSTP		18
#define SIGCONT		19
#define SIGCHLD		20
#define SIGTTIN		21
#define SIGTTOU		22
#define SIGIO		23
#define SIGPOLL		SIGIO   /* SysV name for SIGIO */
#define SIGXCPU		24
#define SIGXFSZ		25
#define SIGVTALRM	26
#define SIGPROF		27
#define SIGWINCH	28
#define SIGLOST		29
#define SIGPWR		SIGLOST
#define SIGUSR1		30
#define SIGUSR2		31

/* Most things should be clean enough to redefine this at will, if care
   is taken to make libc match.  */

#define __OLD_NSIG	32
#define __NEW_NSIG      64
#ifdef __arch64__
#define _NSIG_BPW       64
#else
#define _NSIG_BPW       32
#endif
#define _NSIG_WORDS     (__NEW_NSIG / _NSIG_BPW)

#define SIGRTMIN       32
#define SIGRTMAX       __NEW_NSIG

#if defined(__KERNEL__) || defined(__WANT_POSIX1B_SIGNALS__)
#define _NSIG			__NEW_NSIG
#define __new_sigset_t		sigset_t
#define __new_sigaction		sigaction
#define __new_sigaction32	sigaction32
#define __old_sigset_t		old_sigset_t
#define __old_sigaction		old_sigaction
#define __old_sigaction32	old_sigaction32
#else
#define _NSIG			__OLD_NSIG
#define NSIG			_NSIG
#define __old_sigset_t		sigset_t
#define __old_sigaction		sigaction
#define __old_sigaction32	sigaction32
#endif

#ifndef __ASSEMBLY__

typedef unsigned long __old_sigset_t;            /* at least 32 bits */

typedef struct {
       unsigned long sig[_NSIG_WORDS];
} __new_sigset_t;

/* A SunOS sigstack */
struct sigstack {
	/* XXX 32-bit pointers pinhead XXX */
	char *the_stack;
	int   cur_status;
};

/* Sigvec flags */
#define _SV_SSTACK    1u    /* This signal handler should use sig-stack */
#define _SV_INTR      2u    /* Sig return should not restart system call */
#define _SV_RESET     4u    /* Set handler to SIG_DFL upon taken signal */
#define _SV_IGNCHILD  8u    /* Do not send SIGCHLD */

/*
 * sa_flags values: SA_STACK is not currently supported, but will allow the
 * usage of signal stacks by using the (now obsolete) sa_restorer field in
 * the sigaction structure as a stack pointer. This is now possible due to
 * the changes in signal handling. LBT 010493.
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 */
#define SA_NOCLDSTOP	_SV_IGNCHILD
#define SA_STACK	_SV_SSTACK
#define SA_ONSTACK	_SV_SSTACK
#define SA_RESTART	_SV_INTR
#define SA_ONESHOT	_SV_RESET
#define SA_NOMASK	0x20u
#define SA_NOCLDWAIT    0x100u
#define SA_SIGINFO      0x200u


#define SIG_BLOCK          0x01	/* for blocking signals */
#define SIG_UNBLOCK        0x02	/* for unblocking signals */
#define SIG_SETMASK        0x04	/* for setting the signal mask */

/*
 * sigaltstack controls
 */
#define SS_ONSTACK	1
#define SS_DISABLE	2

#define MINSIGSTKSZ	4096
#define SIGSTKSZ	16384

#ifdef __KERNEL__
/*
 * DJHR
 * SA_STATIC_ALLOC is used for the sparc32 system to indicate that this
 * interrupt handler's irq structure should be statically allocated
 * by the request_irq routine.
 * The alternative is that arch/sparc/kernel/irq.c has carnal knowledge
 * of interrupt usage and that sucks. Also without a flag like this
 * it may be possible for the free_irq routine to attempt to free
 * statically allocated data.. which is NOT GOOD.
 *
 */
#define SA_STATIC_ALLOC         0x8000
#endif

#include <asm-generic/signal-defs.h>

struct __new_sigaction {
	__sighandler_t		sa_handler;
	unsigned long		sa_flags;
	__sigrestore_t		sa_restorer;  /* not used by Linux/SPARC yet */
	__new_sigset_t		sa_mask;
};

struct __old_sigaction {
	__sighandler_t		sa_handler;
	__old_sigset_t		sa_mask;
	unsigned long		sa_flags;
	void			(*sa_restorer)(void);  /* not used by Linux/SPARC yet */
};

typedef struct sigaltstack {
	void			__user *ss_sp;
	int			ss_flags;
	size_t			ss_size;
} stack_t;

#ifdef __KERNEL__

struct k_sigaction {
	struct			__new_sigaction sa;
	void			__user *ka_restorer;
};

#define ptrace_signal_deliver(regs, cookie) do { } while (0)

#endif /* !(__KERNEL__) */

#endif /* !(__ASSEMBLY__) */

#endif /* !(__SPARC_SIGNAL_H) */
