/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005, 06 by Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2005 MIPS Technologies, Inc.
 */
#ifndef _ASM_ABI_H
#define _ASM_ABI_H

#include <asm/signal.h>
#include <asm/siginfo.h>

struct mips_abi {
	int (* const setup_frame)(void *sig_return, struct ksignal *ksig,
				  struct pt_regs *regs, sigset_t *set);
	const unsigned long	signal_return_offset;
	int (* const setup_rt_frame)(void *sig_return, struct ksignal *ksig,
				     struct pt_regs *regs, sigset_t *set);
	const unsigned long	rt_signal_return_offset;
	const unsigned long	restart;

	unsigned	off_sc_fpregs;
	unsigned	off_sc_fpc_csr;
	unsigned	off_sc_used_math;
};

#endif /* _ASM_ABI_H */
