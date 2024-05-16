/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Vector Instructions
 *
 * Assembler macros to generate .byte/.word code for particular
 * vector instructions that are supported by recent binutils (>= 2.26) only.
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#ifndef __ASM_S390_FPU_INSN_ASM_H
#define __ASM_S390_FPU_INSN_ASM_H

#ifndef __ASM_S390_FPU_INSN_H
#error only <asm/fpu-insn.h> can be included directly
#endif

#ifdef __ASSEMBLY__

/* Macros to generate vector instruction byte code */

/* GR_NUM - Retrieve general-purpose register number
 *
 * @opd:	Operand to store register number
 * @r64:	String designation register in the format "%rN"
 */
.macro	GR_NUM	opd gr
	\opd = 255
	.ifc \gr,%r0
		\opd = 0
	.endif
	.ifc \gr,%r1
		\opd = 1
	.endif
	.ifc \gr,%r2
		\opd = 2
	.endif
	.ifc \gr,%r3
		\opd = 3
	.endif
	.ifc \gr,%r4
		\opd = 4
	.endif
	.ifc \gr,%r5
		\opd = 5
	.endif
	.ifc \gr,%r6
		\opd = 6
	.endif
	.ifc \gr,%r7
		\opd = 7
	.endif
	.ifc \gr,%r8
		\opd = 8
	.endif
	.ifc \gr,%r9
		\opd = 9
	.endif
	.ifc \gr,%r10
		\opd = 10
	.endif
	.ifc \gr,%r11
		\opd = 11
	.endif
	.ifc \gr,%r12
		\opd = 12
	.endif
	.ifc \gr,%r13
		\opd = 13
	.endif
	.ifc \gr,%r14
		\opd = 14
	.endif
	.ifc \gr,%r15
		\opd = 15
	.endif
	.if \opd == 255
		\opd = \gr
	.endif
.endm

/* VX_NUM - Retrieve vector register number
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
	.ifc \vxr,%v0
		\opd = 0
	.endif
	.ifc \vxr,%v1
		\opd = 1
	.endif
	.ifc \vxr,%v2
		\opd = 2
	.endif
	.ifc \vxr,%v3
		\opd = 3
	.endif
	.ifc \vxr,%v4
		\opd = 4
	.endif
	.ifc \vxr,%v5
		\opd = 5
	.endif
	.ifc \vxr,%v6
		\opd = 6
	.endif
	.ifc \vxr,%v7
		\opd = 7
	.endif
	.ifc \vxr,%v8
		\opd = 8
	.endif
	.ifc \vxr,%v9
		\opd = 9
	.endif
	.ifc \vxr,%v10
		\opd = 10
	.endif
	.ifc \vxr,%v11
		\opd = 11
	.endif
	.ifc \vxr,%v12
		\opd = 12
	.endif
	.ifc \vxr,%v13
		\opd = 13
	.endif
	.ifc \vxr,%v14
		\opd = 14
	.endif
	.ifc \vxr,%v15
		\opd = 15
	.endif
	.ifc \vxr,%v16
		\opd = 16
	.endif
	.ifc \vxr,%v17
		\opd = 17
	.endif
	.ifc \vxr,%v18
		\opd = 18
	.endif
	.ifc \vxr,%v19
		\opd = 19
	.endif
	.ifc \vxr,%v20
		\opd = 20
	.endif
	.ifc \vxr,%v21
		\opd = 21
	.endif
	.ifc \vxr,%v22
		\opd = 22
	.endif
	.ifc \vxr,%v23
		\opd = 23
	.endif
	.ifc \vxr,%v24
		\opd = 24
	.endif
	.ifc \vxr,%v25
		\opd = 25
	.endif
	.ifc \vxr,%v26
		\opd = 26
	.endif
	.ifc \vxr,%v27
		\opd = 27
	.endif
	.ifc \vxr,%v28
		\opd = 28
	.endif
	.ifc \vxr,%v29
		\opd = 29
	.endif
	.ifc \vxr,%v30
		\opd = 30
	.endif
	.ifc \vxr,%v31
		\opd = 31
	.endif
	.if \opd == 255
		\opd = \vxr
	.endif
.endm

/* RXB - Compute most significant bit used vector registers
 *
 * @rxb:	Operand to store computed RXB value
 * @v1:		Vector register designated operand whose MSB is stored in
 *		RXB bit 0 (instruction bit 36) and whose remaining bits
 *		are stored in instruction bits 8-11.
 * @v2:		Vector register designated operand whose MSB is stored in
 *		RXB bit 1 (instruction bit 37) and whose remaining bits
 *		are stored in instruction bits 12-15.
 * @v3:		Vector register designated operand whose MSB is stored in
 *		RXB bit 2 (instruction bit 38) and whose remaining bits
 *		are stored in instruction bits 16-19.
 * @v4:		Vector register designated operand whose MSB is stored in
 *		RXB bit 3 (instruction bit 39) and whose remaining bits
 *		are stored in instruction bits 32-35.
 *
 * Note: In most vector instruction formats [1] V1, V2, V3, and V4 directly
 * correspond to @v1, @v2, @v3, and @v4. But there are exceptions, such as but
 * not limited to the vector instruction formats VRR-g, VRR-h, VRS-a, VRS-d,
 * and VSI.
 *
 * [1] IBM z/Architecture Principles of Operation, chapter "Program
 * Execution, section "Instructions", subsection "Instruction Formats".
 */
