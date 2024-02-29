/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Lu Zeng <zenglu@loongson.cn>
 *         Pei Huang <huangpei@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/export.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/asm-extable.h>
#include <asm/asm-offsets.h>
#include <asm/errno.h>
#include <asm/fpregdef.h>
#include <asm/loongarch.h>
#include <asm/regdef.h>

#define FPU_REG_WIDTH		8
#define LSX_REG_WIDTH		16
#define LASX_REG_WIDTH		32

	.macro	EX insn, reg, src, offs
.ex\@:	\insn	\reg, \src, \offs
	_asm_extable .ex\@, .L_fpu_fault
	.endm

	.macro sc_save_fp base
	EX	fst.d	$f0,  \base, (0 * FPU_REG_WIDTH)
	EX	fst.d	$f1,  \base, (1 * FPU_REG_WIDTH)
	EX	fst.d	$f2,  \base, (2 * FPU_REG_WIDTH)
	EX	fst.d	$f3,  \base, (3 * FPU_REG_WIDTH)
	EX	fst.d	$f4,  \base, (4 * FPU_REG_WIDTH)
	EX	fst.d	$f5,  \base, (5 * FPU_REG_WIDTH)
	EX	fst.d	$f6,  \base, (6 * FPU_REG_WIDTH)
	EX	fst.d	$f7,  \base, (7 * FPU_REG_WIDTH)
	EX	fst.d	$f8,  \base, (8 * FPU_REG_WIDTH)
	EX	fst.d	$f9,  \base, (9 * FPU_REG_WIDTH)
	EX	fst.d	$f10, \base, (10 * FPU_REG_WIDTH)
	EX	fst.d	$f11, \base, (11 * FPU_REG_WIDTH)
	EX	fst.d	$f12, \base, (12 * FPU_REG_WIDTH)
	EX	fst.d	$f13, \base, (13 * FPU_REG_WIDTH)
	EX	fst.d	$f14, \base, (14 * FPU_REG_WIDTH)
	EX	fst.d	$f15, \base, (15 * FPU_REG_WIDTH)
	EX	fst.d	$f16, \base, (16 * FPU_REG_WIDTH)
	EX	fst.d	$f17, \base, (17 * FPU_REG_WIDTH)
	EX	fst.d	$f18, \base, (18 * FPU_REG_WIDTH)
	EX	fst.d	$f19, \base, (19 * FPU_REG_WIDTH)
	EX	fst.d	$f20, \base, (20 * FPU_REG_WIDTH)
	EX	fst.d	$f21, \base, (21 * FPU_REG_WIDTH)
	EX	fst.d	$f22, \base, (22 * FPU_REG_WIDTH)
	EX	fst.d	$f23, \base, (23 * FPU_REG_WIDTH)
	EX	fst.d	$f24, \base, (24 * FPU_REG_WIDTH)
	EX	fst.d	$f25, \base, (25 * FPU_REG_WIDTH)
	EX	fst.d	$f26, \base, (26 * FPU_REG_WIDTH)
	EX	fst.d	$f27, \base, (27 * FPU_REG_WIDTH)
	EX	fst.d	$f28, \base, (28 * FPU_REG_WIDTH)
	EX	fst.d	$f29, \base, (29 * FPU_REG_WIDTH)
	EX	fst.d	$f30, \base, (30 * FPU_REG_WIDTH)
	EX	fst.d	$f31, \base, (31 * FPU_REG_WIDTH)
	.endm

	.macro sc_restore_fp base
	EX	fld.d	$f0,  \base, (0 * FPU_REG_WIDTH)
	EX	fld.d	$f1,  \base, (1 * FPU_REG_WIDTH)
	EX	fld.d	$f2,  \base, (2 * FPU_REG_WIDTH)
	EX	fld.d	$f3,  \base, (3 * FPU_REG_WIDTH)
	EX	fld.d	$f4,  \base, (4 * FPU_REG_WIDTH)
	EX	fld.d	$f5,  \base, (5 * FPU_REG_WIDTH)
	EX	fld.d	$f6,  \base, (6 * FPU_REG_WIDTH)
	EX	fld.d	$f7,  \base, (7 * FPU_REG_WIDTH)
	EX	fld.d	$f8,  \base, (8 * FPU_REG_WIDTH)
	EX	fld.d	$f9,  \base, (9 * FPU_REG_WIDTH)
	EX	fld.d	$f10, \base, (10 * FPU_REG_WIDTH)
	EX	fld.d	$f11, \base, (11 * FPU_REG_WIDTH)
	EX	fld.d	$f12, \base, (12 * FPU_REG_WIDTH)
	EX	fld.d	$f13, \base, (13 * FPU_REG_WIDTH)
	EX	fld.d	$f14, \base, (14 * FPU_REG_WIDTH)
	EX	fld.d	$f15, \base, (15 * FPU_REG_WIDTH)
	EX	fld.d	$f16, \base, (16 * FPU_REG_WIDTH)
	EX	fld.d	$f17, \base, (17 * FPU_REG_WIDTH)
	EX	fld.d	$f18, \base, (18 * FPU_REG_WIDTH)
	EX	fld.d	$f19, \base, (19 * FPU_REG_WIDTH)
	EX	fld.d	$f20, \base, (20 * FPU_REG_WIDTH)
	EX	fld.d	$f21, \base, (21 * FPU_REG_WIDTH)
	EX	fld.d	$f22, \base, (22 * FPU_REG_WIDTH)
	EX	fld.d	$f23, \base, (23 * FPU_REG_WIDTH)
	EX	fld.d	$f24, \base, (24 * FPU_REG_WIDTH)
	EX	fld.d	$f25, \base, (25 * FPU_REG_WIDTH)
	EX	fld.d	$f26, \base, (26 * FPU_REG_WIDTH)
	EX	fld.d	$f27, \base, (27 * FPU_REG_WIDTH)
	EX	fld.d	$f28, \base, (28 * FPU_REG_WIDTH)
	EX	fld.d	$f29, \base, (29 * FPU_REG_WIDTH)
	EX	fld.d	$f30, \base, (30 * FPU_REG_WIDTH)
	EX	fld.d	$f31, \base, (31 * FPU_REG_WIDTH)
	.endm

	.macro sc_save_fcc base, tmp0, tmp1
	movcf2gr	\tmp0, $fcc0
	move		\tmp1, \tmp0
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
	EX	st.d	\tmp1, \base, 0
	.endm

	.macro sc_restore_fcc base, tmp0, tmp1
	EX	ld.d	\tmp0, \base, 0
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
	EX	st.w	\tmp0, \base, 0
