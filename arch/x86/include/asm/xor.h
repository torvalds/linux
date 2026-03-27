/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_X86_XOR_H
#define _ASM_X86_XOR_H

#include <asm/cpufeature.h>
#include <asm-generic/xor.h>

extern struct xor_block_template xor_block_pII_mmx;
extern struct xor_block_template xor_block_p5_mmx;
extern struct xor_block_template xor_block_sse;
extern struct xor_block_template xor_block_sse_pf64;
extern struct xor_block_template xor_block_avx;

/*
 * When SSE is available, use it as it can write around L2.  We may also be able
 * to load into the L1 only depending on how the cpu deals with a load to a line
 * that is being prefetched.
 *
 * When AVX2 is available, force using it as it is better by all measures.
 *
 * 32-bit without MMX can fall back to the generic routines.
 */
#define arch_xor_init arch_xor_init
static __always_inline void __init arch_xor_init(void)
{
	if (boot_cpu_has(X86_FEATURE_AVX) &&
	    boot_cpu_has(X86_FEATURE_OSXSAVE)) {
		xor_force(&xor_block_avx);
	} else if (IS_ENABLED(CONFIG_X86_64) || boot_cpu_has(X86_FEATURE_XMM)) {
		xor_register(&xor_block_sse);
		xor_register(&xor_block_sse_pf64);
	} else if (boot_cpu_has(X86_FEATURE_MMX)) {
		xor_register(&xor_block_pII_mmx);
		xor_register(&xor_block_p5_mmx);
	} else {
		xor_register(&xor_block_8regs);
		xor_register(&xor_block_8regs_p);
		xor_register(&xor_block_32regs);
		xor_register(&xor_block_32regs_p);
	}
}

#endif /* _ASM_X86_XOR_H */
