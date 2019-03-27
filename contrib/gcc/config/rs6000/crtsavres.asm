/*
 * Special support for eabi and SVR4
 *
 *   Copyright (C) 1995, 1996, 1998, 2000, 2001 Free Software Foundation, Inc.
 *   Written By Michael Meissner
 *   64-bit support written by David Edelsohn
 * 
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 * 
 * In addition to the permissions in the GNU General Public License, the
 * Free Software Foundation gives you unlimited permission to link the
 * compiled version of this file with other programs, and to distribute
 * those programs without any restriction coming from the use of this
 * file.  (The General Public License restrictions do apply in other
 * respects; for example, they cover modification of the file, and
 * distribution when not linked into another program.)
 * 
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 * 
 *    As a special exception, if you link this library with files
 *    compiled with GCC to produce an executable, this does not cause
 *    the resulting executable to be covered by the GNU General Public License.
 *    This exception does not however invalidate any other reasons why
 *    the executable file might be covered by the GNU General Public License.
 */ 

/* Do any initializations needed for the eabi environment */

	.file	"crtsavres.asm"
	.section ".text"
	#include "ppc-asm.h"

/* On PowerPC64 Linux, these functions are provided by the linker.  */
#ifndef __powerpc64__

/* Routines for saving floating point registers, called by the compiler.  */
/* Called with r11 pointing to the stack header word of the caller of the */
/* function, just beyond the end of the floating point save area.  */

FUNC_START(_savefpr_14)	stfd	14,-144(11)	/* save fp registers */
FUNC_START(_savefpr_15)	stfd	15,-136(11)
FUNC_START(_savefpr_16)	stfd	16,-128(11)
FUNC_START(_savefpr_17)	stfd	17,-120(11)
FUNC_START(_savefpr_18)	stfd	18,-112(11)
FUNC_START(_savefpr_19)	stfd	19,-104(11)
FUNC_START(_savefpr_20)	stfd	20,-96(11)
FUNC_START(_savefpr_21)	stfd	21,-88(11)
FUNC_START(_savefpr_22)	stfd	22,-80(11)
FUNC_START(_savefpr_23)	stfd	23,-72(11)
FUNC_START(_savefpr_24)	stfd	24,-64(11)
FUNC_START(_savefpr_25)	stfd	25,-56(11)
FUNC_START(_savefpr_26)	stfd	26,-48(11)
FUNC_START(_savefpr_27)	stfd	27,-40(11)
FUNC_START(_savefpr_28)	stfd	28,-32(11)
FUNC_START(_savefpr_29)	stfd	29,-24(11)
FUNC_START(_savefpr_30)	stfd	30,-16(11)
FUNC_START(_savefpr_31)	stfd	31,-8(11)
			blr
FUNC_END(_savefpr_31)
FUNC_END(_savefpr_30)
FUNC_END(_savefpr_29)
FUNC_END(_savefpr_28)
FUNC_END(_savefpr_27)
FUNC_END(_savefpr_26)
FUNC_END(_savefpr_25)
FUNC_END(_savefpr_24)
FUNC_END(_savefpr_23)
FUNC_END(_savefpr_22)
FUNC_END(_savefpr_21)
FUNC_END(_savefpr_20)
FUNC_END(_savefpr_19)
FUNC_END(_savefpr_18)
FUNC_END(_savefpr_17)
FUNC_END(_savefpr_16)
FUNC_END(_savefpr_15)
FUNC_END(_savefpr_14)

/* Routines for saving integer registers, called by the compiler.  */
/* Called with r11 pointing to the stack header word of the caller of the */
/* function, just beyond the end of the integer save area.  */

FUNC_START(_savegpr_14)	stw	14,-72(11)	/* save gp registers */
FUNC_START(_savegpr_15)	stw	15,-68(11)
FUNC_START(_savegpr_16)	stw	16,-64(11)
FUNC_START(_savegpr_17)	stw	17,-60(11)
FUNC_START(_savegpr_18)	stw	18,-56(11)
FUNC_START(_savegpr_19)	stw	19,-52(11)
FUNC_START(_savegpr_20)	stw	20,-48(11)
FUNC_START(_savegpr_21)	stw	21,-44(11)
FUNC_START(_savegpr_22)	stw	22,-40(11)
FUNC_START(_savegpr_23)	stw	23,-36(11)
FUNC_START(_savegpr_24)	stw	24,-32(11)
FUNC_START(_savegpr_25)	stw	25,-28(11)
FUNC_START(_savegpr_26)	stw	26,-24(11)
FUNC_START(_savegpr_27)	stw	27,-20(11)
FUNC_START(_savegpr_28)	stw	28,-16(11)
FUNC_START(_savegpr_29)	stw	29,-12(11)
FUNC_START(_savegpr_30)	stw	30,-8(11)
FUNC_START(_savegpr_31)	stw	31,-4(11)
			blr