#if defined(CONFIG_CPU_HAS_LBT)
	/* TM bit is always 0 if LBT not supported */
	andi		\tmp0, \tmp0, FPU_CSR_TM
	beqz		\tmp0, 1f
	x86clrtm
1:
#endif
	.endm

	.macro sc_restore_fcsr base, tmp0
	EX	ld.w	\tmp0, \base, 0
	movgr2fcsr	fcsr0, \tmp0
	.endm

	.macro sc_save_lsx base
#ifdef CONFIG_CPU_HAS_LSX
	EX	vst	$vr0,  \base, (0 * LSX_REG_WIDTH)
	EX	vst	$vr1,  \base, (1 * LSX_REG_WIDTH)
	EX	vst	$vr2,  \base, (2 * LSX_REG_WIDTH)
	EX	vst	$vr3,  \base, (3 * LSX_REG_WIDTH)
	EX	vst	$vr4,  \base, (4 * LSX_REG_WIDTH)
	EX	vst	$vr5,  \base, (5 * LSX_REG_WIDTH)
	EX	vst	$vr6,  \base, (6 * LSX_REG_WIDTH)
	EX	vst	$vr7,  \base, (7 * LSX_REG_WIDTH)
	EX	vst	$vr8,  \base, (8 * LSX_REG_WIDTH)
	EX	vst	$vr9,  \base, (9 * LSX_REG_WIDTH)
	EX	vst	$vr10, \base, (10 * LSX_REG_WIDTH)
	EX	vst	$vr11, \base, (11 * LSX_REG_WIDTH)
	EX	vst	$vr12, \base, (12 * LSX_REG_WIDTH)
	EX	vst	$vr13, \base, (13 * LSX_REG_WIDTH)
	EX	vst	$vr14, \base, (14 * LSX_REG_WIDTH)
	EX	vst	$vr15, \base, (15 * LSX_REG_WIDTH)
	EX	vst	$vr16, \base, (16 * LSX_REG_WIDTH)
	EX	vst	$vr17, \base, (17 * LSX_REG_WIDTH)
	EX	vst	$vr18, \base, (18 * LSX_REG_WIDTH)
	EX	vst	$vr19, \base, (19 * LSX_REG_WIDTH)
	EX	vst	$vr20, \base, (20 * LSX_REG_WIDTH)
	EX	vst	$vr21, \base, (21 * LSX_REG_WIDTH)
	EX	vst	$vr22, \base, (22 * LSX_REG_WIDTH)
	EX	vst	$vr23, \base, (23 * LSX_REG_WIDTH)
	EX	vst	$vr24, \base, (24 * LSX_REG_WIDTH)
	EX	vst	$vr25, \base, (25 * LSX_REG_WIDTH)
	EX	vst	$vr26, \base, (26 * LSX_REG_WIDTH)
	EX	vst	$vr27, \base, (27 * LSX_REG_WIDTH)
	EX	vst	$vr28, \base, (28 * LSX_REG_WIDTH)
	EX	vst	$vr29, \base, (29 * LSX_REG_WIDTH)
	EX	vst	$vr30, \base, (30 * LSX_REG_WIDTH)
	EX	vst	$vr31, \base, (31 * LSX_REG_WIDTH)
