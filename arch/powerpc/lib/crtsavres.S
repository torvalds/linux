/*
 * Special support for eabi and SVR4
 *
 *   Copyright (C) 1995, 1996, 1998, 2000, 2001 Free Software Foundation, Inc.
 *   Copyright 2008 Freescale Semiconductor, Inc.
 *   Written By Michael Meissner
 *
 * Based on gcc/config/rs6000/crtsavres.asm from gcc
 * 64 bit additions from reading the PPC elf64abi document.
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

#include <asm/ppc_asm.h>

	.file	"crtsavres.S"
	.section ".text"

#ifdef CONFIG_CC_OPTIMIZE_FOR_SIZE

#ifndef CONFIG_PPC64

/* Routines for saving integer registers, called by the compiler.  */
/* Called with r11 pointing to the stack header word of the caller of the */
/* function, just beyond the end of the integer save area.  */

_GLOBAL(_savegpr_14)
_GLOBAL(_save32gpr_14)
	stw	14,-72(11)	/* save gp registers */
_GLOBAL(_savegpr_15)
_GLOBAL(_save32gpr_15)
	stw	15,-68(11)
_GLOBAL(_savegpr_16)
_GLOBAL(_save32gpr_16)
	stw	16,-64(11)
_GLOBAL(_savegpr_17)
_GLOBAL(_save32gpr_17)
	stw	17,-60(11)
_GLOBAL(_savegpr_18)
_GLOBAL(_save32gpr_18)
	stw	18,-56(11)
_GLOBAL(_savegpr_19)
_GLOBAL(_save32gpr_19)
	stw	19,-52(11)
_GLOBAL(_savegpr_20)
_GLOBAL(_save32gpr_20)
	stw	20,-48(11)
_GLOBAL(_savegpr_21)
_GLOBAL(_save32gpr_21)
	stw	21,-44(11)
_GLOBAL(_savegpr_22)
_GLOBAL(_save32gpr_22)
	stw	22,-40(11)
_GLOBAL(_savegpr_23)
_GLOBAL(_save32gpr_23)
	stw	23,-36(11)
_GLOBAL(_savegpr_24)
_GLOBAL(_save32gpr_24)
	stw	24,-32(11)
_GLOBAL(_savegpr_25)
_GLOBAL(_save32gpr_25)
	stw	25,-28(11)
_GLOBAL(_savegpr_26)
_GLOBAL(_save32gpr_26)
	stw	26,-24(11)
_GLOBAL(_savegpr_27)
_GLOBAL(_save32gpr_27)
	stw	27,-20(11)
_GLOBAL(_savegpr_28)
_GLOBAL(_save32gpr_28)
	stw	28,-16(11)
_GLOBAL(_savegpr_29)
_GLOBAL(_save32gpr_29)
	stw	29,-12(11)
_GLOBAL(_savegpr_30)
_GLOBAL(_save32gpr_30)
	stw	30,-8(11)
_GLOBAL(_savegpr_31)
_GLOBAL(_save32gpr_31)
	stw	31,-4(11)
	blr

/* Routines for restoring integer registers, called by the compiler.  */
/* Called with r11 pointing to the stack header word of the caller of the */
/* function, just beyond the end of the integer restore area.  */

_GLOBAL(_restgpr_14)
_GLOBAL(_rest32gpr_14)
	lwz	14,-72(11)	/* restore gp registers */
_GLOBAL(_restgpr_15)
_GLOBAL(_rest32gpr_15)
	lwz	15,-68(11)
_GLOBAL(_restgpr_16)
_GLOBAL(_rest32gpr_16)
	lwz	16,-64(11)
_GLOBAL(_restgpr_17)
_GLOBAL(_rest32gpr_17)
	lwz	17,-60(11)
_GLOBAL(_restgpr_18)
_GLOBAL(_rest32gpr_18)
	lwz	18,-56(11)
_GLOBAL(_restgpr_19)
_GLOBAL(_rest32gpr_19)
	lwz	19,-52(11)
