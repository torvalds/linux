/*
 * include/asm-xtensa/signal.h
 *
 * Swiped from SH.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */
#ifndef _XTENSA_SIGNAL_H
#define _XTENSA_SIGNAL_H

#include <uapi/asm/signal.h>

#ifndef __ASSEMBLY__
struct sigaction {
	__sighandler_t sa_handler;
	unsigned long sa_flags;
	void (*sa_restorer)(void);
	sigset_t sa_mask;		/* mask last for extensibility */
};

#include <asm/sigcontext.h>

#endif	/* __ASSEMBLY__ */
#endif	/* _XTENSA_SIGNAL_H */
