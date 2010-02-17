/*
 * Generate .byte code for some instructions not supported by old
 * binutils.
 */
#ifndef X86_ASM_INST_H
#define X86_ASM_INST_H

#ifdef __ASSEMBLY__

	.macro XMM_NUM opd xmm
	.ifc \xmm,%xmm0
	\opd = 0
	.endif
	.ifc \xmm,%xmm1
	\opd = 1
	.endif
	.ifc \xmm,%xmm2
	\opd = 2
	.endif
	.ifc \xmm,%xmm3
	\opd = 3
	.endif
	.ifc \xmm,%xmm4
	\opd = 4
	.endif
	.ifc \xmm,%xmm5
	\opd = 5
	.endif
	.ifc \xmm,%xmm6
	\opd = 6
	.endif
	.ifc \xmm,%xmm7
	\opd = 7
	.endif
	.ifc \xmm,%xmm8
	\opd = 8
	.endif
	.ifc \xmm,%xmm9
	\opd = 9
	.endif
	.ifc \xmm,%xmm10
	\opd = 10
	.endif
	.ifc \xmm,%xmm11
	\opd = 11
	.endif
	.ifc \xmm,%xmm12
	\opd = 12
	.endif
	.ifc \xmm,%xmm13
	\opd = 13
	.endif
	.ifc \xmm,%xmm14
	\opd = 14
	.endif
	.ifc \xmm,%xmm15
	\opd = 15
	.endif
	.endm

	.macro PFX_OPD_SIZE
	.byte 0x66
	.endm

	.macro PFX_REX opd1 opd2
	.if (\opd1 | \opd2) & 8
	.byte 0x40 | ((\opd1 & 8) >> 3) | ((\opd2 & 8) >> 1)
	.endif
	.endm

	.macro MODRM mod opd1 opd2
	.byte \mod | (\opd1 & 7) | ((\opd2 & 7) << 3)
	.endm

	.macro PSHUFB_XMM xmm1 xmm2
	XMM_NUM pshufb_opd1 \xmm1
	XMM_NUM pshufb_opd2 \xmm2
	PFX_OPD_SIZE
	PFX_REX pshufb_opd1 pshufb_opd2
	.byte 0x0f, 0x38, 0x00
	MODRM 0xc0 pshufb_opd1 pshufb_opd2
	.endm

	.macro PCLMULQDQ imm8 xmm1 xmm2
	XMM_NUM clmul_opd1 \xmm1
	XMM_NUM clmul_opd2 \xmm2
	PFX_OPD_SIZE
	PFX_REX clmul_opd1 clmul_opd2
	.byte 0x0f, 0x3a, 0x44
	MODRM 0xc0 clmul_opd1 clmul_opd2
	.byte \imm8
	.endm

	.macro AESKEYGENASSIST rcon xmm1 xmm2
	XMM_NUM aeskeygen_opd1 \xmm1
	XMM_NUM aeskeygen_opd2 \xmm2
	PFX_OPD_SIZE
	PFX_REX aeskeygen_opd1 aeskeygen_opd2
	.byte 0x0f, 0x3a, 0xdf
	MODRM 0xc0 aeskeygen_opd1 aeskeygen_opd2
	.byte \rcon
	.endm

	.macro AESIMC xmm1 xmm2
	XMM_NUM aesimc_opd1 \xmm1
	XMM_NUM aesimc_opd2 \xmm2
	PFX_OPD_SIZE
	PFX_REX aesimc_opd1 aesimc_opd2
	.byte 0x0f, 0x38, 0xdb
	MODRM 0xc0 aesimc_opd1 aesimc_opd2
	.endm

	.macro AESENC xmm1 xmm2
	XMM_NUM aesenc_opd1 \xmm1
	XMM_NUM aesenc_opd2 \xmm2
	PFX_OPD_SIZE
	PFX_REX aesenc_opd1 aesenc_opd2
	.byte 0x0f, 0x38, 0xdc
	MODRM 0xc0 aesenc_opd1 aesenc_opd2
	.endm

	.macro AESENCLAST xmm1 xmm2
	XMM_NUM aesenclast_opd1 \xmm1
	XMM_NUM aesenclast_opd2 \xmm2
	PFX_OPD_SIZE
	PFX_REX aesenclast_opd1 aesenclast_opd2
	.byte 0x0f, 0x38, 0xdd
	MODRM 0xc0 aesenclast_opd1 aesenclast_opd2
	.endm

	.macro AESDEC xmm1 xmm2
	XMM_NUM aesdec_opd1 \xmm1
	XMM_NUM aesdec_opd2 \xmm2
	PFX_OPD_SIZE
	PFX_REX aesdec_opd1 aesdec_opd2
	.byte 0x0f, 0x38, 0xde
	MODRM 0xc0 aesdec_opd1 aesdec_opd2
	.endm

	.macro AESDECLAST xmm1 xmm2
	XMM_NUM aesdeclast_opd1 \xmm1
	XMM_NUM aesdeclast_opd2 \xmm2
	PFX_OPD_SIZE
	PFX_REX aesdeclast_opd1 aesdeclast_opd2
	.byte 0x0f, 0x38, 0xdf
	MODRM 0xc0 aesdeclast_opd1 aesdeclast_opd2
	.endm
#endif

#endif
