/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_SIGNAL_H
#define _UAPI_ASM_SIGNAL_H

#include <linux/types.h>

#ifndef __KERNEL__
/* Here we must cater to libcs that poke about in kernel headers.  */

#define NSIG		32
typedef unsigned long sigset_t;

#endif /* !__KERNEL__ */

#define SA_RESTORER	0x04000000 /* to get struct sigaction correct */

#include <asm-generic/signal.h>

#ifndef __KERNEL__
/* Here we must cater to libcs that poke about in kernel headers.  */

struct sigaction {
	union {
	  __sighandler_t _sa_handler;
	  void (*_sa_sigaction)(int, struct siginfo *, void *);
	} _u;
	sigset_t sa_mask;
	unsigned long sa_flags;
	void (*sa_restorer)(void);
};

#define sa_handler	_u._sa_handler
#define sa_sigaction	_u._sa_sigaction

#endif /* __KERNEL__ */

#endif /* _UAPI_ASM_SIGNAL_H */
