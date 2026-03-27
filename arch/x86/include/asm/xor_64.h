/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_XOR_64_H
#define _ASM_X86_XOR_64_H

static struct xor_block_template xor_block_sse = {
	.name = "generic_sse",
	.do_2 = xor_sse_2,
	.do_3 = xor_sse_3,
	.do_4 = xor_sse_4,
	.do_5 = xor_sse_5,
};


/* Also try the AVX routines */
#include <asm/xor_avx.h>

/* We force the use of the SSE xor block because it can write around L2.
   We may also be able to load into the L1 only depending on how the cpu
   deals with a load to a line that is being prefetched.  */
#define arch_xor_init arch_xor_init
static __always_inline void __init arch_xor_init(void)
{
	if (boot_cpu_has(X86_FEATURE_AVX) &&
	    boot_cpu_has(X86_FEATURE_OSXSAVE)) {
		xor_force(&xor_block_avx);
	} else {
		xor_register(&xor_block_sse_pf64);
		xor_register(&xor_block_sse);
	}
}

#endif /* _ASM_X86_XOR_64_H */
