/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_ASMMACRO_H
#define _ASM_ASMMACRO_H

#include <asm/asm-offsets.h>
#include <asm/regdef.h>
#include <asm/fpregdef.h>
#include <asm/loongarch.h>

	.macro	cpu_save_nonscratch thread
	stptr.d	s0, \thread, THREAD_REG23
	stptr.d	s1, \thread, THREAD_REG24
	stptr.d	s2, \thread, THREAD_REG25
	stptr.d	s3, \thread, THREAD_REG26
	stptr.d	s4, \thread, THREAD_REG27
	stptr.d	s5, \thread, THREAD_REG28
	stptr.d	s6, \thread, THREAD_REG29
	stptr.d	s7, \thread, THREAD_REG30
	stptr.d	s8, \thread, THREAD_REG31
	stptr.d	sp, \thread, THREAD_REG03
	stptr.d	fp, \thread, THREAD_REG22
	.endm

	.macro	cpu_restore_nonscratch thread
	ldptr.d	s0, \thread, THREAD_REG23
	ldptr.d	s1, \thread, THREAD_REG24
	ldptr.d	s2, \thread, THREAD_REG25
	ldptr.d	s3, \thread, THREAD_REG26
	ldptr.d	s4, \thread, THREAD_REG27
	ldptr.d	s5, \thread, THREAD_REG28
	ldptr.d	s6, \thread, THREAD_REG29
	ldptr.d	s7, \thread, THREAD_REG30
	ldptr.d	s8, \thread, THREAD_REG31
	ldptr.d	ra, \thread, THREAD_REG01
	ldptr.d	sp, \thread, THREAD_REG03
	ldptr.d	fp, \thread, THREAD_REG22
	.endm

	.macro fpu_save_csr thread tmp
	movfcsr2gr	\tmp, fcsr0
	stptr.w		\tmp, \thread, THREAD_FCSR
#ifdef CONFIG_CPU_HAS_LBT
	/* TM bit is always 0 if LBT not supported */
	andi		\tmp, \tmp, FPU_CSR_TM
	beqz		\tmp, 1f
	/* Save FTOP */
	x86mftop	\tmp
	stptr.w		\tmp, \thread, THREAD_FTOP
	/* Turn off TM to ensure the order of FPR in memory independent of TM */
	x86clrtm
1:
#endif
	.endm

	.macro fpu_restore_csr thread tmp0 tmp1
	ldptr.w		\tmp0, \thread, THREAD_FCSR
	movgr2fcsr	fcsr0, \tmp0
#ifdef CONFIG_CPU_HAS_LBT
	/* TM bit is always 0 if LBT not supported */
	andi		\tmp0, \tmp0, FPU_CSR_TM
	beqz		\tmp0, 2f
	/* Restore FTOP */
	ldptr.w		\tmp0, \thread, THREAD_FTOP
	andi		\tmp0, \tmp0, 0x7
	la.pcrel	\tmp1, 1f
	alsl.d		\tmp1, \tmp0, \tmp1, 3
	jr		\tmp1
1:
	x86mttop	0
	b	2f
	x86mttop	1
	b	2f
	x86mttop	2
	b	2f
	x86mttop	3
	b	2f
	x86mttop	4
	b	2f
	x86mttop	5
	b	2f
	x86mttop	6
	b	2f
	x86mttop	7