_GLOBAL(_restgpr_20)
_GLOBAL(_rest32gpr_20)
	lwz	20,-48(11)
_GLOBAL(_restgpr_21)
_GLOBAL(_rest32gpr_21)
	lwz	21,-44(11)
_GLOBAL(_restgpr_22)
_GLOBAL(_rest32gpr_22)
	lwz	22,-40(11)
_GLOBAL(_restgpr_23)
_GLOBAL(_rest32gpr_23)
	lwz	23,-36(11)
_GLOBAL(_restgpr_24)
_GLOBAL(_rest32gpr_24)
	lwz	24,-32(11)
_GLOBAL(_restgpr_25)
_GLOBAL(_rest32gpr_25)
	lwz	25,-28(11)
_GLOBAL(_restgpr_26)
_GLOBAL(_rest32gpr_26)
	lwz	26,-24(11)
_GLOBAL(_restgpr_27)
_GLOBAL(_rest32gpr_27)
	lwz	27,-20(11)
_GLOBAL(_restgpr_28)
_GLOBAL(_rest32gpr_28)
	lwz	28,-16(11)
_GLOBAL(_restgpr_29)
_GLOBAL(_rest32gpr_29)
	lwz	29,-12(11)
_GLOBAL(_restgpr_30)
_GLOBAL(_rest32gpr_30)
	lwz	30,-8(11)
_GLOBAL(_restgpr_31)
_GLOBAL(_rest32gpr_31)
	lwz	31,-4(11)
	blr

/* Routines for restoring integer registers, called by the compiler.  */
/* Called with r11 pointing to the stack header word of the caller of the */
/* function, just beyond the end of the integer restore area.  */

_GLOBAL(_restgpr_14_x)
_GLOBAL(_rest32gpr_14_x)
	lwz	14,-72(11)	/* restore gp registers */
_GLOBAL(_restgpr_15_x)
_GLOBAL(_rest32gpr_15_x)
	lwz	15,-68(11)
_GLOBAL(_restgpr_16_x)
_GLOBAL(_rest32gpr_16_x)
	lwz	16,-64(11)
_GLOBAL(_restgpr_17_x)
_GLOBAL(_rest32gpr_17_x)
	lwz	17,-60(11)
_GLOBAL(_restgpr_18_x)
_GLOBAL(_rest32gpr_18_x)
	lwz	18,-56(11)
_GLOBAL(_restgpr_19_x)
_GLOBAL(_rest32gpr_19_x)
	lwz	19,-52(11)
_GLOBAL(_restgpr_20_x)
_GLOBAL(_rest32gpr_20_x)
	lwz	20,-48(11)
_GLOBAL(_restgpr_21_x)
_GLOBAL(_rest32gpr_21_x)
	lwz	21,-44(11)
_GLOBAL(_restgpr_22_x)
_GLOBAL(_rest32gpr_22_x)
	lwz	22,-40(11)
_GLOBAL(_restgpr_23_x)
_GLOBAL(_rest32gpr_23_x)
	lwz	23,-36(11)
_GLOBAL(_restgpr_24_x)
_GLOBAL(_rest32gpr_24_x)
	lwz	24,-32(11)
_GLOBAL(_restgpr_25_x)
_GLOBAL(_rest32gpr_25_x)
	lwz	25,-28(11)
_GLOBAL(_restgpr_26_x)
_GLOBAL(_rest32gpr_26_x)
	lwz	26,-24(11)
_GLOBAL(_restgpr_27_x)
_GLOBAL(_rest32gpr_27_x)
	lwz	27,-20(11)
_GLOBAL(_restgpr_28_x)
_GLOBAL(_rest32gpr_28_x)
	lwz	28,-16(11)
_GLOBAL(_restgpr_29_x)
_GLOBAL(_rest32gpr_29_x)
	lwz	29,-12(11)
