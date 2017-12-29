/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ALPHA_SIGINFO_H
#define _ALPHA_SIGINFO_H

#define __ARCH_SI_PREAMBLE_SIZE		(4 * sizeof(int))
#define __ARCH_SI_TRAPNO

#include <asm-generic/siginfo.h>

/*
 * SIGFPE si_codes
 */
#ifdef __KERNEL__
#define FPE_FIXME	0	/* Broken dup of SI_USER */
#endif /* __KERNEL__ */

/*
 * SIGTRAP si_codes
 */
#ifdef __KERNEL__
#define TRAP_FIXME	0	/* Broken dup of SI_USER */
#endif /* __KERNEL__ */

#endif
