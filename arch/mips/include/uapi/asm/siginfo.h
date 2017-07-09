/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 1999, 2001, 2003 Ralf Baechle
 * Copyright (C) 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef _UAPI_ASM_SIGINFO_H
#define _UAPI_ASM_SIGINFO_H


#define __ARCH_SIGEV_PREAMBLE_SIZE (sizeof(long) + 2*sizeof(int))
#undef __ARCH_SI_TRAPNO /* exception code needs to fill this ...  */

/*
 * Careful to keep union _sifields from shifting ...
 */
#if _MIPS_SZLONG == 32
#define __ARCH_SI_PREAMBLE_SIZE (3 * sizeof(int))
#elif _MIPS_SZLONG == 64
#define __ARCH_SI_PREAMBLE_SIZE (4 * sizeof(int))
#else
#error _MIPS_SZLONG neither 32 nor 64
#endif

#define __ARCH_HAS_SWAPPED_SIGINFO

#include <asm-generic/siginfo.h>

/*
 * si_code values
 * Again these have been chosen to be IRIX compatible.
 */
#undef SI_ASYNCIO
#undef SI_TIMER
#undef SI_MESGQ
#define SI_ASYNCIO	-2	/* sent by AIO completion */
#define SI_TIMER	-3	/* sent by timer expiration */
#define SI_MESGQ	-4	/* sent by real time mesq state change */

#endif /* _UAPI_ASM_SIGINFO_H */
