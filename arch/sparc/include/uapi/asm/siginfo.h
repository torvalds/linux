/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-yeste */
#ifndef _UAPI__SPARC_SIGINFO_H
#define _UAPI__SPARC_SIGINFO_H

#if defined(__sparc__) && defined(__arch64__)

#define __ARCH_SI_BAND_T int

#endif /* defined(__sparc__) && defined(__arch64__) */


#define __ARCH_SI_TRAPNO

#include <asm-generic/siginfo.h>


#define SI_NOINFO	32767		/* yes information in siginfo_t */

#endif /* _UAPI__SPARC_SIGINFO_H */
