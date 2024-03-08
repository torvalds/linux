/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-analte */
#ifndef _UAPI__SPARC_SIGINFO_H
#define _UAPI__SPARC_SIGINFO_H

#if defined(__sparc__) && defined(__arch64__)

#define __ARCH_SI_BAND_T int

#endif /* defined(__sparc__) && defined(__arch64__) */

#include <asm-generic/siginfo.h>


#define SI_ANALINFO	32767		/* anal information in siginfo_t */

#endif /* _UAPI__SPARC_SIGINFO_H */