#endif
	.endm

	.macro sc_restore_lsx base
#ifdef CONFIG_CPU_HAS_LSX
	EX	vld	$vr0,  \base, (0 * LSX_REG_WIDTH)
	EX	vld	$vr1,  \base, (1 * LSX_REG_WIDTH)
	EX	vld	$vr2,  \base, (2 * LSX_REG_WIDTH)
	EX	vld	$vr3,  \base, (3 * LSX_REG_WIDTH)
	EX	vld	$vr4,  \base, (4 * LSX_REG_WIDTH)
	EX	vld	$vr5,  \base, (5 * LSX_REG_WIDTH)
	EX	vld	$vr6,  \base, (6 * LSX_REG_WIDTH)
	EX	vld	$vr7,  \base, (7 * LSX_REG_WIDTH)
	EX	vld	$vr8,  \base, (8 * LSX_REG_WIDTH)
	EX	vld	$vr9,  \base, (9 * LSX_REG_WIDTH)
	EX	vld	$vr10, \base, (10 * LSX_REG_WIDTH)
	EX	vld	$vr11, \base, (11 * LSX_REG_WIDTH)
	EX	vld	$vr12, \base, (12 * LSX_REG_WIDTH)
	EX	vld	$vr13, \base, (13 * LSX_REG_WIDTH)
	EX	vld	$vr14, \base, (14 * LSX_REG_WIDTH)
	EX	vld	$vr15, \base, (15 * LSX_REG_WIDTH)
	EX	vld	$vr16, \base, (16 * LSX_REG_WIDTH)
	EX	vld	$vr17, \base, (17 * LSX_REG_WIDTH)
	EX	vld	$vr18, \base, (18 * LSX_REG_WIDTH)
	EX	vld	$vr19, \base, (19 * LSX_REG_WIDTH)
	EX	vld	$vr20, \base, (20 * LSX_REG_WIDTH)
	EX	vld	$vr21, \base, (21 * LSX_REG_WIDTH)
	EX	vld	$vr22, \base, (22 * LSX_REG_WIDTH)
	EX	vld	$vr23, \base, (23 * LSX_REG_WIDTH)
	EX	vld	$vr24, \base, (24 * LSX_REG_WIDTH)
	EX	vld	$vr25, \base, (25 * LSX_REG_WIDTH)
	EX	vld	$vr26, \base, (26 * LSX_REG_WIDTH)
	EX	vld	$vr27, \base, (27 * LSX_REG_WIDTH)
	EX	vld	$vr28, \base, (28 * LSX_REG_WIDTH)
	EX	vld	$vr29, \base, (29 * LSX_REG_WIDTH)
	EX	vld	$vr30, \base, (30 * LSX_REG_WIDTH)
	EX	vld	$vr31, \base, (31 * LSX_REG_WIDTH)
