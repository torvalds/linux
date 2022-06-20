/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Lu Zeng <zenglu@loongson.cn>
 *         Pei Huang <huangpei@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/asm-offsets.h>
#include <asm/errno.h>
#include <asm/export.h>
#include <asm/fpregdef.h>
#include <asm/loongarch.h>
#include <asm/regdef.h>

#define FPU_REG_WIDTH		8
#define LSX_REG_WIDTH		16
#define LASX_REG_WIDTH		32

	.macro	EX insn, reg, src, offs
.ex\@:	\insn	\reg, \src, \offs
	.section __ex_table,"a"
	PTR	.ex\@, fault
	.previous
	.endm

	.macro sc_save_fp base
	EX	fst.d $f0,  \base, (0 * FPU_REG_WIDTH)
	EX	fst.d $f1,  \base, (1 * FPU_REG_WIDTH)
	EX	fst.d $f2,  \base, (2 * FPU_REG_WIDTH)
	EX	fst.d $f3,  \base, (3 * FPU_REG_WIDTH)
	EX	fst.d $f4,  \base, (4 * FPU_REG_WIDTH)
	EX	fst.d $f5,  \base, (5 * FPU_REG_WIDTH)
	EX	fst.d $f6,  \base, (6 * FPU_REG_WIDTH)
	EX	fst.d $f7,  \base, (7 * FPU_REG_WIDTH)
	EX	fst.d $f8,  \base, (8 * FPU_REG_WIDTH)
	EX	fst.d $f9,  \base, (9 * FPU_REG_WIDTH)
	EX	fst.d $f10, \base, (10 * FPU_REG_WIDTH)
	EX	fst.d $f11, \base, (11 * FPU_REG_WIDTH)
	EX	fst.d $f12, \base, (12 * FPU_REG_WIDTH)
	EX	fst.d $f13, \base, (13 * FPU_REG_WIDTH)
	EX	fst.d $f14, \base, (14 * FPU_REG_WIDTH)
	EX	fst.d $f15, \base, (15 * FPU_REG_WIDTH)
	EX	fst.d $f16, \base, (16 * FPU_REG_WIDTH)
	EX	fst.d $f17, \base, (17 * FPU_REG_WIDTH)
	EX	fst.d $f18, \base, (18 * FPU_REG_WIDTH)
	EX	fst.d $f19, \base, (19 * FPU_REG_WIDTH)
	EX	fst.d $f20, \base, (20 * FPU_REG_WIDTH)
	EX	fst.d $f21, \base, (21 * FPU_REG_WIDTH)
	EX	fst.d $f22, \base, (22 * FPU_REG_WIDTH)
	EX	fst.d $f23, \base, (23 * FPU_REG_WIDTH)
	EX	fst.d $f24, \base, (24 * FPU_REG_WIDTH)
	EX	fst.d $f25, \base, (25 * FPU_REG_WIDTH)
	EX	fst.d $f26, \base, (26 * FPU_REG_WIDTH)
	EX	fst.d $f27, \base, (27 * FPU_REG_WIDTH)
	EX	fst.d $f28, \base, (28 * FPU_REG_WIDTH)
	EX	fst.d $f29, \base, (29 * FPU_REG_WIDTH)
	EX	fst.d $f30, \base, (30 * FPU_REG_WIDTH)
	EX	fst.d $f31, \base, (31 * FPU_REG_WIDTH)
	.endm

	.macro sc_restore_fp base
	EX	fld.d $f0,  \base, (0 * FPU_REG_WIDTH)
	EX	fld.d $f1,  \base, (1 * FPU_REG_WIDTH)
	EX	fld.d $f2,  \base, (2 * FPU_REG_WIDTH)
	EX	fld.d $f3,  \base, (3 * FPU_REG_WIDTH)
	EX	fld.d $f4,  \base, (4 * FPU_REG_WIDTH)
	EX	fld.d $f5,  \base, (5 * FPU_REG_WIDTH)
	EX	fld.d $f6,  \base, (6 * FPU_REG_WIDTH)
	EX	fld.d $f7,  \base, (7 * FPU_REG_WIDTH)
	EX	fld.d $f8,  \base, (8 * FPU_REG_WIDTH)
	EX	fld.d $f9,  \base, (9 * FPU_REG_WIDTH)
	EX	fld.d $f10, \base, (10 * FPU_REG_WIDTH)
	EX	fld.d $f11, \base, (11 * FPU_REG_WIDTH)
	EX	fld.d $f12, \base, (12 * FPU_REG_WIDTH)
	EX	fld.d $f13, \base, (13 * FPU_REG_WIDTH)
	EX	fld.d $f14, \base, (14 * FPU_REG_WIDTH)
	EX	fld.d $f15, \base, (15 * FPU_REG_WIDTH)
	EX	fld.d $f16, \base, (16 * FPU_REG_WIDTH)
	EX	fld.d $f17, \base, (17 * FPU_REG_WIDTH)
	EX	fld.d $f18, \base, (18 * FPU_REG_WIDTH)
	EX	fld.d $f19, \base, (19 * FPU_REG_WIDTH)
	EX	fld.d $f20, \base, (20 * FPU_REG_WIDTH)
	EX	fld.d $f21, \base, (21 * FPU_REG_WIDTH)
	EX	fld.d $f22, \base, (22 * FPU_REG_WIDTH)
	EX	fld.d $f23, \base, (23 * FPU_REG_WIDTH)
	EX	fld.d $f24, \base, (24 * FPU_REG_WIDTH)
	EX	fld.d $f25, \base, (25 * FPU_REG_WIDTH)
	EX	fld.d $f26, \base, (26 * FPU_REG_WIDTH)
	EX	fld.d $f27, \base, (27 * FPU_REG_WIDTH)
	EX	fld.d $f28, \base, (28 * FPU_REG_WIDTH)
	EX	fld.d $f29, \base, (29 * FPU_REG_WIDTH)
	EX	fld.d $f30, \base, (30 * FPU_REG_WIDTH)
	EX	fld.d $f31, \base, (31 * FPU_REG_WIDTH)
	.endm

	.macro sc_save_fcc base, tmp0, tmp1
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
	EX	st.d \tmp1, \base, 0
	.endm

	.macro sc_restore_fcc base, tmp0, tmp1
	EX	ld.d \tmp0, \base, 0
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

	.macro sc_save_fcsr base, tmp0
	movfcsr2gr	\tmp0, fcsr0
	EX	st.w \tmp0, \base, 0
	.endm

	.macro sc_restore_fcsr base, tmp0
	EX	ld.w \tmp0, \base, 0
	movgr2fcsr	fcsr0, \tmp0
	.endm

	.macro sc_save_vcsr base, tmp0
	movfcsr2gr	\tmp0, vcsr16
	EX	st.w \tmp0, \base, 0
	.endm

	.macro sc_restore_vcsr base, tmp0
	EX	ld.w \tmp0, \base, 0
	movgr2fcsr	vcsr16, \tmp0
	.endm

