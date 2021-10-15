/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __X86_KERNEL_FPU_XSTATE_H
#define __X86_KERNEL_FPU_XSTATE_H

#include <asm/cpufeature.h>
#include <asm/fpu/xstate.h>

static inline void xstate_init_xcomp_bv(struct xregs_state *xsave, u64 mask)
{
	/*
	 * XRSTORS requires these bits set in xcomp_bv, or it will
	 * trigger #GP:
	 */
	if (cpu_feature_enabled(X86_FEATURE_XSAVES))
		xsave->header.xcomp_bv = mask | XCOMP_BV_COMPACTED_FORMAT;
}

#endif
