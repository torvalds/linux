/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1996, 1998, 1999, 2004 by Ralf Baechle
 * Copyright (c) 1999 Silicon Graphics, Inc.
 */
#include <asm/asm.h>
#include <asm/offset.h>
#include <asm/regdef.h>

#define EX(insn,reg,addr,handler)			\
9:	insn	reg, addr;				\
	.section __ex_table,"a";			\
	PTR	9b, handler;				\
	.previous

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 for error
 */
LEAF(__strlen_user_asm)
	LONG_L		v0, TI_ADDR_LIMIT($28)	# pointer ok?
	and		v0, a0
	bnez		v0, fault

FEXPORT(__strlen_user_nocheck_asm)
	move		v0, a0
1:	EX(lb, t0, (v0), fault)
	PTR_ADDIU	v0, 1
	bnez		t0, 1b
	PTR_SUBU	v0, a0
	jr		ra
	END(__strlen_user_asm)

fault:	move		v0, zero
	jr		ra