#endif
	.endm

	.macro sc_save_lasx base
#ifdef CONFIG_CPU_HAS_LASX
	EX	xvst	$xr0,  \base, (0 * LASX_REG_WIDTH)
	EX	xvst	$xr1,  \base, (1 * LASX_REG_WIDTH)
	EX	xvst	$xr2,  \base, (2 * LASX_REG_WIDTH)
	EX	xvst	$xr3,  \base, (3 * LASX_REG_WIDTH)
	EX	xvst	$xr4,  \base, (4 * LASX_REG_WIDTH)
	EX	xvst	$xr5,  \base, (5 * LASX_REG_WIDTH)
	EX	xvst	$xr6,  \base, (6 * LASX_REG_WIDTH)
	EX	xvst	$xr7,  \base, (7 * LASX_REG_WIDTH)
	EX	xvst	$xr8,  \base, (8 * LASX_REG_WIDTH)
	EX	xvst	$xr9,  \base, (9 * LASX_REG_WIDTH)
	EX	xvst	$xr10, \base, (10 * LASX_REG_WIDTH)
	EX	xvst	$xr11, \base, (11 * LASX_REG_WIDTH)
	EX	xvst	$xr12, \base, (12 * LASX_REG_WIDTH)
	EX	xvst	$xr13, \base, (13 * LASX_REG_WIDTH)
	EX	xvst	$xr14, \base, (14 * LASX_REG_WIDTH)
	EX	xvst	$xr15, \base, (15 * LASX_REG_WIDTH)
	EX	xvst	$xr16, \base, (16 * LASX_REG_WIDTH)
	EX	xvst	$xr17, \base, (17 * LASX_REG_WIDTH)
	EX	xvst	$xr18, \base, (18 * LASX_REG_WIDTH)
	EX	xvst	$xr19, \base, (19 * LASX_REG_WIDTH)
	EX	xvst	$xr20, \base, (20 * LASX_REG_WIDTH)
	EX	xvst	$xr21, \base, (21 * LASX_REG_WIDTH)
	EX	xvst	$xr22, \base, (22 * LASX_REG_WIDTH)
	EX	xvst	$xr23, \base, (23 * LASX_REG_WIDTH)
	EX	xvst	$xr24, \base, (24 * LASX_REG_WIDTH)
	EX	xvst	$xr25, \base, (25 * LASX_REG_WIDTH)
	EX	xvst	$xr26, \base, (26 * LASX_REG_WIDTH)
	EX	xvst	$xr27, \base, (27 * LASX_REG_WIDTH)
	EX	xvst	$xr28, \base, (28 * LASX_REG_WIDTH)
	EX	xvst	$xr29, \base, (29 * LASX_REG_WIDTH)
	EX	xvst	$xr30, \base, (30 * LASX_REG_WIDTH)
	EX	xvst	$xr31, \base, (31 * LASX_REG_WIDTH)
#endif
	.endm

	.macro sc_restore_lasx base
