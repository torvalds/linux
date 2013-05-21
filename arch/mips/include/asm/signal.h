/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 97, 98, 99, 2003 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_SIGNAL_H
#define _ASM_SIGNAL_H

#include <uapi/asm/signal.h>


#ifdef CONFIG_TRAD_SIGNALS
#define sig_uses_siginfo(ka)	((ka)->sa.sa_flags & SA_SIGINFO)
#else
#define sig_uses_siginfo(ka)	(1)
#endif

#include <asm/sigcontext.h>
#include <asm/siginfo.h>

#define __ARCH_HAS_IRIX_SIGACTION

#endif /* _ASM_SIGNAL_H */
