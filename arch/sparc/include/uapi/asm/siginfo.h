#ifndef _UAPI__SPARC_SIGINFO_H
#define _UAPI__SPARC_SIGINFO_H

#if defined(__sparc__) && defined(__arch64__)

#define __ARCH_SI_PREAMBLE_SIZE	(4 * sizeof(int))
#define __ARCH_SI_BAND_T int

#endif /* defined(__sparc__) && defined(__arch64__) */


#define __ARCH_SI_TRAPNO

#include <asm-generic/siginfo.h>


#define SI_NOINFO	32767		/* no information in siginfo_t */

/*
 * SIGFPE si_codes
 */
#ifdef __KERNEL__
#define FPE_FIXME	(__SI_FAULT|0)	/* Broken dup of SI_USER */
#endif /* __KERNEL__ */

/*
 * SIGEMT si_codes
 */
#define EMT_TAGOVF	(__SI_FAULT|1)	/* tag overflow */
#define NSIGEMT		1

#endif /* _UAPI__SPARC_SIGINFO_H */