FUNC_END(_savegpr_31)
FUNC_END(_savegpr_30)
FUNC_END(_savegpr_29)
FUNC_END(_savegpr_28)
FUNC_END(_savegpr_27)
FUNC_END(_savegpr_26)
FUNC_END(_savegpr_25)
FUNC_END(_savegpr_24)
FUNC_END(_savegpr_23)
FUNC_END(_savegpr_22)
FUNC_END(_savegpr_21)
FUNC_END(_savegpr_20)
FUNC_END(_savegpr_19)
FUNC_END(_savegpr_18)
FUNC_END(_savegpr_17)
FUNC_END(_savegpr_16)
FUNC_END(_savegpr_15)
FUNC_END(_savegpr_14)

/* Routines for restoring floating point registers, called by the compiler.  */
/* Called with r11 pointing to the stack header word of the caller of the */
/* function, just beyond the end of the floating point save area.  */

FUNC_START(_restfpr_14)	lfd	14,-144(11)	/* restore fp registers */
FUNC_START(_restfpr_15)	lfd	15,-136(11)
FUNC_START(_restfpr_16)	lfd	16,-128(11)
FUNC_START(_restfpr_17)	lfd	17,-120(11)
FUNC_START(_restfpr_18)	lfd	18,-112(11)
FUNC_START(_restfpr_19)	lfd	19,-104(11)
FUNC_START(_restfpr_20)	lfd	20,-96(11)
FUNC_START(_restfpr_21)	lfd	21,-88(11)
FUNC_START(_restfpr_22)	lfd	22,-80(11)
FUNC_START(_restfpr_23)	lfd	23,-72(11)
FUNC_START(_restfpr_24)	lfd	24,-64(11)
FUNC_START(_restfpr_25)	lfd	25,-56(11)
FUNC_START(_restfpr_26)	lfd	26,-48(11)
FUNC_START(_restfpr_27)	lfd	27,-40(11)
FUNC_START(_restfpr_28)	lfd	28,-32(11)
FUNC_START(_restfpr_29)	lfd	29,-24(11)
FUNC_START(_restfpr_30)	lfd	30,-16(11)
FUNC_START(_restfpr_31)	lfd	31,-8(11)
			blr
FUNC_END(_restfpr_31)
FUNC_END(_restfpr_30)
FUNC_END(_restfpr_29)
FUNC_END(_restfpr_28)
FUNC_END(_restfpr_27)
FUNC_END(_restfpr_26)
FUNC_END(_restfpr_25)
FUNC_END(_restfpr_24)
FUNC_END(_restfpr_23)
FUNC_END(_restfpr_22)
FUNC_END(_restfpr_21)
FUNC_END(_restfpr_20)
FUNC_END(_restfpr_19)
FUNC_END(_restfpr_18)
FUNC_END(_restfpr_17)
FUNC_END(_restfpr_16)
FUNC_END(_restfpr_15)
FUNC_END(_restfpr_14)

/* Routines for restoring integer registers, called by the compiler.  */
/* Called with r11 pointing to the stack header word of the caller of the */
/* function, just beyond the end of the integer restore area.  */

FUNC_START(_restgpr_14)	lwz	14,-72(11)	/* restore gp registers */
FUNC_START(_restgpr_15)	lwz	15,-68(11)
FUNC_START(_restgpr_16)	lwz	16,-64(11)
FUNC_START(_restgpr_17)	lwz	17,-60(11)
FUNC_START(_restgpr_18)	lwz	18,-56(11)
FUNC_START(_restgpr_19)	lwz	19,-52(11)
FUNC_START(_restgpr_20)	lwz	20,-48(11)
FUNC_START(_restgpr_21)	lwz	21,-44(11)
FUNC_START(_restgpr_22)	lwz	22,-40(11)
FUNC_START(_restgpr_23)	lwz	23,-36(11)
FUNC_START(_restgpr_24)	lwz	24,-32(11)
FUNC_START(_restgpr_25)	lwz	25,-28(11)
FUNC_START(_restgpr_26)	lwz	26,-24(11)
FUNC_START(_restgpr_27)	lwz	27,-20(11)
FUNC_START(_restgpr_28)	lwz	28,-16(11)
FUNC_START(_restgpr_29)	lwz	29,-12(11)
FUNC_START(_restgpr_30)	lwz	30,-8(11)
FUNC_START(_restgpr_31)	lwz	31,-4(11)
			blr
FUNC_END(_restgpr_31)
FUNC_END(_restgpr_30)
FUNC_END(_restgpr_29)
FUNC_END(_restgpr_28)
FUNC_END(_restgpr_27)
FUNC_END(_restgpr_26)
FUNC_END(_restgpr_25)
FUNC_END(_restgpr_24)
FUNC_END(_restgpr_23)
FUNC_END(_restgpr_22)
FUNC_END(_restgpr_21)
FUNC_END(_restgpr_20)
FUNC_END(_restgpr_19)
FUNC_END(_restgpr_18)
FUNC_END(_restgpr_17)
FUNC_END(_restgpr_16)
FUNC_END(_restgpr_15)
FUNC_END(_restgpr_14)

