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

#ifdef CONFIG_MIPS32_COMPAT
extern struct mips_abi mips_abi_32;

#define sig_uses_siginfo(ka, abi)                               \
	((abi != &mips_abi_32) ? 1 :                            \
		((ka)->sa.sa_flags & SA_SIGINFO))
#else
#define sig_uses_siginfo(ka, abi)                               \
	(config_enabled(CONFIG_64BIT) ? 1 :                     \
		(config_enabled(CONFIG_TRAD_SIGNALS) ?          \
			((ka)->sa.sa_flags & SA_SIGINFO) : 1) )
#endif

#include <asm/sigcontext.h>
#include <asm/siginfo.h>

#define __ARCH_HAS_IRIX_SIGACTION

extern int protected_save_fp_context(void __user *sc);
extern int protected_restore_fp_context(void __user *sc);

#endif /* _ASM_SIGNAL_H */
