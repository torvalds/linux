/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 * Copyright (C) IBM Corporation, 2012
 *
 * Author: Anton Blanchard <anton@au.ibm.com>
 */
#ifndef _ASM_POWERPC_XOR_H
#define _ASM_POWERPC_XOR_H

#include <asm/cpu_has_feature.h>
#include <asm-generic/xor.h>

extern struct xor_block_template xor_block_altivec;

#define arch_xor_init arch_xor_init
static __always_inline void __init arch_xor_init(void)
{
	xor_register(&xor_block_8regs);
	xor_register(&xor_block_8regs_p);
	xor_register(&xor_block_32regs);
	xor_register(&xor_block_32regs_p);
#ifdef CONFIG_ALTIVEC
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		xor_register(&xor_block_altivec);
#endif
}

#endif /* _ASM_POWERPC_XOR_H */