/* Routines for restoring floating point registers, called by the compiler.  */
/* Called with r11 pointing to the stack header word of the caller of the */
/* function, just beyond the end of the floating point save area.  */
/* In addition to restoring the fp registers, it will return to the caller's */
/* caller */

FUNC_START(_restfpr_14_x)	lfd	14,-144(11)	/* restore fp registers */
FUNC_START(_restfpr_15_x)	lfd	15,-136(11)
FUNC_START(_restfpr_16_x)	lfd	16,-128(11)
FUNC_START(_restfpr_17_x)	lfd	17,-120(11)
FUNC_START(_restfpr_18_x)	lfd	18,-112(11)
FUNC_START(_restfpr_19_x)	lfd	19,-104(11)
FUNC_START(_restfpr_20_x)	lfd	20,-96(11)
FUNC_START(_restfpr_21_x)	lfd	21,-88(11)
FUNC_START(_restfpr_22_x)	lfd	22,-80(11)
FUNC_START(_restfpr_23_x)	lfd	23,-72(11)
FUNC_START(_restfpr_24_x)	lfd	24,-64(11)
FUNC_START(_restfpr_25_x)	lfd	25,-56(11)
FUNC_START(_restfpr_26_x)	lfd	26,-48(11)
FUNC_START(_restfpr_27_x)	lfd	27,-40(11)
FUNC_START(_restfpr_28_x)	lfd	28,-32(11)
FUNC_START(_restfpr_29_x)	lfd	29,-24(11)
FUNC_START(_restfpr_30_x)	lfd	30,-16(11)
FUNC_START(_restfpr_31_x)	lwz	0,4(11)
				lfd	31,-8(11)
				mtlr	0
				mr	1,11
				blr
FUNC_END(_restfpr_31_x)
FUNC_END(_restfpr_30_x)
FUNC_END(_restfpr_29_x)
FUNC_END(_restfpr_28_x)
FUNC_END(_restfpr_27_x)
FUNC_END(_restfpr_26_x)
FUNC_END(_restfpr_25_x)
FUNC_END(_restfpr_24_x)
FUNC_END(_restfpr_23_x)
FUNC_END(_restfpr_22_x)
FUNC_END(_restfpr_21_x)
FUNC_END(_restfpr_20_x)
FUNC_END(_restfpr_19_x)
FUNC_END(_restfpr_18_x)
FUNC_END(_restfpr_17_x)
FUNC_END(_restfpr_16_x)
FUNC_END(_restfpr_15_x)
FUNC_END(_restfpr_14_x)

/* Routines for restoring integer registers, called by the compiler.  */
/* Called with r11 pointing to the stack header word of the caller of the */
/* function, just beyond the end of the integer restore area.  */

FUNC_START(_restgpr_14_x)	lwz	14,-72(11)	/* restore gp registers */
FUNC_START(_restgpr_15_x)	lwz	15,-68(11)
FUNC_START(_restgpr_16_x)	lwz	16,-64(11)
FUNC_START(_restgpr_17_x)	lwz	17,-60(11)
FUNC_START(_restgpr_18_x)	lwz	18,-56(11)
FUNC_START(_restgpr_19_x)	lwz	19,-52(11)
FUNC_START(_restgpr_20_x)	lwz	20,-48(11)
FUNC_START(_restgpr_21_x)	lwz	21,-44(11)
FUNC_START(_restgpr_22_x)	lwz	22,-40(11)
FUNC_START(_restgpr_23_x)	lwz	23,-36(11)
FUNC_START(_restgpr_24_x)	lwz	24,-32(11)
FUNC_START(_restgpr_25_x)	lwz	25,-28(11)
FUNC_START(_restgpr_26_x)	lwz	26,-24(11)
FUNC_START(_restgpr_27_x)	lwz	27,-20(11)
FUNC_START(_restgpr_28_x)	lwz	28,-16(11)
FUNC_START(_restgpr_29_x)	lwz	29,-12(11)
FUNC_START(_restgpr_30_x)	lwz	30,-8(11)
FUNC_START(_restgpr_31_x)	lwz	0,4(11)
				lwz	31,-4(11)
				mtlr	0
				mr	1,11
				blr
FUNC_END(_restgpr_31_x)
FUNC_END(_restgpr_30_x)
FUNC_END(_restgpr_29_x)
FUNC_END(_restgpr_28_x)
FUNC_END(_restgpr_27_x)
FUNC_END(_restgpr_26_x)
FUNC_END(_restgpr_25_x)
FUNC_END(_restgpr_24_x)
FUNC_END(_restgpr_23_x)
FUNC_END(_restgpr_22_x)
FUNC_END(_restgpr_21_x)
FUNC_END(_restgpr_20_x)
FUNC_END(_restgpr_19_x)
FUNC_END(_restgpr_18_x)
FUNC_END(_restgpr_17_x)
FUNC_END(_restgpr_16_x)
FUNC_END(_restgpr_15_x)
FUNC_END(_restgpr_14_x)

#endif

	.section .note.GNU-stack,"",%progbits
