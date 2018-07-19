/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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
