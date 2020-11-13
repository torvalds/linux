/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_SIGNAL_H
#define _ASM_PARISC_SIGNAL_H

#include <uapi/asm/signal.h>

#define _NSIG		64
/* bits-per-word, where word apparently means 'long' not 'int' */
#define _NSIG_BPW	BITS_PER_LONG
#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

# ifndef __ASSEMBLY__

/* Most things should be clean enough to redefine this at will, if care
   is taken to make libc match.  */

typedef unsigned long old_sigset_t;		/* at least 32 bits */

typedef struct {
	/* next_signal() assumes this is a long - no choice */
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

#define __ARCH_UAPI_SA_FLAGS	_SA_SIGGFAULT

#include <asm/sigcontext.h>

#endif /* !__ASSEMBLY */
#endif /* _ASM_PARISC_SIGNAL_H */
