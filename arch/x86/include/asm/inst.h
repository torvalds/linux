/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Generate .byte code for some instructions not supported by old
 * binutils.
 */
#ifndef X86_ASM_INST_H
#define X86_ASM_INST_H

#ifdef __ASSEMBLY__

#define REG_NUM_INVALID		100

#define REG_TYPE_R32		0
#define REG_TYPE_R64		1
#define REG_TYPE_INVALID	100

	.macro R32_NUM opd r32
	\opd = REG_NUM_INVALID
	.ifc \r32,%eax
	\opd = 0
	.endif
	.ifc \r32,%ecx
	\opd = 1
	.endif
	.ifc \r32,%edx
	\opd = 2
	.endif
	.ifc \r32,%ebx
	\opd = 3
	.endif
	.ifc \r32,%esp
	\opd = 4
	.endif
	.ifc \r32,%ebp
	\opd = 5
	.endif
	.ifc \r32,%esi
	\opd = 6
	.endif
	.ifc \r32,%edi
	\opd = 7
	.endif
#ifdef CONFIG_X86_64
	.ifc \r32,%r8d
	\opd = 8
	.endif
	.ifc \r32,%r9d
	\opd = 9
	.endif
	.ifc \r32,%r10d
	\opd = 10
	.endif
	.ifc \r32,%r11d
	\opd = 11
	.endif
	.ifc \r32,%r12d
	\opd = 12
	.endif
	.ifc \r32,%r13d
	\opd = 13
	.endif
	.ifc \r32,%r14d
	\opd = 14
	.endif
	.ifc \r32,%r15d
	\opd = 15
	.endif
#endif
	.endm

	.macro R64_NUM opd r64
	\opd = REG_NUM_INVALID
#ifdef CONFIG_X86_64
	.ifc \r64,%rax
	\opd = 0
	.endif
	.ifc \r64,%rcx
	\opd = 1
	.endif
	.ifc \r64,%rdx
	\opd = 2
	.endif
	.ifc \r64,%rbx
	\opd = 3
	.endif
	.ifc \r64,%rsp
	\opd = 4
	.endif
	.ifc \r64,%rbp
	\opd = 5
	.endif
	.ifc \r64,%rsi
	\opd = 6
	.endif
	.ifc \r64,%rdi
	\opd = 7
	.endif
	.ifc \r64,%r8
	\opd = 8
	.endif
	.ifc \r64,%r9
	\opd = 9
	.endif
	.ifc \r64,%r10
	\opd = 10
	.endif
	.ifc \r64,%r11
	\opd = 11
	.endif
	.ifc \r64,%r12
	\opd = 12
	.endif
	.ifc \r64,%r13
	\opd = 13
	.endif
	.ifc \r64,%r14
	\opd = 14
	.endif
	.ifc \r64,%r15
	\opd = 15
	.endif
#endif
	.endm

	.macro REG_TYPE type reg
	R32_NUM reg_type_r32 \reg
	R64_NUM reg_type_r64 \reg
	.if reg_type_r64 <> REG_NUM_INVALID
	\type = REG_TYPE_R64
	.elseif reg_type_r32 <> REG_NUM_INVALID
	\type = REG_TYPE_R32
	.else
	\type = REG_TYPE_INVALID
	.endif
	.endm

	.macro PFX_REX opd1 opd2 W=0
	.if ((\opd1 | \opd2) & 8) || \W
	.byte 0x40 | ((\opd1 & 8) >> 3) | ((\opd2 & 8) >> 1) | (\W << 3)
	.endif
	.endm

	.macro MODRM mod opd1 opd2
	.byte \mod | (\opd1 & 7) | ((\opd2 & 7) << 3)
	.endm
#endif

#endif
