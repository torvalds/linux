/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <asm/special_insns.h>
#include <asm-generic/xor.h>

extern struct xor_block_template xor_block_alpha;
extern struct xor_block_template xor_block_alpha_prefetch;

/*
 * Force the use of alpha_prefetch if EV6, as it is significantly faster in the
 * cold cache case.
 */
#define arch_xor_init arch_xor_init
static __always_inline void __init arch_xor_init(void)
{
	if (implver() == IMPLVER_EV6) {
		xor_force(&xor_block_alpha_prefetch);
	} else {
		xor_register(&xor_block_8regs);
		xor_register(&xor_block_32regs);
		xor_register(&xor_block_alpha);
		xor_register(&xor_block_alpha_prefetch);
	}
}
