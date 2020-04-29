/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __NDS32_ASSEMBLER_H__
#define __NDS32_ASSEMBLER_H__

.macro gie_disable
	setgie.d
	dsb
.endm

.macro gie_enable
	setgie.e
	dsb
.endm

.macro gie_save oldpsw
	mfsr \oldpsw, $ir0
	setgie.d
        dsb
.endm

.macro gie_restore oldpsw
	andi \oldpsw, \oldpsw, #0x1
	beqz \oldpsw, 7001f
	setgie.e
	dsb
7001:
.endm


#define USER(insn,  reg, addr, opr)	\
9999:	insn  reg, addr, opr;		\
	.section __ex_table,"a";	\
	.align 3;			\
	.long	9999b, 9001f;		\
	.previous

#endif /* __NDS32_ASSEMBLER_H__ */