#ifdef CONFIG_CPU_HAS_LASX
	EX	xvld	$xr0,  \base, (0 * LASX_REG_WIDTH)
	EX	xvld	$xr1,  \base, (1 * LASX_REG_WIDTH)
	EX	xvld	$xr2,  \base, (2 * LASX_REG_WIDTH)
	EX	xvld	$xr3,  \base, (3 * LASX_REG_WIDTH)
	EX	xvld	$xr4,  \base, (4 * LASX_REG_WIDTH)
	EX	xvld	$xr5,  \base, (5 * LASX_REG_WIDTH)
	EX	xvld	$xr6,  \base, (6 * LASX_REG_WIDTH)
	EX	xvld	$xr7,  \base, (7 * LASX_REG_WIDTH)
	EX	xvld	$xr8,  \base, (8 * LASX_REG_WIDTH)
	EX	xvld	$xr9,  \base, (9 * LASX_REG_WIDTH)
	EX	xvld	$xr10, \base, (10 * LASX_REG_WIDTH)
	EX	xvld	$xr11, \base, (11 * LASX_REG_WIDTH)
	EX	xvld	$xr12, \base, (12 * LASX_REG_WIDTH)
	EX	xvld	$xr13, \base, (13 * LASX_REG_WIDTH)
	EX	xvld	$xr14, \base, (14 * LASX_REG_WIDTH)
	EX	xvld	$xr15, \base, (15 * LASX_REG_WIDTH)
	EX	xvld	$xr16, \base, (16 * LASX_REG_WIDTH)
	EX	xvld	$xr17, \base, (17 * LASX_REG_WIDTH)
	EX	xvld	$xr18, \base, (18 * LASX_REG_WIDTH)
	EX	xvld	$xr19, \base, (19 * LASX_REG_WIDTH)
	EX	xvld	$xr20, \base, (20 * LASX_REG_WIDTH)
	EX	xvld	$xr21, \base, (21 * LASX_REG_WIDTH)
	EX	xvld	$xr22, \base, (22 * LASX_REG_WIDTH)
	EX	xvld	$xr23, \base, (23 * LASX_REG_WIDTH)
	EX	xvld	$xr24, \base, (24 * LASX_REG_WIDTH)
	EX	xvld	$xr25, \base, (25 * LASX_REG_WIDTH)
	EX	xvld	$xr26, \base, (26 * LASX_REG_WIDTH)
	EX	xvld	$xr27, \base, (27 * LASX_REG_WIDTH)
	EX	xvld	$xr28, \base, (28 * LASX_REG_WIDTH)
	EX	xvld	$xr29, \base, (29 * LASX_REG_WIDTH)
	EX	xvld	$xr30, \base, (30 * LASX_REG_WIDTH)
	EX	xvld	$xr31, \base, (31 * LASX_REG_WIDTH)
#endif
	.endm

/*
 * Save a thread's fp context.
 */
SYM_FUNC_START(_save_fp)
	fpu_save_csr	a0 t1
	fpu_save_double	a0 t1			# clobbers t1
	fpu_save_cc	a0 t1 t2		# clobbers t1, t2
	jr		ra
SYM_FUNC_END(_save_fp)
EXPORT_SYMBOL(_save_fp)

/*
 * Restore a thread's fp context.
 */
SYM_FUNC_START(_restore_fp)
	fpu_restore_double	a0 t1		# clobbers t1
	fpu_restore_csr		a0 t1 t2
	fpu_restore_cc		a0 t1 t2	# clobbers t1, t2
	jr			ra
SYM_FUNC_END(_restore_fp)

#ifdef CONFIG_CPU_HAS_LSX

/*
 * Save a thread's LSX vector context.
 */
SYM_FUNC_START(_save_lsx)
	lsx_save_all	a0 t1 t2
	jr	ra
SYM_FUNC_END(_save_lsx)
EXPORT_SYMBOL(_save_lsx)

/*
 * Restore a thread's LSX vector context.
 */
SYM_FUNC_START(_restore_lsx)
	lsx_restore_all	a0 t1 t2
	jr	ra
SYM_FUNC_END(_restore_lsx)

SYM_FUNC_START(_save_lsx_upper)
	lsx_save_all_upper a0 t0 t1
	jr	ra
