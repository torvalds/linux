/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Hibernation support specific for LoongArch
 *
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/regdef.h>

.text
SYM_FUNC_START(swsusp_asm_suspend)
	la.pcrel	t0, saved_regs
	PTR_S		ra, t0, PT_R1
	PTR_S		tp, t0, PT_R2
	PTR_S		sp, t0, PT_R3
	PTR_S		u0, t0, PT_R21
	PTR_S		fp, t0, PT_R22
	PTR_S		s0, t0, PT_R23
	PTR_S		s1, t0, PT_R24
	PTR_S		s2, t0, PT_R25
	PTR_S		s3, t0, PT_R26
	PTR_S		s4, t0, PT_R27
	PTR_S		s5, t0, PT_R28
	PTR_S		s6, t0, PT_R29
	PTR_S		s7, t0, PT_R30
	PTR_S		s8, t0, PT_R31
	b		swsusp_save
SYM_FUNC_END(swsusp_asm_suspend)

SYM_FUNC_START(swsusp_asm_resume)
	la.pcrel	t0, restore_pblist
	PTR_L		t0, t0, 0
0:
	PTR_L		t1, t0, PBE_ADDRESS  /* source */
	PTR_L		t2, t0, PBE_ORIG_ADDRESS /* destination */
	PTR_LI		t3, _PAGE_SIZE
	PTR_ADD		t3, t3, t1
1:
	REG_L		t8, t1, 0
	REG_S		t8, t2, 0
	PTR_ADDI	t1, t1, SZREG
	PTR_ADDI	t2, t2, SZREG
	bne		t1, t3, 1b
	PTR_L		t0, t0, PBE_NEXT
	bnez		t0, 0b
	la.pcrel	t0, saved_regs
	PTR_L		ra, t0, PT_R1
	PTR_L		tp, t0, PT_R2
	PTR_L		sp, t0, PT_R3
	PTR_L		u0, t0, PT_R21
	PTR_L		fp, t0, PT_R22
	PTR_L		s0, t0, PT_R23
	PTR_L		s1, t0, PT_R24
	PTR_L		s2, t0, PT_R25
	PTR_L		s3, t0, PT_R26
	PTR_L		s4, t0, PT_R27
	PTR_L		s5, t0, PT_R28
	PTR_L		s6, t0, PT_R29
	PTR_L		s7, t0, PT_R30
	PTR_L		s8, t0, PT_R31
	PTR_LI		a0, 0x0
	jirl		zero, ra, 0
SYM_FUNC_END(swsusp_asm_resume)