.macro	RXB	rxb v1 v2=0 v3=0 v4=0
	\rxb = 0
	.if \v1 & 0x10
		\rxb = \rxb | 0x08
	.endif
	.if \v2 & 0x10
		\rxb = \rxb | 0x04
	.endif
	.if \v3 & 0x10
		\rxb = \rxb | 0x02
	.endif
	.if \v4 & 0x10
		\rxb = \rxb | 0x01
	.endif
.endm

/* MRXB - Generate Element Size Control and RXB value
 *
 * @m:		Element size control
 * @v1:		First vector register designated operand (for RXB)
 * @v2:		Second vector register designated operand (for RXB)
 * @v3:		Third vector register designated operand (for RXB)
 * @v4:		Fourth vector register designated operand (for RXB)
 *
 * Note: For @v1, @v2, @v3, and @v4 also refer to the RXB macro
 * description for further details.
 */
.macro	MRXB	m v1 v2=0 v3=0 v4=0
	rxb = 0
	RXB	rxb, \v1, \v2, \v3, \v4
	.byte	(\m << 4) | rxb
.endm

/* MRXBOPC - Generate Element Size Control, RXB, and final Opcode fields
 *
 * @m:		Element size control
 * @opc:	Opcode
 * @v1:		First vector register designated operand (for RXB)
 * @v2:		Second vector register designated operand (for RXB)
 * @v3:		Third vector register designated operand (for RXB)
 * @v4:		Fourth vector register designated operand (for RXB)
 *
 * Note: For @v1, @v2, @v3, and @v4 also refer to the RXB macro
 * description for further details.
 */
.macro	MRXBOPC	m opc v1 v2=0 v3=0 v4=0
	MRXB	\m, \v1, \v2, \v3, \v4
	.byte	\opc
.endm

/* Vector support instructions */

/* VECTOR GENERATE BYTE MASK */
.macro	VGBM	vr imm2
	VX_NUM	v1, \vr
	.word	(0xE700 | ((v1&15) << 4))
	.word	\imm2
	MRXBOPC	0, 0x44, v1
.endm
.macro	VZERO	vxr
	VGBM	\vxr, 0
.endm
.macro	VONE	vxr
	VGBM	\vxr, 0xFFFF
.endm

/* VECTOR LOAD VR ELEMENT FROM GR */
.macro	VLVG	v, gr, disp, m
	VX_NUM	v1, \v
	GR_NUM	b2, "%r0"
	GR_NUM	r3, \gr
	.word	0xE700 | ((v1&15) << 4) | r3
	.word	(b2 << 12) | (\disp)
	MRXBOPC	\m, 0x22, v1
.endm
.macro	VLVGB	v, gr, index, base
	VLVG	\v, \gr, \index, \base, 0
.endm
.macro	VLVGH	v, gr, index
	VLVG	\v, \gr, \index, 1
.endm
.macro	VLVGF	v, gr, index
	VLVG	\v, \gr, \index, 2
.endm
.macro	VLVGG	v, gr, index
	VLVG	\v, \gr, \index, 3
.endm

/* VECTOR LOAD REGISTER */
.macro	VLR	v1, v2
	VX_NUM	v1, \v1
	VX_NUM	v2, \v2
	.word	0xE700 | ((v1&15) << 4) | (v2&15)
	.word	0
	MRXBOPC	0, 0x56, v1, v2
