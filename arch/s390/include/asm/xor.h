/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Optimited xor routines
 *
 * Copyright IBM Corp. 2016
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */
#ifndef _ASM_S390_XOR_H
#define _ASM_S390_XOR_H

extern struct xor_block_template xor_block_xc;

#define arch_xor_init arch_xor_init
static __always_inline void __init arch_xor_init(void)
{
	xor_force(&xor_block_xc);
}

#endif /* _ASM_S390_XOR_H */
