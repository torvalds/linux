/*
 *  S390 version
 *
 *  Derived from "include/asm-i386/signal.h"
 */
#ifndef _ASMS390_SIGNAL_H
#define _ASMS390_SIGNAL_H

#include <uapi/asm/signal.h>

/* Most things should be clean enough to redefine this at will, if care
   is taken to make libc match.  */
#include <asm/sigcontext.h>
#define _NSIG           _SIGCONTEXT_NSIG
#define _NSIG_BPW       _SIGCONTEXT_NSIG_BPW
#define _NSIG_WORDS     _SIGCONTEXT_NSIG_WORDS

typedef unsigned long old_sigset_t;             /* at least 32 bits */

typedef struct {
        unsigned long sig[_NSIG_WORDS];
} sigset_t;

struct old_sigaction {
        __sighandler_t sa_handler;
        old_sigset_t sa_mask;
        unsigned long sa_flags;
        void (*sa_restorer)(void);
};

struct sigaction {
        __sighandler_t sa_handler;
        unsigned long sa_flags;
        void (*sa_restorer)(void);
        sigset_t sa_mask;               /* mask last for extensibility */
};

#endif