.endm

/* VECTOR LOAD */
.macro	VL	v, disp, index="%r0", base
	VX_NUM	v1, \v
	GR_NUM	x2, \index
	GR_NUM	b2, \base
	.word	0xE700 | ((v1&15) << 4) | x2
	.word	(b2 << 12) | (\disp)
	MRXBOPC 0, 0x06, v1
.endm

/* VECTOR LOAD ELEMENT */
.macro	VLEx	vr1, disp, index="%r0", base, m3, opc
	VX_NUM	v1, \vr1
	GR_NUM	x2, \index
	GR_NUM	b2, \base
	.word	0xE700 | ((v1&15) << 4) | x2
	.word	(b2 << 12) | (\disp)
	MRXBOPC	\m3, \opc, v1
.endm
.macro	VLEB	vr1, disp, index="%r0", base, m3
	VLEx	\vr1, \disp, \index, \base, \m3, 0x00
.endm
.macro	VLEH	vr1, disp, index="%r0", base, m3
	VLEx	\vr1, \disp, \index, \base, \m3, 0x01
.endm
.macro	VLEF	vr1, disp, index="%r0", base, m3
	VLEx	\vr1, \disp, \index, \base, \m3, 0x03
.endm
.macro	VLEG	vr1, disp, index="%r0", base, m3
	VLEx	\vr1, \disp, \index, \base, \m3, 0x02
.endm

/* VECTOR LOAD ELEMENT IMMEDIATE */
.macro	VLEIx	vr1, imm2, m3, opc
	VX_NUM	v1, \vr1
	.word	0xE700 | ((v1&15) << 4)
	.word	\imm2
	MRXBOPC	\m3, \opc, v1
.endm
.macro	VLEIB	vr1, imm2, index
	VLEIx	\vr1, \imm2, \index, 0x40
.endm
.macro	VLEIH	vr1, imm2, index
	VLEIx	\vr1, \imm2, \index, 0x41
.endm
.macro	VLEIF	vr1, imm2, index
	VLEIx	\vr1, \imm2, \index, 0x43
.endm
.macro	VLEIG	vr1, imm2, index
	VLEIx	\vr1, \imm2, \index, 0x42
.endm

/* VECTOR LOAD GR FROM VR ELEMENT */
.macro	VLGV	gr, vr, disp, base="%r0", m
	GR_NUM	r1, \gr
	GR_NUM	b2, \base
	VX_NUM	v3, \vr
	.word	0xE700 | (r1 << 4) | (v3&15)
	.word	(b2 << 12) | (\disp)
	MRXBOPC	\m, 0x21, 0, v3
.endm
.macro	VLGVB	gr, vr, disp, base="%r0"
	VLGV	\gr, \vr, \disp, \base, 0
.endm
.macro	VLGVH	gr, vr, disp, base="%r0"
	VLGV	\gr, \vr, \disp, \base, 1
.endm
.macro	VLGVF	gr, vr, disp, base="%r0"
	VLGV	\gr, \vr, \disp, \base, 2
.endm
.macro	VLGVG	gr, vr, disp, base="%r0"
	VLGV	\gr, \vr, \disp, \base, 3
.endm

/* VECTOR LOAD MULTIPLE */
.macro	VLM	vfrom, vto, disp, base, hint=3
	VX_NUM	v1, \vfrom
	VX_NUM	v3, \vto
	GR_NUM	b2, \base
	.word	0xE700 | ((v1&15) << 4) | (v3&15)
	.word	(b2 << 12) | (\disp)
	MRXBOPC	\hint, 0x36, v1, v3
.endm

/* VECTOR STORE */
.macro	VST	vr1, disp, index="%r0", base
	VX_NUM	v1, \vr1
	GR_NUM	x2, \index
	GR_NUM	b2, \base
	.word	0xE700 | ((v1&15) << 4) | (x2&15)
	.word	(b2 << 12) | (\disp)
	MRXBOPC	0, 0x0E, v1
.endm

/* VECTOR STORE MULTIPLE */
.macro	VSTM	vfrom, vto, disp, base, hint=3
	VX_NUM	v1, \vfrom
	VX_NUM	v3, \vto
	GR_NUM	b2, \base
	.word	0xE700 | ((v1&15) << 4) | (v3&15)
	.word	(b2 << 12) | (\disp)
	MRXBOPC	\hint, 0x3E, v1, v3
