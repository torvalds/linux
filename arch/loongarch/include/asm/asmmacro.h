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

	.macro	parse_v var val
	\var	= \val
	.endm

	.macro	parse_r var r
	\var	= -1
	.ifc	\r, $r0
	\var	= 0
	.endif
	.ifc	\r, $r1
	\var	= 1
	.endif
	.ifc	\r, $r2
	\var	= 2
	.endif
	.ifc	\r, $r3
	\var	= 3
	.endif
	.ifc	\r, $r4
	\var	= 4
	.endif
	.ifc	\r, $r5
	\var	= 5
	.endif
	.ifc	\r, $r6
	\var	= 6
	.endif
	.ifc	\r, $r7
	\var	= 7
	.endif
	.ifc	\r, $r8
	\var	= 8
	.endif
	.ifc	\r, $r9
	\var	= 9
	.endif
	.ifc	\r, $r10
	\var	= 10
	.endif
	.ifc	\r, $r11
	\var	= 11
	.endif
	.ifc	\r, $r12
	\var	= 12
	.endif
	.ifc	\r, $r13
	\var	= 13
	.endif
	.ifc	\r, $r14
	\var	= 14
	.endif
	.ifc	\r, $r15
	\var	= 15
	.endif
	.ifc	\r, $r16
	\var	= 16
	.endif
	.ifc	\r, $r17
	\var	= 17
	.endif
	.ifc	\r, $r18
	\var	= 18
	.endif
	.ifc	\r, $r19
	\var	= 19
	.endif
	.ifc	\r, $r20
	\var	= 20
	.endif
	.ifc	\r, $r21
	\var	= 21
	.endif
	.ifc	\r, $r22
	\var	= 22
	.endif
	.ifc	\r, $r23
	\var	= 23
	.endif
	.ifc	\r, $r24
	\var	= 24
	.endif
	.ifc	\r, $r25
	\var	= 25
	.endif
	.ifc	\r, $r26
	\var	= 26
	.endif
	.ifc	\r, $r27
	\var	= 27
	.endif
	.ifc	\r, $r28
	\var	= 28
	.endif
	.ifc	\r, $r29
	\var	= 29
	.endif
	.ifc	\r, $r30
	\var	= 30
	.endif
	.ifc	\r, $r31
	\var	= 31
	.endif
	.iflt	\var
	.error	"Unable to parse register name \r"
	.endif
	.endm

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
	stptr.w	\tmp, \thread, THREAD_FCSR
	.endm

	.macro fpu_restore_csr thread tmp
	ldptr.w	\tmp, \thread, THREAD_FCSR
	movgr2fcsr	fcsr0, \tmp
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

.macro not dst src
	nor	\dst, \src, zero
.endm

.macro bgt r0 r1 label
	blt	\r1, \r0, \label
.endm

.macro bltz r0 label
	blt	\r0, zero, \label
.endm

.macro bgez r0 label
	bge	\r0, zero, \label
.endm

#endif /* _ASM_ASMMACRO_H */