SYM_FUNC_END(_save_lsx_upper)

SYM_FUNC_START(_restore_lsx_upper)
	lsx_restore_all_upper a0 t0 t1
	jr	ra
SYM_FUNC_END(_restore_lsx_upper)
EXPORT_SYMBOL(_restore_lsx_upper)

SYM_FUNC_START(_init_lsx_upper)
	lsx_init_all_upper t1
	jr	ra
SYM_FUNC_END(_init_lsx_upper)
#endif

#ifdef CONFIG_CPU_HAS_LASX

/*
 * Save a thread's LASX vector context.
 */
SYM_FUNC_START(_save_lasx)
	lasx_save_all	a0 t1 t2
	jr	ra
SYM_FUNC_END(_save_lasx)
EXPORT_SYMBOL(_save_lasx)

/*
 * Restore a thread's LASX vector context.
 */
SYM_FUNC_START(_restore_lasx)
	lasx_restore_all a0 t1 t2
	jr	ra
SYM_FUNC_END(_restore_lasx)

SYM_FUNC_START(_save_lasx_upper)
	lasx_save_all_upper a0 t0 t1
	jr	ra
SYM_FUNC_END(_save_lasx_upper)

SYM_FUNC_START(_restore_lasx_upper)
	lasx_restore_all_upper a0 t0 t1
	jr	ra
SYM_FUNC_END(_restore_lasx_upper)
EXPORT_SYMBOL(_restore_lasx_upper)

SYM_FUNC_START(_init_lasx_upper)
	lasx_init_all_upper t1
	jr	ra
SYM_FUNC_END(_init_lasx_upper)
#endif

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

	jr	ra
SYM_FUNC_END(_init_fpu)

/*
 * a0: fpregs
 * a1: fcc
 * a2: fcsr
 */
SYM_FUNC_START(_save_fp_context)
	sc_save_fcc	a1 t1 t2
	sc_save_fcsr	a2 t1
	sc_save_fp	a0
	li.w		a0, 0				# success
	jr		ra
SYM_FUNC_END(_save_fp_context)

/*
 * a0: fpregs
 * a1: fcc
 * a2: fcsr
 */
SYM_FUNC_START(_restore_fp_context)
	sc_restore_fp	a0
	sc_restore_fcc	a1 t1 t2
	sc_restore_fcsr	a2 t1
	li.w		a0, 0				# success
	jr		ra
SYM_FUNC_END(_restore_fp_context)

/*
 * a0: fpregs
 * a1: fcc
 * a2: fcsr
 */
SYM_FUNC_START(_save_lsx_context)
	sc_save_fcc a1, t0, t1
	sc_save_fcsr a2, t0
	sc_save_lsx a0
	li.w	a0, 0					# success
	jr	ra
SYM_FUNC_END(_save_lsx_context)

/*
 * a0: fpregs
 * a1: fcc
 * a2: fcsr
 */
SYM_FUNC_START(_restore_lsx_context)
	sc_restore_lsx a0
	sc_restore_fcc a1, t1, t2
	sc_restore_fcsr a2, t1
	li.w	a0, 0					# success
	jr	ra
SYM_FUNC_END(_restore_lsx_context)

/*
 * a0: fpregs
 * a1: fcc
 * a2: fcsr
 */
SYM_FUNC_START(_save_lasx_context)
	sc_save_fcc a1, t0, t1
	sc_save_fcsr a2, t0
	sc_save_lasx a0
	li.w	a0, 0					# success
	jr	ra
SYM_FUNC_END(_save_lasx_context)

/*
 * a0: fpregs
 * a1: fcc
 * a2: fcsr
 */
SYM_FUNC_START(_restore_lasx_context)
	sc_restore_lasx a0
	sc_restore_fcc a1, t1, t2
	sc_restore_fcsr a2, t1
	li.w	a0, 0					# success
	jr	ra
SYM_FUNC_END(_restore_lasx_context)

.L_fpu_fault:
	li.w	a0, -EFAULT				# failure
	jr	ra