.endm

/* VECTOR PERMUTE */
.macro	VPERM	vr1, vr2, vr3, vr4
	VX_NUM	v1, \vr1
	VX_NUM	v2, \vr2
	VX_NUM	v3, \vr3
	VX_NUM	v4, \vr4
	.word	0xE700 | ((v1&15) << 4) | (v2&15)
	.word	((v3&15) << 12)
	MRXBOPC	(v4&15), 0x8C, v1, v2, v3, v4
.endm

/* VECTOR UNPACK LOGICAL LOW */
.macro	VUPLL	vr1, vr2, m3
	VX_NUM	v1, \vr1
	VX_NUM	v2, \vr2
	.word	0xE700 | ((v1&15) << 4) | (v2&15)
	.word	0x0000
	MRXBOPC	\m3, 0xD4, v1, v2
.endm
.macro	VUPLLB	vr1, vr2
	VUPLL	\vr1, \vr2, 0
.endm
.macro	VUPLLH	vr1, vr2
	VUPLL	\vr1, \vr2, 1
.endm
.macro	VUPLLF	vr1, vr2
	VUPLL	\vr1, \vr2, 2
.endm

/* VECTOR PERMUTE DOUBLEWORD IMMEDIATE */
.macro	VPDI	vr1, vr2, vr3, m4
	VX_NUM	v1, \vr1
	VX_NUM	v2, \vr2
	VX_NUM	v3, \vr3
	.word	0xE700 | ((v1&15) << 4) | (v2&15)
	.word	((v3&15) << 12)
	MRXBOPC	\m4, 0x84, v1, v2, v3
.endm

/* VECTOR REPLICATE */
.macro	VREP	vr1, vr3, imm2, m4
	VX_NUM	v1, \vr1
	VX_NUM	v3, \vr3
	.word	0xE700 | ((v1&15) << 4) | (v3&15)
	.word	\imm2
	MRXBOPC	\m4, 0x4D, v1, v3
.endm
.macro	VREPB	vr1, vr3, imm2
	VREP	\vr1, \vr3, \imm2, 0
.endm
.macro	VREPH	vr1, vr3, imm2
	VREP	\vr1, \vr3, \imm2, 1
.endm
.macro	VREPF	vr1, vr3, imm2
	VREP	\vr1, \vr3, \imm2, 2
.endm
.macro	VREPG	vr1, vr3, imm2
	VREP	\vr1, \vr3, \imm2, 3
.endm

/* VECTOR MERGE HIGH */
.macro	VMRH	vr1, vr2, vr3, m4
	VX_NUM	v1, \vr1
	VX_NUM	v2, \vr2
	VX_NUM	v3, \vr3
	.word	0xE700 | ((v1&15) << 4) | (v2&15)
	.word	((v3&15) << 12)
	MRXBOPC	\m4, 0x61, v1, v2, v3
.endm
.macro	VMRHB	vr1, vr2, vr3
	VMRH	\vr1, \vr2, \vr3, 0
.endm
.macro	VMRHH	vr1, vr2, vr3
	VMRH	\vr1, \vr2, \vr3, 1
.endm
.macro	VMRHF	vr1, vr2, vr3
	VMRH	\vr1, \vr2, \vr3, 2
.endm
.macro	VMRHG	vr1, vr2, vr3
	VMRH	\vr1, \vr2, \vr3, 3
.endm

/* VECTOR MERGE LOW */
.macro	VMRL	vr1, vr2, vr3, m4
	VX_NUM	v1, \vr1
	VX_NUM	v2, \vr2
	VX_NUM	v3, \vr3
	.word	0xE700 | ((v1&15) << 4) | (v2&15)
	.word	((v3&15) << 12)
	MRXBOPC	\m4, 0x60, v1, v2, v3
.endm
.macro	VMRLB	vr1, vr2, vr3
	VMRL	\vr1, \vr2, \vr3, 0
.endm
.macro	VMRLH	vr1, vr2, vr3
	VMRL	\vr1, \vr2, \vr3, 1
.endm
.macro	VMRLF	vr1, vr2, vr3
	VMRL	\vr1, \vr2, \vr3, 2
.endm
.macro	VMRLG	vr1, vr2, vr3
	VMRL	\vr1, \vr2, \vr3, 3
.endm