_GLOBAL(_restgpr_30_x)
_GLOBAL(_rest32gpr_30_x)
	lwz	30,-8(11)
_GLOBAL(_restgpr_31_x)
_GLOBAL(_rest32gpr_31_x)
	lwz	0,4(11)
	lwz	31,-4(11)
	mtlr	0
	mr	1,11
	blr

#else /* CONFIG_PPC64 */

.globl	_savegpr0_14
_savegpr0_14:
	std	r14,-144(r1)
.globl	_savegpr0_15
_savegpr0_15:
	std	r15,-136(r1)
.globl	_savegpr0_16
_savegpr0_16:
	std	r16,-128(r1)
.globl	_savegpr0_17
_savegpr0_17:
	std	r17,-120(r1)
.globl	_savegpr0_18
_savegpr0_18:
	std	r18,-112(r1)
.globl	_savegpr0_19
_savegpr0_19:
	std	r19,-104(r1)
.globl	_savegpr0_20
_savegpr0_20:
	std	r20,-96(r1)
.globl	_savegpr0_21
_savegpr0_21:
	std	r21,-88(r1)
.globl	_savegpr0_22
_savegpr0_22:
	std	r22,-80(r1)
.globl	_savegpr0_23
_savegpr0_23:
	std	r23,-72(r1)
.globl	_savegpr0_24
_savegpr0_24:
	std	r24,-64(r1)
.globl	_savegpr0_25
_savegpr0_25:
	std	r25,-56(r1)
.globl	_savegpr0_26
_savegpr0_26:
	std	r26,-48(r1)
.globl	_savegpr0_27
_savegpr0_27:
	std	r27,-40(r1)
.globl	_savegpr0_28
_savegpr0_28:
	std	r28,-32(r1)
.globl	_savegpr0_29
_savegpr0_29:
	std	r29,-24(r1)
.globl	_savegpr0_30
_savegpr0_30:
	std	r30,-16(r1)
.globl	_savegpr0_31
_savegpr0_31:
	std	r31,-8(r1)
	std	r0,16(r1)
	blr

.globl	_restgpr0_14
_restgpr0_14:
	ld	r14,-144(r1)
.globl	_restgpr0_15
_restgpr0_15:
	ld	r15,-136(r1)
.globl	_restgpr0_16
_restgpr0_16:
	ld	r16,-128(r1)
.globl	_restgpr0_17
_restgpr0_17:
	ld	r17,-120(r1)
.globl	_restgpr0_18
_restgpr0_18:
	ld	r18,-112(r1)
.globl	_restgpr0_19
_restgpr0_19:
	ld	r19,-104(r1)
.globl	_restgpr0_20
_restgpr0_20:
	ld	r20,-96(r1)
.globl	_restgpr0_21
_restgpr0_21:
	ld	r21,-88(r1)
.globl	_restgpr0_22
_restgpr0_22:
	ld	r22,-80(r1)
.globl	_restgpr0_23
_restgpr0_23:
	ld	r23,-72(r1)
.globl	_restgpr0_24
_restgpr0_24:
	ld	r24,-64(r1)
.globl	_restgpr0_25
_restgpr0_25:
	ld	r25,-56(r1)
.globl	_restgpr0_26
_restgpr0_26:
	ld	r26,-48(r1)
.globl	_restgpr0_27
_restgpr0_27:
	ld	r27,-40(r1)
.globl	_restgpr0_28
_restgpr0_28:
	ld	r28,-32(r1)
.globl	_restgpr0_29
_restgpr0_29:
	ld	r0,16(r1)
	ld	r29,-24(r1)
	mtlr	r0
	ld	r30,-16(r1)
	ld	r31,-8(r1)
	blr

.globl	_restgpr0_30
_restgpr0_30:
	ld	r30,-16(r1)
.globl	_restgpr0_31
_restgpr0_31:
	ld	r0,16(r1)
	ld	r31,-8(r1)
	mtlr	r0
	blr

#endif /* CONFIG_PPC64 */

#endif
