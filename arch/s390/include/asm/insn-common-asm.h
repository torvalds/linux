/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Assembler helper macros to generate .byte/.word code for instructions
 * that are unknown to older binutils versions.
 */

#ifndef __ASM_S390_INSN_COMMON_ASM_H
#define __ASM_S390_INSN_COMMON_ASM_H

#ifdef __ASSEMBLER__

/*
 * GR_NUM - Retrieve general-purpose register number
 *
 * @opd:	Operand to store register number
 * @gr:		String designation register in the format "%rN"
 */
.macro	GR_NUM	opd gr
	\opd = 255
	.irp rs,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	.ifc \gr,%r\rs
		\opd = \rs
	.endif
	.endr
	.if \opd == 255
		\opd = \gr
	.endif
.endm

/*
 * VX_NUM - Retrieve vector register number
 *
 * @opd:	Operand to store register number
 * @vxr:	String designation register in the format "%vN"
 *
 * The vector register number is used for as input number to the
 * instruction and, as well as, to compute the RXB field of the
 * instruction.
 */
.macro	VX_NUM	opd vxr
	\opd = 255
	.irp vs,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	.ifc \vxr,%v\vs
		\opd = \vs
	.endif
	.endr
	.if \opd == 255
		\opd = \vxr
	.endif
.endm

#endif	/* __ASSEMBLER__ */
#endif	/* __ASM_S390_INSN_COMMON_ASM_H */