2:
#endif
	.endm

	.macro fpu_save_cc thread tmp0 tmp1
	movcf2gr	\tmp0, $fcc0
	move	\tmp1, \tmp0
	movcf2gr	\tmp0, $fcc1
	bstrins.d	\tmp1, \tmp0, 15, 8
	movcf2gr	\tmp0, $fcc2
	bstrins.d	\tmp1, \tmp0, 23, 16
	movcf2gr	\tmp0, $fcc3
	bstrins.d	\tmp1, \tmp0, 31, 24
	movcf2gr	\tmp0, $fcc4
	bstrins.d	\tmp1, \tmp0, 39, 32
	movcf2gr	\tmp0, $fcc5
	bstrins.d	\tmp1, \tmp0, 47, 40
	movcf2gr	\tmp0, $fcc6
	bstrins.d	\tmp1, \tmp0, 55, 48
	movcf2gr	\tmp0, $fcc7
	bstrins.d	\tmp1, \tmp0, 63, 56
	stptr.d		\tmp1, \thread, THREAD_FCC
	.endm

	.macro fpu_restore_cc thread tmp0 tmp1
	ldptr.d	\tmp0, \thread, THREAD_FCC
	bstrpick.d	\tmp1, \tmp0, 7, 0
	movgr2cf	$fcc0, \tmp1
	bstrpick.d	\tmp1, \tmp0, 15, 8
	movgr2cf	$fcc1, \tmp1
	bstrpick.d	\tmp1, \tmp0, 23, 16
	movgr2cf	$fcc2, \tmp1
	bstrpick.d	\tmp1, \tmp0, 31, 24
	movgr2cf	$fcc3, \tmp1
	bstrpick.d	\tmp1, \tmp0, 39, 32
	movgr2cf	$fcc4, \tmp1
	bstrpick.d	\tmp1, \tmp0, 47, 40
	movgr2cf	$fcc5, \tmp1
	bstrpick.d	\tmp1, \tmp0, 55, 48
	movgr2cf	$fcc6, \tmp1
	bstrpick.d	\tmp1, \tmp0, 63, 56
	movgr2cf	$fcc7, \tmp1
	.endm

	.macro	fpu_save_double thread tmp
	li.w	\tmp, THREAD_FPR0
	PTR_ADD \tmp, \tmp, \thread
	fst.d	$f0, \tmp, THREAD_FPR0  - THREAD_FPR0
	fst.d	$f1, \tmp, THREAD_FPR1  - THREAD_FPR0
	fst.d	$f2, \tmp, THREAD_FPR2  - THREAD_FPR0
	fst.d	$f3, \tmp, THREAD_FPR3  - THREAD_FPR0
	fst.d	$f4, \tmp, THREAD_FPR4  - THREAD_FPR0
	fst.d	$f5, \tmp, THREAD_FPR5  - THREAD_FPR0
	fst.d	$f6, \tmp, THREAD_FPR6  - THREAD_FPR0
	fst.d	$f7, \tmp, THREAD_FPR7  - THREAD_FPR0
	fst.d	$f8, \tmp, THREAD_FPR8  - THREAD_FPR0
	fst.d	$f9, \tmp, THREAD_FPR9  - THREAD_FPR0
	fst.d	$f10, \tmp, THREAD_FPR10 - THREAD_FPR0
	fst.d	$f11, \tmp, THREAD_FPR11 - THREAD_FPR0
	fst.d	$f12, \tmp, THREAD_FPR12 - THREAD_FPR0
	fst.d	$f13, \tmp, THREAD_FPR13 - THREAD_FPR0
	fst.d	$f14, \tmp, THREAD_FPR14 - THREAD_FPR0
	fst.d	$f15, \tmp, THREAD_FPR15 - THREAD_FPR0
	fst.d	$f16, \tmp, THREAD_FPR16 - THREAD_FPR0
	fst.d	$f17, \tmp, THREAD_FPR17 - THREAD_FPR0
	fst.d	$f18, \tmp, THREAD_FPR18 - THREAD_FPR0
	fst.d	$f19, \tmp, THREAD_FPR19 - THREAD_FPR0
	fst.d	$f20, \tmp, THREAD_FPR20 - THREAD_FPR0
	fst.d	$f21, \tmp, THREAD_FPR21 - THREAD_FPR0
	fst.d	$f22, \tmp, THREAD_FPR22 - THREAD_FPR0
	fst.d	$f23, \tmp, THREAD_FPR23 - THREAD_FPR0
	fst.d	$f24, \tmp, THREAD_FPR24 - THREAD_FPR0
	fst.d	$f25, \tmp, THREAD_FPR25 - THREAD_FPR0
	fst.d	$f26, \tmp, THREAD_FPR26 - THREAD_FPR0
	fst.d	$f27, \tmp, THREAD_FPR27 - THREAD_FPR0
	fst.d	$f28, \tmp, THREAD_FPR28 - THREAD_FPR0
	fst.d	$f29, \tmp, THREAD_FPR29 - THREAD_FPR0
	fst.d	$f30, \tmp, THREAD_FPR30 - THREAD_FPR0
	fst.d	$f31, \tmp, THREAD_FPR31 - THREAD_FPR0
	.endm

	.macro	fpu_restore_double thread tmp
	li.w	\tmp, THREAD_FPR0
	PTR_ADD \tmp, \tmp, \thread
	fld.d	$f0, \tmp, THREAD_FPR0  - THREAD_FPR0
	fld.d	$f1, \tmp, THREAD_FPR1  - THREAD_FPR0
	fld.d	$f2, \tmp, THREAD_FPR2  - THREAD_FPR0
	fld.d	$f3, \tmp, THREAD_FPR3  - THREAD_FPR0
	fld.d	$f4, \tmp, THREAD_FPR4  - THREAD_FPR0
	fld.d	$f5, \tmp, THREAD_FPR5  - THREAD_FPR0
	fld.d	$f6, \tmp, THREAD_FPR6  - THREAD_FPR0
	fld.d	$f7, \tmp, THREAD_FPR7  - THREAD_FPR0
	fld.d	$f8, \tmp, THREAD_FPR8  - THREAD_FPR0
	fld.d	$f9, \tmp, THREAD_FPR9  - THREAD_FPR0
	fld.d	$f10, \tmp, THREAD_FPR10 - THREAD_FPR0
	fld.d	$f11, \tmp, THREAD_FPR11 - THREAD_FPR0
	fld.d	$f12, \tmp, THREAD_FPR12 - THREAD_FPR0
	fld.d	$f13, \tmp, THREAD_FPR13 - THREAD_FPR0
	fld.d	$f14, \tmp, THREAD_FPR14 - THREAD_FPR0
	fld.d	$f15, \tmp, THREAD_FPR15 - THREAD_FPR0
	fld.d	$f16, \tmp, THREAD_FPR16 - THREAD_FPR0
	fld.d	$f17, \tmp, THREAD_FPR17 - THREAD_FPR0
	fld.d	$f18, \tmp, THREAD_FPR18 - THREAD_FPR0
	fld.d	$f19, \tmp, THREAD_FPR19 - THREAD_FPR0
	fld.d	$f20, \tmp, THREAD_FPR20 - THREAD_FPR0
	fld.d	$f21, \tmp, THREAD_FPR21 - THREAD_FPR0
	fld.d	$f22, \tmp, THREAD_FPR22 - THREAD_FPR0
	fld.d	$f23, \tmp, THREAD_FPR23 - THREAD_FPR0
	fld.d	$f24, \tmp, THREAD_FPR24 - THREAD_FPR0
	fld.d	$f25, \tmp, THREAD_FPR25 - THREAD_FPR0
	fld.d	$f26, \tmp, THREAD_FPR26 - THREAD_FPR0
	fld.d	$f27, \tmp, THREAD_FPR27 - THREAD_FPR0
	fld.d	$f28, \tmp, THREAD_FPR28 - THREAD_FPR0
	fld.d	$f29, \tmp, THREAD_FPR29 - THREAD_FPR0
	fld.d	$f30, \tmp, THREAD_FPR30 - THREAD_FPR0
	fld.d	$f31, \tmp, THREAD_FPR31 - THREAD_FPR0
	.endm

	.macro	lsx_save_data thread tmp
	li.w	\tmp, THREAD_FPR0
	PTR_ADD \tmp, \thread, \tmp
	vst	$vr0, \tmp, THREAD_FPR0  - THREAD_FPR0
	vst	$vr1, \tmp, THREAD_FPR1  - THREAD_FPR0
	vst	$vr2, \tmp, THREAD_FPR2  - THREAD_FPR0
	vst	$vr3, \tmp, THREAD_FPR3  - THREAD_FPR0
	vst	$vr4, \tmp, THREAD_FPR4  - THREAD_FPR0
	vst	$vr5, \tmp, THREAD_FPR5  - THREAD_FPR0
	vst	$vr6, \tmp, THREAD_FPR6  - THREAD_FPR0
	vst	$vr7, \tmp, THREAD_FPR7  - THREAD_FPR0
	vst	$vr8, \tmp, THREAD_FPR8  - THREAD_FPR0
	vst	$vr9, \tmp, THREAD_FPR9  - THREAD_FPR0
	vst	$vr10, \tmp, THREAD_FPR10 - THREAD_FPR0
	vst	$vr11, \tmp, THREAD_FPR11 - THREAD_FPR0
	vst	$vr12, \tmp, THREAD_FPR12 - THREAD_FPR0
	vst	$vr13, \tmp, THREAD_FPR13 - THREAD_FPR0
	vst	$vr14, \tmp, THREAD_FPR14 - THREAD_FPR0
	vst	$vr15, \tmp, THREAD_FPR15 - THREAD_FPR0
	vst	$vr16, \tmp, THREAD_FPR16 - THREAD_FPR0
	vst	$vr17, \tmp, THREAD_FPR17 - THREAD_FPR0
	vst	$vr18, \tmp, THREAD_FPR18 - THREAD_FPR0
	vst	$vr19, \tmp, THREAD_FPR19 - THREAD_FPR0
	vst	$vr20, \tmp, THREAD_FPR20 - THREAD_FPR0
	vst	$vr21, \tmp, THREAD_FPR21 - THREAD_FPR0
	vst	$vr22, \tmp, THREAD_FPR22 - THREAD_FPR0
	vst	$vr23, \tmp, THREAD_FPR23 - THREAD_FPR0
	vst	$vr24, \tmp, THREAD_FPR24 - THREAD_FPR0
	vst	$vr25, \tmp, THREAD_FPR25 - THREAD_FPR0
	vst	$vr26, \tmp, THREAD_FPR26 - THREAD_FPR0
	vst	$vr27, \tmp, THREAD_FPR27 - THREAD_FPR0
	vst	$vr28, \tmp, THREAD_FPR28 - THREAD_FPR0
	vst	$vr29, \tmp, THREAD_FPR29 - THREAD_FPR0
	vst	$vr30, \tmp, THREAD_FPR30 - THREAD_FPR0
	vst	$vr31, \tmp, THREAD_FPR31 - THREAD_FPR0
	.endm

	.macro	lsx_restore_data thread tmp
	li.w	\tmp, THREAD_FPR0
	PTR_ADD	\tmp, \thread, \tmp
	vld	$vr0, \tmp, THREAD_FPR0  - THREAD_FPR0
	vld	$vr1, \tmp, THREAD_FPR1  - THREAD_FPR0
	vld	$vr2, \tmp, THREAD_FPR2  - THREAD_FPR0
	vld	$vr3, \tmp, THREAD_FPR3  - THREAD_FPR0
	vld	$vr4, \tmp, THREAD_FPR4  - THREAD_FPR0
	vld	$vr5, \tmp, THREAD_FPR5  - THREAD_FPR0
	vld	$vr6, \tmp, THREAD_FPR6  - THREAD_FPR0
	vld	$vr7, \tmp, THREAD_FPR7  - THREAD_FPR0
	vld	$vr8, \tmp, THREAD_FPR8  - THREAD_FPR0
	vld	$vr9, \tmp, THREAD_FPR9  - THREAD_FPR0
	vld	$vr10, \tmp, THREAD_FPR10 - THREAD_FPR0
	vld	$vr11, \tmp, THREAD_FPR11 - THREAD_FPR0
	vld	$vr12, \tmp, THREAD_FPR12 - THREAD_FPR0
	vld	$vr13, \tmp, THREAD_FPR13 - THREAD_FPR0
	vld	$vr14, \tmp, THREAD_FPR14 - THREAD_FPR0
	vld	$vr15, \tmp, THREAD_FPR15 - THREAD_FPR0
	vld	$vr16, \tmp, THREAD_FPR16 - THREAD_FPR0
	vld	$vr17, \tmp, THREAD_FPR17 - THREAD_FPR0
	vld	$vr18, \tmp, THREAD_FPR18 - THREAD_FPR0
	vld	$vr19, \tmp, THREAD_FPR19 - THREAD_FPR0
	vld	$vr20, \tmp, THREAD_FPR20 - THREAD_FPR0
	vld	$vr21, \tmp, THREAD_FPR21 - THREAD_FPR0
	vld	$vr22, \tmp, THREAD_FPR22 - THREAD_FPR0
	vld	$vr23, \tmp, THREAD_FPR23 - THREAD_FPR0
	vld	$vr24, \tmp, THREAD_FPR24 - THREAD_FPR0
	vld	$vr25, \tmp, THREAD_FPR25 - THREAD_FPR0
	vld	$vr26, \tmp, THREAD_FPR26 - THREAD_FPR0
	vld	$vr27, \tmp, THREAD_FPR27 - THREAD_FPR0
	vld	$vr28, \tmp, THREAD_FPR28 - THREAD_FPR0
	vld	$vr29, \tmp, THREAD_FPR29 - THREAD_FPR0
	vld	$vr30, \tmp, THREAD_FPR30 - THREAD_FPR0
	vld	$vr31, \tmp, THREAD_FPR31 - THREAD_FPR0
	.endm

	.macro	lsx_save_all	thread tmp0 tmp1
	fpu_save_cc		\thread, \tmp0, \tmp1
	fpu_save_csr		\thread, \tmp0
	lsx_save_data		\thread, \tmp0
	.endm

	.macro	lsx_restore_all	thread tmp0 tmp1
	lsx_restore_data	\thread, \tmp0
	fpu_restore_cc		\thread, \tmp0, \tmp1
	fpu_restore_csr		\thread, \tmp0, \tmp1
	.endm

	.macro	lsx_save_upper vd base tmp off
	vpickve2gr.d	\tmp, \vd, 1
	st.d		\tmp, \base, (\off+8)
	.endm

	.macro	lsx_save_all_upper thread base tmp
	li.w		\tmp, THREAD_FPR0
	PTR_ADD		\base, \thread, \tmp
	lsx_save_upper	$vr0,  \base, \tmp, (THREAD_FPR0-THREAD_FPR0)
	lsx_save_upper	$vr1,  \base, \tmp, (THREAD_FPR1-THREAD_FPR0)
	lsx_save_upper	$vr2,  \base, \tmp, (THREAD_FPR2-THREAD_FPR0)
	lsx_save_upper	$vr3,  \base, \tmp, (THREAD_FPR3-THREAD_FPR0)
	lsx_save_upper	$vr4,  \base, \tmp, (THREAD_FPR4-THREAD_FPR0)
	lsx_save_upper	$vr5,  \base, \tmp, (THREAD_FPR5-THREAD_FPR0)
	lsx_save_upper	$vr6,  \base, \tmp, (THREAD_FPR6-THREAD_FPR0)
	lsx_save_upper	$vr7,  \base, \tmp, (THREAD_FPR7-THREAD_FPR0)
	lsx_save_upper	$vr8,  \base, \tmp, (THREAD_FPR8-THREAD_FPR0)
	lsx_save_upper	$vr9,  \base, \tmp, (THREAD_FPR9-THREAD_FPR0)
	lsx_save_upper	$vr10, \base, \tmp, (THREAD_FPR10-THREAD_FPR0)
	lsx_save_upper	$vr11, \base, \tmp, (THREAD_FPR11-THREAD_FPR0)
	lsx_save_upper	$vr12, \base, \tmp, (THREAD_FPR12-THREAD_FPR0)
	lsx_save_upper	$vr13, \base, \tmp, (THREAD_FPR13-THREAD_FPR0)
	lsx_save_upper	$vr14, \base, \tmp, (THREAD_FPR14-THREAD_FPR0)
	lsx_save_upper	$vr15, \base, \tmp, (THREAD_FPR15-THREAD_FPR0)
	lsx_save_upper	$vr16, \base, \tmp, (THREAD_FPR16-THREAD_FPR0)
	lsx_save_upper	$vr17, \base, \tmp, (THREAD_FPR17-THREAD_FPR0)
	lsx_save_upper	$vr18, \base, \tmp, (THREAD_FPR18-THREAD_FPR0)
	lsx_save_upper	$vr19, \base, \tmp, (THREAD_FPR19-THREAD_FPR0)
	lsx_save_upper	$vr20, \base, \tmp, (THREAD_FPR20-THREAD_FPR0)
	lsx_save_upper	$vr21, \base, \tmp, (THREAD_FPR21-THREAD_FPR0)
	lsx_save_upper	$vr22, \base, \tmp, (THREAD_FPR22-THREAD_FPR0)
	lsx_save_upper	$vr23, \base, \tmp, (THREAD_FPR23-THREAD_FPR0)
	lsx_save_upper	$vr24, \base, \tmp, (THREAD_FPR24-THREAD_FPR0)
	lsx_save_upper	$vr25, \base, \tmp, (THREAD_FPR25-THREAD_FPR0)
	lsx_save_upper	$vr26, \base, \tmp, (THREAD_FPR26-THREAD_FPR0)
	lsx_save_upper	$vr27, \base, \tmp, (THREAD_FPR27-THREAD_FPR0)
	lsx_save_upper	$vr28, \base, \tmp, (THREAD_FPR28-THREAD_FPR0)
	lsx_save_upper	$vr29, \base, \tmp, (THREAD_FPR29-THREAD_FPR0)
	lsx_save_upper	$vr30, \base, \tmp, (THREAD_FPR30-THREAD_FPR0)
	lsx_save_upper	$vr31, \base, \tmp, (THREAD_FPR31-THREAD_FPR0)
	.endm

	.macro	lsx_restore_upper vd base tmp off
	ld.d		\tmp, \base, (\off+8)
	vinsgr2vr.d	\vd,  \tmp, 1
	.endm

	.macro	lsx_restore_all_upper thread base tmp
	li.w		  \tmp, THREAD_FPR0
	PTR_ADD		  \base, \thread, \tmp
	lsx_restore_upper $vr0,  \base, \tmp, (THREAD_FPR0-THREAD_FPR0)
	lsx_restore_upper $vr1,  \base, \tmp, (THREAD_FPR1-THREAD_FPR0)
	lsx_restore_upper $vr2,  \base, \tmp, (THREAD_FPR2-THREAD_FPR0)
	lsx_restore_upper $vr3,  \base, \tmp, (THREAD_FPR3-THREAD_FPR0)
	lsx_restore_upper $vr4,  \base, \tmp, (THREAD_FPR4-THREAD_FPR0)
	lsx_restore_upper $vr5,  \base, \tmp, (THREAD_FPR5-THREAD_FPR0)
	lsx_restore_upper $vr6,  \base, \tmp, (THREAD_FPR6-THREAD_FPR0)
	lsx_restore_upper $vr7,  \base, \tmp, (THREAD_FPR7-THREAD_FPR0)
	lsx_restore_upper $vr8,  \base, \tmp, (THREAD_FPR8-THREAD_FPR0)
	lsx_restore_upper $vr9,  \base, \tmp, (THREAD_FPR9-THREAD_FPR0)
	lsx_restore_upper $vr10, \base, \tmp, (THREAD_FPR10-THREAD_FPR0)
	lsx_restore_upper $vr11, \base, \tmp, (THREAD_FPR11-THREAD_FPR0)
	lsx_restore_upper $vr12, \base, \tmp, (THREAD_FPR12-THREAD_FPR0)
	lsx_restore_upper $vr13, \base, \tmp, (THREAD_FPR13-THREAD_FPR0)
	lsx_restore_upper $vr14, \base, \tmp, (THREAD_FPR14-THREAD_FPR0)
	lsx_restore_upper $vr15, \base, \tmp, (THREAD_FPR15-THREAD_FPR0)
	lsx_restore_upper $vr16, \base, \tmp, (THREAD_FPR16-THREAD_FPR0)
	lsx_restore_upper $vr17, \base, \tmp, (THREAD_FPR17-THREAD_FPR0)
	lsx_restore_upper $vr18, \base, \tmp, (THREAD_FPR18-THREAD_FPR0)
	lsx_restore_upper $vr19, \base, \tmp, (THREAD_FPR19-THREAD_FPR0)
	lsx_restore_upper $vr20, \base, \tmp, (THREAD_FPR20-THREAD_FPR0)
	lsx_restore_upper $vr21, \base, \tmp, (THREAD_FPR21-THREAD_FPR0)
	lsx_restore_upper $vr22, \base, \tmp, (THREAD_FPR22-THREAD_FPR0)
	lsx_restore_upper $vr23, \base, \tmp, (THREAD_FPR23-THREAD_FPR0)
	lsx_restore_upper $vr24, \base, \tmp, (THREAD_FPR24-THREAD_FPR0)
	lsx_restore_upper $vr25, \base, \tmp, (THREAD_FPR25-THREAD_FPR0)
	lsx_restore_upper $vr26, \base, \tmp, (THREAD_FPR26-THREAD_FPR0)
	lsx_restore_upper $vr27, \base, \tmp, (THREAD_FPR27-THREAD_FPR0)
	lsx_restore_upper $vr28, \base, \tmp, (THREAD_FPR28-THREAD_FPR0)
	lsx_restore_upper $vr29, \base, \tmp, (THREAD_FPR29-THREAD_FPR0)
	lsx_restore_upper $vr30, \base, \tmp, (THREAD_FPR30-THREAD_FPR0)
	lsx_restore_upper $vr31, \base, \tmp, (THREAD_FPR31-THREAD_FPR0)
	.endm

	.macro	lsx_init_upper vd tmp
	vinsgr2vr.d	\vd, \tmp, 1
	.endm

	.macro	lsx_init_all_upper tmp
	not		\tmp, zero
	lsx_init_upper	$vr0 \tmp
	lsx_init_upper	$vr1 \tmp
	lsx_init_upper	$vr2 \tmp
	lsx_init_upper	$vr3 \tmp
	lsx_init_upper	$vr4 \tmp
	lsx_init_upper	$vr5 \tmp
	lsx_init_upper	$vr6 \tmp
	lsx_init_upper	$vr7 \tmp
	lsx_init_upper	$vr8 \tmp
	lsx_init_upper	$vr9 \tmp
	lsx_init_upper	$vr10 \tmp
	lsx_init_upper	$vr11 \tmp
	lsx_init_upper	$vr12 \tmp
	lsx_init_upper	$vr13 \tmp
	lsx_init_upper	$vr14 \tmp
	lsx_init_upper	$vr15 \tmp
	lsx_init_upper	$vr16 \tmp
	lsx_init_upper	$vr17 \tmp
	lsx_init_upper	$vr18 \tmp
	lsx_init_upper	$vr19 \tmp
	lsx_init_upper	$vr20 \tmp
	lsx_init_upper	$vr21 \tmp
	lsx_init_upper	$vr22 \tmp
	lsx_init_upper	$vr23 \tmp
	lsx_init_upper	$vr24 \tmp
	lsx_init_upper	$vr25 \tmp
	lsx_init_upper	$vr26 \tmp
	lsx_init_upper	$vr27 \tmp
	lsx_init_upper	$vr28 \tmp
	lsx_init_upper	$vr29 \tmp
	lsx_init_upper	$vr30 \tmp
	lsx_init_upper	$vr31 \tmp
	.endm

	.macro	lasx_save_data thread tmp
	li.w	\tmp, THREAD_FPR0
	PTR_ADD	\tmp, \thread, \tmp
	xvst	$xr0, \tmp, THREAD_FPR0  - THREAD_FPR0
	xvst	$xr1, \tmp, THREAD_FPR1  - THREAD_FPR0
	xvst	$xr2, \tmp, THREAD_FPR2  - THREAD_FPR0
	xvst	$xr3, \tmp, THREAD_FPR3  - THREAD_FPR0
	xvst	$xr4, \tmp, THREAD_FPR4  - THREAD_FPR0
	xvst	$xr5, \tmp, THREAD_FPR5  - THREAD_FPR0
	xvst	$xr6, \tmp, THREAD_FPR6  - THREAD_FPR0
	xvst	$xr7, \tmp, THREAD_FPR7  - THREAD_FPR0
	xvst	$xr8, \tmp, THREAD_FPR8  - THREAD_FPR0
	xvst	$xr9, \tmp, THREAD_FPR9  - THREAD_FPR0
	xvst	$xr10, \tmp, THREAD_FPR10 - THREAD_FPR0
	xvst	$xr11, \tmp, THREAD_FPR11 - THREAD_FPR0
	xvst	$xr12, \tmp, THREAD_FPR12 - THREAD_FPR0
	xvst	$xr13, \tmp, THREAD_FPR13 - THREAD_FPR0
	xvst	$xr14, \tmp, THREAD_FPR14 - THREAD_FPR0
	xvst	$xr15, \tmp, THREAD_FPR15 - THREAD_FPR0
	xvst	$xr16, \tmp, THREAD_FPR16 - THREAD_FPR0
	xvst	$xr17, \tmp, THREAD_FPR17 - THREAD_FPR0
	xvst	$xr18, \tmp, THREAD_FPR18 - THREAD_FPR0
	xvst	$xr19, \tmp, THREAD_FPR19 - THREAD_FPR0
	xvst	$xr20, \tmp, THREAD_FPR20 - THREAD_FPR0
	xvst	$xr21, \tmp, THREAD_FPR21 - THREAD_FPR0
	xvst	$xr22, \tmp, THREAD_FPR22 - THREAD_FPR0
	xvst	$xr23, \tmp, THREAD_FPR23 - THREAD_FPR0
	xvst	$xr24, \tmp, THREAD_FPR24 - THREAD_FPR0
	xvst	$xr25, \tmp, THREAD_FPR25 - THREAD_FPR0
	xvst	$xr26, \tmp, THREAD_FPR26 - THREAD_FPR0
	xvst	$xr27, \tmp, THREAD_FPR27 - THREAD_FPR0
	xvst	$xr28, \tmp, THREAD_FPR28 - THREAD_FPR0
	xvst	$xr29, \tmp, THREAD_FPR29 - THREAD_FPR0
	xvst	$xr30, \tmp, THREAD_FPR30 - THREAD_FPR0
	xvst	$xr31, \tmp, THREAD_FPR31 - THREAD_FPR0
	.endm

	.macro	lasx_restore_data thread tmp
	li.w	\tmp, THREAD_FPR0
	PTR_ADD	\tmp, \thread, \tmp
	xvld	$xr0, \tmp, THREAD_FPR0  - THREAD_FPR0
	xvld	$xr1, \tmp, THREAD_FPR1  - THREAD_FPR0
	xvld	$xr2, \tmp, THREAD_FPR2  - THREAD_FPR0
	xvld	$xr3, \tmp, THREAD_FPR3  - THREAD_FPR0
	xvld	$xr4, \tmp, THREAD_FPR4  - THREAD_FPR0
	xvld	$xr5, \tmp, THREAD_FPR5  - THREAD_FPR0
	xvld	$xr6, \tmp, THREAD_FPR6  - THREAD_FPR0
	xvld	$xr7, \tmp, THREAD_FPR7  - THREAD_FPR0
	xvld	$xr8, \tmp, THREAD_FPR8  - THREAD_FPR0
	xvld	$xr9, \tmp, THREAD_FPR9  - THREAD_FPR0
	xvld	$xr10, \tmp, THREAD_FPR10 - THREAD_FPR0
	xvld	$xr11, \tmp, THREAD_FPR11 - THREAD_FPR0
	xvld	$xr12, \tmp, THREAD_FPR12 - THREAD_FPR0
	xvld	$xr13, \tmp, THREAD_FPR13 - THREAD_FPR0
	xvld	$xr14, \tmp, THREAD_FPR14 - THREAD_FPR0
	xvld	$xr15, \tmp, THREAD_FPR15 - THREAD_FPR0
	xvld	$xr16, \tmp, THREAD_FPR16 - THREAD_FPR0
	xvld	$xr17, \tmp, THREAD_FPR17 - THREAD_FPR0
	xvld	$xr18, \tmp, THREAD_FPR18 - THREAD_FPR0
	xvld	$xr19, \tmp, THREAD_FPR19 - THREAD_FPR0
	xvld	$xr20, \tmp, THREAD_FPR20 - THREAD_FPR0
	xvld	$xr21, \tmp, THREAD_FPR21 - THREAD_FPR0
	xvld	$xr22, \tmp, THREAD_FPR22 - THREAD_FPR0
	xvld	$xr23, \tmp, THREAD_FPR23 - THREAD_FPR0
	xvld	$xr24, \tmp, THREAD_FPR24 - THREAD_FPR0
	xvld	$xr25, \tmp, THREAD_FPR25 - THREAD_FPR0
	xvld	$xr26, \tmp, THREAD_FPR26 - THREAD_FPR0
	xvld	$xr27, \tmp, THREAD_FPR27 - THREAD_FPR0
	xvld	$xr28, \tmp, THREAD_FPR28 - THREAD_FPR0
	xvld	$xr29, \tmp, THREAD_FPR29 - THREAD_FPR0
	xvld	$xr30, \tmp, THREAD_FPR30 - THREAD_FPR0
	xvld	$xr31, \tmp, THREAD_FPR31 - THREAD_FPR0
	.endm

	.macro	lasx_save_all	thread tmp0 tmp1
	fpu_save_cc		\thread, \tmp0, \tmp1
	fpu_save_csr		\thread, \tmp0
	lasx_save_data		\thread, \tmp0
	.endm

	.macro	lasx_restore_all thread tmp0 tmp1
	lasx_restore_data	\thread, \tmp0
	fpu_restore_cc		\thread, \tmp0, \tmp1
	fpu_restore_csr		\thread, \tmp0, \tmp1
	.endm

	.macro	lasx_save_upper xd base tmp off
	/* Nothing */
	.endm

	.macro	lasx_save_all_upper thread base tmp
	/* Nothing */
	.endm

	.macro	lasx_restore_upper xd base tmp0 tmp1 off
	vld		\tmp0, \base, (\off+16)
	xvpermi.q 	\xd,   \tmp1, 0x2
	.endm

	.macro	lasx_restore_all_upper thread base tmp
	li.w		\tmp, THREAD_FPR0
	PTR_ADD		\base, \thread, \tmp
	/* Save $vr31 ($xr31 lower bits) with xvpickve2gr */
	xvpickve2gr.d	$r17, $xr31, 0
	xvpickve2gr.d	$r18, $xr31, 1
	lasx_restore_upper $xr0, \base, $vr31, $xr31, (THREAD_FPR0-THREAD_FPR0)
	lasx_restore_upper $xr1, \base, $vr31, $xr31, (THREAD_FPR1-THREAD_FPR0)
	lasx_restore_upper $xr2, \base, $vr31, $xr31, (THREAD_FPR2-THREAD_FPR0)
	lasx_restore_upper $xr3, \base, $vr31, $xr31, (THREAD_FPR3-THREAD_FPR0)
	lasx_restore_upper $xr4, \base, $vr31, $xr31, (THREAD_FPR4-THREAD_FPR0)
	lasx_restore_upper $xr5, \base, $vr31, $xr31, (THREAD_FPR5-THREAD_FPR0)
	lasx_restore_upper $xr6, \base, $vr31, $xr31, (THREAD_FPR6-THREAD_FPR0)
	lasx_restore_upper $xr7, \base, $vr31, $xr31, (THREAD_FPR7-THREAD_FPR0)
	lasx_restore_upper $xr8, \base, $vr31, $xr31, (THREAD_FPR8-THREAD_FPR0)
	lasx_restore_upper $xr9, \base, $vr31, $xr31, (THREAD_FPR9-THREAD_FPR0)
	lasx_restore_upper $xr10, \base, $vr31, $xr31, (THREAD_FPR10-THREAD_FPR0)
	lasx_restore_upper $xr11, \base, $vr31, $xr31, (THREAD_FPR11-THREAD_FPR0)
	lasx_restore_upper $xr12, \base, $vr31, $xr31, (THREAD_FPR12-THREAD_FPR0)
	lasx_restore_upper $xr13, \base, $vr31, $xr31, (THREAD_FPR13-THREAD_FPR0)
	lasx_restore_upper $xr14, \base, $vr31, $xr31, (THREAD_FPR14-THREAD_FPR0)
	lasx_restore_upper $xr15, \base, $vr31, $xr31, (THREAD_FPR15-THREAD_FPR0)
	lasx_restore_upper $xr16, \base, $vr31, $xr31, (THREAD_FPR16-THREAD_FPR0)
	lasx_restore_upper $xr17, \base, $vr31, $xr31, (THREAD_FPR17-THREAD_FPR0)
	lasx_restore_upper $xr18, \base, $vr31, $xr31, (THREAD_FPR18-THREAD_FPR0)
	lasx_restore_upper $xr19, \base, $vr31, $xr31, (THREAD_FPR19-THREAD_FPR0)
	lasx_restore_upper $xr20, \base, $vr31, $xr31, (THREAD_FPR20-THREAD_FPR0)
	lasx_restore_upper $xr21, \base, $vr31, $xr31, (THREAD_FPR21-THREAD_FPR0)
	lasx_restore_upper $xr22, \base, $vr31, $xr31, (THREAD_FPR22-THREAD_FPR0)
	lasx_restore_upper $xr23, \base, $vr31, $xr31, (THREAD_FPR23-THREAD_FPR0)
	lasx_restore_upper $xr24, \base, $vr31, $xr31, (THREAD_FPR24-THREAD_FPR0)
	lasx_restore_upper $xr25, \base, $vr31, $xr31, (THREAD_FPR25-THREAD_FPR0)
	lasx_restore_upper $xr26, \base, $vr31, $xr31, (THREAD_FPR26-THREAD_FPR0)
	lasx_restore_upper $xr27, \base, $vr31, $xr31, (THREAD_FPR27-THREAD_FPR0)
	lasx_restore_upper $xr28, \base, $vr31, $xr31, (THREAD_FPR28-THREAD_FPR0)
	lasx_restore_upper $xr29, \base, $vr31, $xr31, (THREAD_FPR29-THREAD_FPR0)
	lasx_restore_upper $xr30, \base, $vr31, $xr31, (THREAD_FPR30-THREAD_FPR0)
	lasx_restore_upper $xr31, \base, $vr31, $xr31, (THREAD_FPR31-THREAD_FPR0)
	/* Restore $vr31 ($xr31 lower bits) with xvinsgr2vr */
	xvinsgr2vr.d	$xr31, $r17, 0
	xvinsgr2vr.d	$xr31, $r18, 1
	.endm

	.macro	lasx_init_upper xd tmp
	xvinsgr2vr.d	\xd, \tmp, 2
	xvinsgr2vr.d	\xd, \tmp, 3
	.endm

	.macro	lasx_init_all_upper tmp
	not		\tmp, zero
	lasx_init_upper	$xr0 \tmp
	lasx_init_upper	$xr1 \tmp
	lasx_init_upper	$xr2 \tmp
	lasx_init_upper	$xr3 \tmp
	lasx_init_upper	$xr4 \tmp
	lasx_init_upper	$xr5 \tmp
	lasx_init_upper	$xr6 \tmp
	lasx_init_upper	$xr7 \tmp
	lasx_init_upper	$xr8 \tmp
	lasx_init_upper	$xr9 \tmp
	lasx_init_upper	$xr10 \tmp
	lasx_init_upper	$xr11 \tmp
	lasx_init_upper	$xr12 \tmp
	lasx_init_upper	$xr13 \tmp
	lasx_init_upper	$xr14 \tmp
	lasx_init_upper	$xr15 \tmp
	lasx_init_upper	$xr16 \tmp
	lasx_init_upper	$xr17 \tmp
	lasx_init_upper	$xr18 \tmp
	lasx_init_upper	$xr19 \tmp
	lasx_init_upper	$xr20 \tmp
	lasx_init_upper	$xr21 \tmp
	lasx_init_upper	$xr22 \tmp
	lasx_init_upper	$xr23 \tmp
	lasx_init_upper	$xr24 \tmp
	lasx_init_upper	$xr25 \tmp
	lasx_init_upper	$xr26 \tmp
	lasx_init_upper	$xr27 \tmp
	lasx_init_upper	$xr28 \tmp
	lasx_init_upper	$xr29 \tmp
	lasx_init_upper	$xr30 \tmp
	lasx_init_upper	$xr31 \tmp
	.endm

.macro not dst src
	nor	\dst, \src, zero
.endm

.macro la_abs reg, sym
#ifndef CONFIG_RELOCATABLE
	la.abs	\reg, \sym
#else
	766:
	lu12i.w	\reg, 0
	ori	\reg, \reg, 0
	lu32i.d	\reg, 0
	lu52i.d	\reg, \reg, 0
	.pushsection ".la_abs", "aw", %progbits
	768:
	.dword	768b-766b
	.dword	\sym
	.popsection
#endif
.endm

#endif /* _ASM_ASMMACRO_H */