/* VECTOR LOAD WITH LENGTH */
.macro VLL	v, gr, disp, base
	VX_NUM	v1, \v
	GR_NUM	b2, \base
	GR_NUM	r3, \gr
	.word	0xE700 | ((v1&15) << 4) | r3
	.word	(b2 << 12) | (\disp)
	MRXBOPC 0, 0x37, v1
.endm

/* VECTOR STORE WITH LENGTH */
.macro VSTL	v, gr, disp, base
	VX_NUM	v1, \v
	GR_NUM	b2, \base
	GR_NUM	r3, \gr
	.word	0xE700 | ((v1&15) << 4) | r3
	.word	(b2 << 12) | (\disp)
	MRXBOPC 0, 0x3f, v1
.endm

/* Vector integer instructions */

/* VECTOR AND */
.macro	VN	vr1, vr2, vr3
	VX_NUM	v1, \vr1
	VX_NUM	v2, \vr2
	VX_NUM	v3, \vr3
	.word	0xE700 | ((v1&15) << 4) | (v2&15)
	.word	((v3&15) << 12)
	MRXBOPC	0, 0x68, v1, v2, v3
.endm

/* VECTOR CHECKSUM */
.macro VCKSM	vr1, vr2, vr3
	VX_NUM	v1, \vr1
	VX_NUM	v2, \vr2
	VX_NUM	v3, \vr3
	.word	0xE700 | ((v1&15) << 4) | (v2&15)
	.word	((v3&15) << 12)
	MRXBOPC 0, 0x66, v1, v2, v3
.endm

/* VECTOR EXCLUSIVE OR */
.macro	VX	vr1, vr2, vr3
	VX_NUM	v1, \vr1
	VX_NUM	v2, \vr2
	VX_NUM	v3, \vr3
	.word	0xE700 | ((v1&15) << 4) | (v2&15)
	.word	((v3&15) << 12)
	MRXBOPC	0, 0x6D, v1, v2, v3
.endm

/* VECTOR GALOIS FIELD MULTIPLY SUM */
.macro	VGFM	vr1, vr2, vr3, m4
	VX_NUM	v1, \vr1
	VX_NUM	v2, \vr2
	VX_NUM	v3, \vr3
	.word	0xE700 | ((v1&15) << 4) | (v2&15)
	.word	((v3&15) << 12)
	MRXBOPC	\m4, 0xB4, v1, v2, v3
.endm
.macro	VGFMB	vr1, vr2, vr3
	VGFM	\vr1, \vr2, \vr3, 0
.endm
.macro	VGFMH	vr1, vr2, vr3
	VGFM	\vr1, \vr2, \vr3, 1
.endm
.macro	VGFMF	vr1, vr2, vr3
	VGFM	\vr1, \vr2, \vr3, 2
.endm
.macro	VGFMG	vr1, vr2, vr3
	VGFM	\vr1, \vr2, \vr3, 3
.endm

/* VECTOR GALOIS FIELD MULTIPLY SUM AND ACCUMULATE */
.macro	VGFMA	vr1, vr2, vr3, vr4, m5
	VX_NUM	v1, \vr1
	VX_NUM	v2, \vr2
	VX_NUM	v3, \vr3
	VX_NUM	v4, \vr4
	.word	0xE700 | ((v1&15) << 4) | (v2&15)
	.word	((v3&15) << 12) | (\m5 << 8)
	MRXBOPC	(v4&15), 0xBC, v1, v2, v3, v4
.endm
.macro	VGFMAB	vr1, vr2, vr3, vr4
	VGFMA	\vr1, \vr2, \vr3, \vr4, 0
.endm
.macro	VGFMAH	vr1, vr2, vr3, vr4
	VGFMA	\vr1, \vr2, \vr3, \vr4, 1
.endm
.macro	VGFMAF	vr1, vr2, vr3, vr4
	VGFMA	\vr1, \vr2, \vr3, \vr4, 2
.endm
.macro	VGFMAG	vr1, vr2, vr3, vr4
	VGFMA	\vr1, \vr2, \vr3, \vr4, 3
.endm

/* VECTOR SHIFT RIGHT LOGICAL BY BYTE */
.macro	VSRLB	vr1, vr2, vr3
	VX_NUM	v1, \vr1
	VX_NUM	v2, \vr2
	VX_NUM	v3, \vr3
	.word	0xE700 | ((v1&15) << 4) | (v2&15)
	.word	((v3&15) << 12)
	MRXBOPC	0, 0x7D, v1, v2, v3
