/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_SIGCONTEXT_H
#define _ASM_SIGCONTEXT_H

#include <uapi/asm/sigcontext.h>

#if _MIPS_SIM == _MIPS_SIM_ABI64 || _MIPS_SIM == _MIPS_SIM_NABI32

struct sigcontext32 {
	__u32		sc_regmask;	/* Unused */
	__u32		sc_status;	/* Unused */
	__u64		sc_pc;
	__u64		sc_regs[32];
	__u64		sc_fpregs[32];
	__u32		sc_acx;		/* Only MIPS32; was sc_ownedfp */
	__u32		sc_fpc_csr;
	__u32		sc_fpc_eir;	/* Unused */
	__u32		sc_used_math;
	__u32		sc_dsp;		/* dsp status, was sc_ssflags */
	__u64		sc_mdhi;
	__u64		sc_mdlo;
	__u32		sc_hi1;		/* Was sc_cause */
	__u32		sc_lo1;		/* Was sc_badvaddr */
	__u32		sc_hi2;		/* Was sc_sigset[4] */
	__u32		sc_lo2;
	__u32		sc_hi3;
	__u32		sc_lo3;
	__u64		sc_msaregs[32];	/* Most significant 64 bits */
	__u32		sc_msa_csr;
};
#endif /* _MIPS_SIM == _MIPS_SIM_ABI64 || _MIPS_SIM == _MIPS_SIM_NABI32 */
#endif /* _ASM_SIGCONTEXT_H */
