/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 2006 David S. Miller <davem@davemloft.net>
 */
#ifndef ___ASM_SPARC_XOR_H
#define ___ASM_SPARC_XOR_H

#if defined(__sparc__) && defined(__arch64__)
#include <asm/spitfire.h>

extern struct xor_block_template xor_block_VIS;
extern struct xor_block_template xor_block_niagara;

#define arch_xor_init arch_xor_init
static __always_inline void __init arch_xor_init(void)
{
	/* Force VIS for everything except Niagara.  */
	if (tlb_type == hypervisor &&
	    (sun4v_chip_type == SUN4V_CHIP_NIAGARA1 ||
	     sun4v_chip_type == SUN4V_CHIP_NIAGARA2 ||
	     sun4v_chip_type == SUN4V_CHIP_NIAGARA3 ||
	     sun4v_chip_type == SUN4V_CHIP_NIAGARA4 ||
	     sun4v_chip_type == SUN4V_CHIP_NIAGARA5))
		xor_force(&xor_block_niagara);
	else
		xor_force(&xor_block_VIS);
}
#else /* sparc64 */

/* For grins, also test the generic routines.  */
#include <asm-generic/xor.h>

extern struct xor_block_template xor_block_SPARC;

#define arch_xor_init arch_xor_init
static __always_inline void __init arch_xor_init(void)
{
	xor_register(&xor_block_8regs);
	xor_register(&xor_block_32regs);
	xor_register(&xor_block_SPARC);
}
#endif /* !sparc64 */
#endif /* ___ASM_SPARC_XOR_H */
