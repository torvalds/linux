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

#undef XOR_TRY_TEMPLATES
#define XOR_TRY_TEMPLATES				\
do {							\
	xor_speed(&xor_block_xc);			\
} while (0)

#define XOR_SELECT_TEMPLATE(FASTEST)	(&xor_block_xc)

#endif /* _ASM_S390_XOR_H */
