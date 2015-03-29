/*
 * Hibernation support specific for mips - temporary page tables
 *
 * Licensed under the GPLv2
 *
 * Copyright (C) 2009 Lemote Inc.
 * Author: Hu Hongbing <huhb@lemote.com>
 *	   Wu Zhangjin <wuzhangjin@gmail.com>
 */
#include <asm/asm-offsets.h>
#include <asm/regdef.h>
#include <asm/asm.h>

.text
LEAF(swsusp_arch_suspend)
	PTR_LA t0, saved_regs
	PTR_S ra, PT_R31(t0)
	PTR_S sp, PT_R29(t0)
	PTR_S fp, PT_R30(t0)
	PTR_S gp, PT_R28(t0)
	PTR_S s0, PT_R16(t0)
	PTR_S s1, PT_R17(t0)
	PTR_S s2, PT_R18(t0)
	PTR_S s3, PT_R19(t0)
	PTR_S s4, PT_R20(t0)
	PTR_S s5, PT_R21(t0)
	PTR_S s6, PT_R22(t0)
	PTR_S s7, PT_R23(t0)
	j swsusp_save
END(swsusp_arch_suspend)

LEAF(restore_image)
	PTR_L t0, restore_pblist
0:
	PTR_L t1, PBE_ADDRESS(t0)   /* source */
	PTR_L t2, PBE_ORIG_ADDRESS(t0) /* destination */
	PTR_ADDU t3, t1, _PAGE_SIZE
1:
	REG_L t8, (t1)
	REG_S t8, (t2)
	PTR_ADDIU t1, t1, SZREG
	PTR_ADDIU t2, t2, SZREG
	bne t1, t3, 1b
	PTR_L t0, PBE_NEXT(t0)
	bnez t0, 0b
	PTR_LA t0, saved_regs
	PTR_L ra, PT_R31(t0)
	PTR_L sp, PT_R29(t0)
	PTR_L fp, PT_R30(t0)
	PTR_L gp, PT_R28(t0)
	PTR_L s0, PT_R16(t0)
	PTR_L s1, PT_R17(t0)
	PTR_L s2, PT_R18(t0)
	PTR_L s3, PT_R19(t0)
	PTR_L s4, PT_R20(t0)
	PTR_L s5, PT_R21(t0)
	PTR_L s6, PT_R22(t0)
	PTR_L s7, PT_R23(t0)
	PTR_LI v0, 0x0
	jr ra
END(restore_image)
