/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Based on <asm-i386/siginfo.h>.
 *
 * Modified 1998-2002
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co
 */
#ifndef _UAPI_ASM_IA64_SIGINFO_H
#define _UAPI_ASM_IA64_SIGINFO_H


#define __ARCH_SI_PREAMBLE_SIZE	(4 * sizeof(int))

#include <asm-generic/siginfo.h>

#define si_imm		_sifields._sigfault._imm	/* as per UNIX SysV ABI spec */
#define si_flags	_sifields._sigfault._flags
/*
 * si_isr is valid for SIGILL, SIGFPE, SIGSEGV, SIGBUS, and SIGTRAP provided that
 * si_code is non-zero and __ISR_VALID is set in si_flags.
 */
#define si_isr		_sifields._sigfault._isr

/*
 * Flag values for si_flags:
 */
#define __ISR_VALID_BIT	0
#define __ISR_VALID	(1 << __ISR_VALID_BIT)

/*
 * SIGILL si_codes
 */
#define ILL_BADIADDR	9	/* unimplemented instruction address */
#define __ILL_BREAK	10	/* illegal break */
#define __ILL_BNDMOD	11	/* bundle-update (modification) in progress */
#undef NSIGILL
#define NSIGILL		11

/*
 * SIGFPE si_codes
 */
#ifdef __KERNEL__
#define FPE_FIXME	0	/* Broken dup of SI_USER */
#endif /* __KERNEL__ */
#define __FPE_DECOVF	9	/* decimal overflow */
#define __FPE_DECDIV	10	/* decimal division by zero */
#define __FPE_DECERR	11	/* packed decimal error */
#define __FPE_INVASC	12	/* invalid ASCII digit */
#define __FPE_INVDEC	13	/* invalid decimal digit */
#undef NSIGFPE
#define NSIGFPE		13

/*
 * SIGSEGV si_codes
 */
#define __SEGV_PSTKOVF	4	/* paragraph stack overflow */
#undef NSIGSEGV
#define NSIGSEGV	4

#undef NSIGTRAP
#define NSIGTRAP	4


#endif /* _UAPI_ASM_IA64_SIGINFO_H */