.endm

/* VECTOR REPLICATE IMMEDIATE */
.macro	VREPI	vr1, imm2, m3
	VX_NUM	v1, \vr1
	.word	0xE700 | ((v1&15) << 4)
	.word	\imm2
	MRXBOPC	\m3, 0x45, v1
.endm
.macro	VREPIB	vr1, imm2
	VREPI	\vr1, \imm2, 0
.endm
.macro	VREPIH	vr1, imm2
	VREPI	\vr1, \imm2, 1
.endm
.macro	VREPIF	vr1, imm2
	VREPI	\vr1, \imm2, 2
.endm
.macro	VREPIG	vr1, imm2
	VREP	\vr1, \imm2, 3
.endm

/* VECTOR ADD */
.macro	VA	vr1, vr2, vr3, m4
	VX_NUM	v1, \vr1
	VX_NUM	v2, \vr2
	VX_NUM	v3, \vr3
	.word	0xE700 | ((v1&15) << 4) | (v2&15)
	.word	((v3&15) << 12)
	MRXBOPC	\m4, 0xF3, v1, v2, v3
.endm
.macro	VAB	vr1, vr2, vr3
	VA	\vr1, \vr2, \vr3, 0
.endm
.macro	VAH	vr1, vr2, vr3
	VA	\vr1, \vr2, \vr3, 1
.endm
.macro	VAF	vr1, vr2, vr3
	VA	\vr1, \vr2, \vr3, 2
.endm
.macro	VAG	vr1, vr2, vr3
	VA	\vr1, \vr2, \vr3, 3
.endm
.macro	VAQ	vr1, vr2, vr3
	VA	\vr1, \vr2, \vr3, 4
.endm

/* VECTOR ELEMENT SHIFT RIGHT ARITHMETIC */
.macro	VESRAV	vr1, vr2, vr3, m4
	VX_NUM	v1, \vr1
	VX_NUM	v2, \vr2
	VX_NUM	v3, \vr3
	.word	0xE700 | ((v1&15) << 4) | (v2&15)
	.word	((v3&15) << 12)
	MRXBOPC \m4, 0x7A, v1, v2, v3
.endm

.macro	VESRAVB	vr1, vr2, vr3
	VESRAV	\vr1, \vr2, \vr3, 0
.endm
.macro	VESRAVH	vr1, vr2, vr3
	VESRAV	\vr1, \vr2, \vr3, 1
.endm
.macro	VESRAVF	vr1, vr2, vr3
	VESRAV	\vr1, \vr2, \vr3, 2
.endm
.macro	VESRAVG	vr1, vr2, vr3
	VESRAV	\vr1, \vr2, \vr3, 3
.endm

/* VECTOR ELEMENT ROTATE LEFT LOGICAL */
.macro	VERLL	vr1, vr3, disp, base="%r0", m4
	VX_NUM	v1, \vr1
	VX_NUM	v3, \vr3
	GR_NUM	b2, \base
	.word	0xE700 | ((v1&15) << 4) | (v3&15)
	.word	(b2 << 12) | (\disp)
	MRXBOPC	\m4, 0x33, v1, v3
.endm
.macro	VERLLB	vr1, vr3, disp, base="%r0"
	VERLL	\vr1, \vr3, \disp, \base, 0
.endm
.macro	VERLLH	vr1, vr3, disp, base="%r0"
	VERLL	\vr1, \vr3, \disp, \base, 1
.endm
.macro	VERLLF	vr1, vr3, disp, base="%r0"
	VERLL	\vr1, \vr3, \disp, \base, 2
.endm
.macro	VERLLG	vr1, vr3, disp, base="%r0"
	VERLL	\vr1, \vr3, \disp, \base, 3
.endm

/* VECTOR SHIFT LEFT DOUBLE BY BYTE */
.macro	VSLDB	vr1, vr2, vr3, imm4
	VX_NUM	v1, \vr1
	VX_NUM	v2, \vr2
	VX_NUM	v3, \vr3
	.word	0xE700 | ((v1&15) << 4) | (v2&15)
	.word	((v3&15) << 12) | (\imm4)
	MRXBOPC	0, 0x77, v1, v2, v3
.endm

#endif	/* __ASSEMBLY__ */
#endif	/* __ASM_S390_FPU_INSN_ASM_H */
