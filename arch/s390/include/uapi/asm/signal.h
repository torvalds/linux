/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  S390 version
 *
 *  Derived from "include/asm-i386/signal.h"
 */

#ifndef _UAPI_ASMS390_SIGNAL_H
#define _UAPI_ASMS390_SIGNAL_H

#include <linux/types.h>
#include <linux/time.h>

/* Avoid too many header ordering problems.  */
struct siginfo;
struct pt_regs;

#ifndef __KERNEL__
/* Here we must cater to libcs that poke about in kernel headers.  */

#define NSIG            32
typedef unsigned long sigset_t;

#endif /* __KERNEL__ */

#define SIGHUP           1
#define SIGINT           2
#define SIGQUIT          3
#define SIGILL           4
#define SIGTRAP          5
#define SIGABRT          6
#define SIGIOT           6
#define SIGBUS           7
#define SIGFPE           8
#define SIGKILL          9
#define SIGUSR1         10
#define SIGSEGV         11
#define SIGUSR2         12
#define SIGPIPE         13
#define SIGALRM         14
#define SIGTERM         15
#define SIGSTKFLT       16
#define SIGCHLD         17
#define SIGCONT         18
#define SIGSTOP         19
#define SIGTSTP         20
#define SIGTTIN         21
#define SIGTTOU         22
#define SIGURG          23
#define SIGXCPU         24
#define SIGXFSZ         25
#define SIGVTALRM       26
#define SIGPROF         27
#define SIGWINCH        28
#define SIGIO           29
#define SIGPOLL         SIGIO
/*
#define SIGLOST         29
*/
#define SIGPWR          30
#define SIGSYS		31
#define SIGUNUSED       31

/* These should not be considered constants from userland.  */
#define SIGRTMIN        32
#define SIGRTMAX        _NSIG

/*
 * SA_FLAGS values:
 *
 * SA_ONSTACK indicates that a registered stack_t will be used.
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 * SA_NOCLDSTOP flag to turn off SIGCHLD when children stop.
 * SA_RESETHAND clears the handler when the signal is delivered.
 * SA_NOCLDWAIT flag on SIGCHLD to inhibit zombies.
 * SA_NODEFER prevents the current signal from being masked in the handler.
 *
 * SA_ONESHOT and SA_NOMASK are the historical Linux names for the Single
 * Unix names RESETHAND and NODEFER respectively.
 */
#define SA_NOCLDSTOP    0x00000001
#define SA_NOCLDWAIT    0x00000002
#define SA_SIGINFO      0x00000004
#define SA_ONSTACK      0x08000000
#define SA_RESTART      0x10000000
#define SA_NODEFER      0x40000000
#define SA_RESETHAND    0x80000000

#define SA_NOMASK       SA_NODEFER
#define SA_ONESHOT      SA_RESETHAND

#define SA_RESTORER     0x04000000

#define MINSIGSTKSZ     2048
#define SIGSTKSZ        8192

#include <asm-generic/signal-defs.h>

#ifndef __KERNEL__

/*
 * There are two system calls in regard to sigaction, sys_rt_sigaction
 * and sys_sigaction. Internally the kernel uses the struct old_sigaction
 * for the older sys_sigaction system call, and the kernel version of the
 * struct sigaction for the newer sys_rt_sigaction.
 *
 * The uapi definition for struct sigaction has made a strange distinction
 * between 31-bit and 64-bit in the past. For 64-bit the uapi structure
 * looks like the kernel struct sigaction, but for 31-bit it used to
 * look like the kernel struct old_sigaction. That practically made the
 * structure unusable for either system call. To get around this problem
 * the glibc always had its own definitions for the sigaction structures.
 *
 * The current struct sigaction uapi definition below is suitable for the
 * sys_rt_sigaction system call only.
 */
struct sigaction {
        union {
          __sighandler_t _sa_handler;
          void (*_sa_sigaction)(int, struct siginfo *, void *);
        } _u;
        unsigned long sa_flags;
        void (*sa_restorer)(void);
	sigset_t sa_mask;
};

#define sa_handler      _u._sa_handler
#define sa_sigaction    _u._sa_sigaction

#endif /* __KERNEL__ */

typedef struct sigaltstack {
        void __user *ss_sp;
        int ss_flags;
        size_t ss_size;
} stack_t;


#endif /* _UAPI_ASMS390_SIGNAL_H */
