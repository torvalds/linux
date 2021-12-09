/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * FP/SIMD state saving and restoring macros
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 */

#include <asm/assembler.h>

.macro fpsimd_save state, tmpnr
	stp	q0, q1, [\state, #16 * 0]
	stp	q2, q3, [\state, #16 * 2]
	stp	q4, q5, [\state, #16 * 4]
	stp	q6, q7, [\state, #16 * 6]
	stp	q8, q9, [\state, #16 * 8]
	stp	q10, q11, [\state, #16 * 10]
	stp	q12, q13, [\state, #16 * 12]
	stp	q14, q15, [\state, #16 * 14]
	stp	q16, q17, [\state, #16 * 16]
	stp	q18, q19, [\state, #16 * 18]
	stp	q20, q21, [\state, #16 * 20]
	stp	q22, q23, [\state, #16 * 22]
	stp	q24, q25, [\state, #16 * 24]
	stp	q26, q27, [\state, #16 * 26]
	stp	q28, q29, [\state, #16 * 28]
	stp	q30, q31, [\state, #16 * 30]!
	mrs	x\tmpnr, fpsr
	str	w\tmpnr, [\state, #16 * 2]
	mrs	x\tmpnr, fpcr
	str	w\tmpnr, [\state, #16 * 2 + 4]
.endm

.macro fpsimd_restore_fpcr state, tmp
	/*
	 * Writes to fpcr may be self-synchronising, so avoid restoring
	 * the register if it hasn't changed.
	 */
	mrs	\tmp, fpcr
	cmp	\tmp, \state
	b.eq	9999f
	msr	fpcr, \state
9999:
.endm

/* Clobbers \state */
.macro fpsimd_restore state, tmpnr
	ldp	q0, q1, [\state, #16 * 0]
	ldp	q2, q3, [\state, #16 * 2]
	ldp	q4, q5, [\state, #16 * 4]
	ldp	q6, q7, [\state, #16 * 6]
	ldp	q8, q9, [\state, #16 * 8]
	ldp	q10, q11, [\state, #16 * 10]
	ldp	q12, q13, [\state, #16 * 12]
	ldp	q14, q15, [\state, #16 * 14]
	ldp	q16, q17, [\state, #16 * 16]
	ldp	q18, q19, [\state, #16 * 18]
	ldp	q20, q21, [\state, #16 * 20]
	ldp	q22, q23, [\state, #16 * 22]
	ldp	q24, q25, [\state, #16 * 24]
	ldp	q26, q27, [\state, #16 * 26]
	ldp	q28, q29, [\state, #16 * 28]
	ldp	q30, q31, [\state, #16 * 30]!
	ldr	w\tmpnr, [\state, #16 * 2]
	msr	fpsr, x\tmpnr
	ldr	w\tmpnr, [\state, #16 * 2 + 4]
	fpsimd_restore_fpcr x\tmpnr, \state
.endm

/* Sanity-check macros to help avoid encoding garbage instructions */

.macro _check_general_reg nr
	.if (\nr) < 0 || (\nr) > 30
		.error "Bad register number \nr."
	.endif
.endm

.macro _sve_check_zreg znr
	.if (\znr) < 0 || (\znr) > 31
		.error "Bad Scalable Vector Extension vector register number \znr."
	.endif
.endm

.macro _sve_check_preg pnr
	.if (\pnr) < 0 || (\pnr) > 15
		.error "Bad Scalable Vector Extension predicate register number \pnr."
	.endif
.endm

.macro _check_num n, min, max
	.if (\n) < (\min) || (\n) > (\max)
		.error "Number \n out of range [\min,\max]"
	.endif
.endm

/* SVE instruction encodings for non-SVE-capable assemblers */
/* (pre binutils 2.28, all kernel capable clang versions support SVE) */

/* STR (vector): STR Z\nz, [X\nxbase, #\offset, MUL VL] */
.macro _sve_str_v nz, nxbase, offset=0
	_sve_check_zreg \nz
	_check_general_reg \nxbase
	_check_num (\offset), -0x100, 0xff
	.inst	0xe5804000			\
		| (\nz)				\
		| ((\nxbase) << 5)		\
		| (((\offset) & 7) << 10)	\
		| (((\offset) & 0x1f8) << 13)
.endm

/* LDR (vector): LDR Z\nz, [X\nxbase, #\offset, MUL VL] */
.macro _sve_ldr_v nz, nxbase, offset=0
	_sve_check_zreg \nz
	_check_general_reg \nxbase
	_check_num (\offset), -0x100, 0xff
	.inst	0x85804000			\
		| (\nz)				\
		| ((\nxbase) << 5)		\
		| (((\offset) & 7) << 10)	\
		| (((\offset) & 0x1f8) << 13)
.endm

/* STR (predicate): STR P\np, [X\nxbase, #\offset, MUL VL] */
.macro _sve_str_p np, nxbase, offset=0
	_sve_check_preg \np
	_check_general_reg \nxbase
	_check_num (\offset), -0x100, 0xff
	.inst	0xe5800000			\
		| (\np)				\
		| ((\nxbase) << 5)		\
		| (((\offset) & 7) << 10)	\
		| (((\offset) & 0x1f8) << 13)
.endm

/* LDR (predicate): LDR P\np, [X\nxbase, #\offset, MUL VL] */
.macro _sve_ldr_p np, nxbase, offset=0
	_sve_check_preg \np
	_check_general_reg \nxbase
	_check_num (\offset), -0x100, 0xff
	.inst	0x85800000			\
		| (\np)				\
		| ((\nxbase) << 5)		\
		| (((\offset) & 7) << 10)	\
		| (((\offset) & 0x1f8) << 13)
.endm

/* RDVL X\nx, #\imm */
.macro _sve_rdvl nx, imm
	_check_general_reg \nx
	_check_num (\imm), -0x20, 0x1f
	.inst	0x04bf5000			\
		| (\nx)				\
		| (((\imm) & 0x3f) << 5)
.endm

/* RDFFR (unpredicated): RDFFR P\np.B */
.macro _sve_rdffr np
	_sve_check_preg \np
	.inst	0x2519f000			\
		| (\np)
.endm

/* WRFFR P\np.B */
.macro _sve_wrffr np
	_sve_check_preg \np
	.inst	0x25289000			\
		| ((\np) << 5)
.endm

/* PFALSE P\np.B */
.macro _sve_pfalse np
	_sve_check_preg \np
	.inst	0x2518e400			\
		| (\np)
.endm

.macro __for from:req, to:req
	.if (\from) == (\to)
		_for__body %\from
	.else
		__for %\from, %((\from) + ((\to) - (\from)) / 2)
		__for %((\from) + ((\to) - (\from)) / 2 + 1), %\to
	.endif
.endm

.macro _for var:req, from:req, to:req, insn:vararg
	.macro _for__body \var:req
		.noaltmacro
		\insn
		.altmacro
	.endm

	.altmacro
	__for \from, \to
	.noaltmacro

	.purgem _for__body
.endm

/* Update ZCR_EL1.LEN with the new VQ */
.macro sve_load_vq xvqminus1, xtmp, xtmp2
		mrs_s		\xtmp, SYS_ZCR_EL1
		bic		\xtmp2, \xtmp, ZCR_ELx_LEN_MASK
		orr		\xtmp2, \xtmp2, \xvqminus1
		cmp		\xtmp2, \xtmp
		b.eq		921f
		msr_s		SYS_ZCR_EL1, \xtmp2	//self-synchronising
921:
.endm

/* Preserve the first 128-bits of Znz and zero the rest. */
.macro _sve_flush_z nz
	_sve_check_zreg \nz
	mov	v\nz\().16b, v\nz\().16b
.endm

.macro sve_flush_z
 _for n, 0, 31, _sve_flush_z	\n
.endm
.macro sve_flush_p_ffr
 _for n, 0, 15, _sve_pfalse	\n
		_sve_wrffr	0
.endm

.macro sve_save nxbase, xpfpsr, nxtmp
 _for n, 0, 31,	_sve_str_v	\n, \nxbase, \n - 34
 _for n, 0, 15,	_sve_str_p	\n, \nxbase, \n - 16
		_sve_rdffr	0
		_sve_str_p	0, \nxbase
		_sve_ldr_p	0, \nxbase, -16

		mrs		x\nxtmp, fpsr
		str		w\nxtmp, [\xpfpsr]
		mrs		x\nxtmp, fpcr
		str		w\nxtmp, [\xpfpsr, #4]
.endm

.macro __sve_load nxbase, xpfpsr, nxtmp
 _for n, 0, 31,	_sve_ldr_v	\n, \nxbase, \n - 34
		_sve_ldr_p	0, \nxbase
		_sve_wrffr	0
 _for n, 0, 15,	_sve_ldr_p	\n, \nxbase, \n - 16

		ldr		w\nxtmp, [\xpfpsr]
		msr		fpsr, x\nxtmp
		ldr		w\nxtmp, [\xpfpsr, #4]
		msr		fpcr, x\nxtmp
.endm

.macro sve_load nxbase, xpfpsr, xvqminus1, nxtmp, xtmp2
		sve_load_vq	\xvqminus1, x\nxtmp, \xtmp2
		__sve_load	\nxbase, \xpfpsr, \nxtmp
.endm
