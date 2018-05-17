/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _PARISC_SIGINFO_H
#define _PARISC_SIGINFO_H

#if defined(__LP64__)
#define __ARCH_SI_PREAMBLE_SIZE   (4 * sizeof(int))
#endif

#include <asm-generic/siginfo.h>

/*
 * SIGFPE si_codes
 */
#ifdef __KERNEL__
#define FPE_FIXME	0	/* Broken dup of SI_USER */
#endif /* __KERNEL__ */

#endif
