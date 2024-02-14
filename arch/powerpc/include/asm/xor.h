/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 * Copyright (C) IBM Corporation, 2012
 *
 * Author: Anton Blanchard <anton@au.ibm.com>
 */
#ifndef _ASM_POWERPC_XOR_H
#define _ASM_POWERPC_XOR_H

#ifdef CONFIG_ALTIVEC

#include <asm/cputable.h>
#include <asm/cpu_has_feature.h>
#include <asm/xor_altivec.h>

static struct xor_block_template xor_block_altivec = {
	.name = "altivec",
	.do_2 = xor_altivec_2,
	.do_3 = xor_altivec_3,
	.do_4 = xor_altivec_4,
	.do_5 = xor_altivec_5,
};

#define XOR_SPEED_ALTIVEC()				\
	do {						\
		if (cpu_has_feature(CPU_FTR_ALTIVEC))	\
			xor_speed(&xor_block_altivec);	\
	} while (0)
#else
#define XOR_SPEED_ALTIVEC()
#endif

/* Also try the generic routines. */
#include <asm-generic/xor.h>

#undef XOR_TRY_TEMPLATES
#define XOR_TRY_TEMPLATES				\
do {							\
	xor_speed(&xor_block_8regs);			\
	xor_speed(&xor_block_8regs_p);			\
	xor_speed(&xor_block_32regs);			\
	xor_speed(&xor_block_32regs_p);			\
	XOR_SPEED_ALTIVEC();				\
} while (0)

#endif /* _ASM_POWERPC_XOR_H */