/*
 * Save a thread's fp context.
 */
SYM_FUNC_START(_save_fp)
	fpu_save_csr	a0 t1
	fpu_save_double a0 t1			# clobbers t1
	fpu_save_cc	a0 t1 t2		# clobbers t1, t2
	jirl zero, ra, 0
SYM_FUNC_END(_save_fp)
EXPORT_SYMBOL(_save_fp)

/*
 * Restore a thread's fp context.
 */
SYM_FUNC_START(_restore_fp)
	fpu_restore_double a0 t1		# clobbers t1
	fpu_restore_csr	a0 t1
	fpu_restore_cc	a0 t1 t2		# clobbers t1, t2
	jirl zero, ra, 0
SYM_FUNC_END(_restore_fp)

/*
 * Load the FPU with signalling NANS.  This bit pattern we're using has
 * the property that no matter whether considered as single or as double
 * precision represents signaling NANS.
 *
 * The value to initialize fcsr0 to comes in $a0.
 */

SYM_FUNC_START(_init_fpu)
	li.w	t1, CSR_EUEN_FPEN
	csrxchg	t1, t1, LOONGARCH_CSR_EUEN

	movgr2fcsr	fcsr0, a0

	li.w	t1, -1				# SNaN

	movgr2fr.d	$f0, t1
	movgr2fr.d	$f1, t1
	movgr2fr.d	$f2, t1
	movgr2fr.d	$f3, t1
	movgr2fr.d	$f4, t1
	movgr2fr.d	$f5, t1
	movgr2fr.d	$f6, t1
	movgr2fr.d	$f7, t1
	movgr2fr.d	$f8, t1
	movgr2fr.d	$f9, t1
	movgr2fr.d	$f10, t1
	movgr2fr.d	$f11, t1
	movgr2fr.d	$f12, t1
	movgr2fr.d	$f13, t1
	movgr2fr.d	$f14, t1
	movgr2fr.d	$f15, t1
	movgr2fr.d	$f16, t1
	movgr2fr.d	$f17, t1
	movgr2fr.d	$f18, t1
	movgr2fr.d	$f19, t1
	movgr2fr.d	$f20, t1
	movgr2fr.d	$f21, t1
	movgr2fr.d	$f22, t1
	movgr2fr.d	$f23, t1
	movgr2fr.d	$f24, t1
	movgr2fr.d	$f25, t1
	movgr2fr.d	$f26, t1
	movgr2fr.d	$f27, t1
	movgr2fr.d	$f28, t1
	movgr2fr.d	$f29, t1
	movgr2fr.d	$f30, t1
	movgr2fr.d	$f31, t1

	jirl zero, ra, 0
SYM_FUNC_END(_init_fpu)

/*
 * a0: fpregs
 * a1: fcc
 * a2: fcsr
 */
SYM_FUNC_START(_save_fp_context)
	sc_save_fcc a1 t1 t2
	sc_save_fcsr a2 t1
	sc_save_fp a0
	li.w	a0, 0					# success
	jirl zero, ra, 0
SYM_FUNC_END(_save_fp_context)

/*
 * a0: fpregs
 * a1: fcc
 * a2: fcsr
 */
SYM_FUNC_START(_restore_fp_context)
	sc_restore_fp a0
	sc_restore_fcc a1 t1 t2
	sc_restore_fcsr a2 t1
	li.w	a0, 0					# success
	jirl zero, ra, 0
SYM_FUNC_END(_restore_fp_context)

SYM_FUNC_START(fault)
	li.w	a0, -EFAULT				# failure
	jirl zero, ra, 0
SYM_FUNC_END(fault)
