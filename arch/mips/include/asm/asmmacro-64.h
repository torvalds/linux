/*
 * asmmacro.h: Assembler macros to make things easier to read.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1998, 1999 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_ASMMACRO_64_H
#define _ASM_ASMMACRO_64_H

#include <asm/asm-offsets.h>
#include <asm/regdef.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>

	.macro	fpu_save_16even thread tmp=t0
	cfc1	\tmp, fcr31
	sdc1	$f0,  THREAD_FPR0(\thread)
	sdc1	$f2,  THREAD_FPR2(\thread)
	sdc1	$f4,  THREAD_FPR4(\thread)
	sdc1	$f6,  THREAD_FPR6(\thread)
	sdc1	$f8,  THREAD_FPR8(\thread)
	sdc1	$f10, THREAD_FPR10(\thread)
	sdc1	$f12, THREAD_FPR12(\thread)
	sdc1	$f14, THREAD_FPR14(\thread)
	sdc1	$f16, THREAD_FPR16(\thread)
	sdc1	$f18, THREAD_FPR18(\thread)
	sdc1	$f20, THREAD_FPR20(\thread)
	sdc1	$f22, THREAD_FPR22(\thread)
	sdc1	$f24, THREAD_FPR24(\thread)
	sdc1	$f26, THREAD_FPR26(\thread)
	sdc1	$f28, THREAD_FPR28(\thread)
	sdc1	$f30, THREAD_FPR30(\thread)
	sw	\tmp, THREAD_FCR31(\thread)
	.endm

	.macro	fpu_save_16odd thread
	sdc1	$f1,  THREAD_FPR1(\thread)
	sdc1	$f3,  THREAD_FPR3(\thread)
	sdc1	$f5,  THREAD_FPR5(\thread)
	sdc1	$f7,  THREAD_FPR7(\thread)
	sdc1	$f9,  THREAD_FPR9(\thread)
	sdc1	$f11, THREAD_FPR11(\thread)
	sdc1	$f13, THREAD_FPR13(\thread)
	sdc1	$f15, THREAD_FPR15(\thread)
	sdc1	$f17, THREAD_FPR17(\thread)
	sdc1	$f19, THREAD_FPR19(\thread)
	sdc1	$f21, THREAD_FPR21(\thread)
	sdc1	$f23, THREAD_FPR23(\thread)
	sdc1	$f25, THREAD_FPR25(\thread)
	sdc1	$f27, THREAD_FPR27(\thread)
	sdc1	$f29, THREAD_FPR29(\thread)
	sdc1	$f31, THREAD_FPR31(\thread)
	.endm

	.macro	fpu_save_double thread status tmp
	sll	\tmp, \status, 5
	bgez	\tmp, 2f
	fpu_save_16odd \thread
2:
	fpu_save_16even \thread \tmp
	.endm

	.macro	fpu_restore_16even thread tmp=t0
	lw	\tmp, THREAD_FCR31(\thread)
	ldc1	$f0,  THREAD_FPR0(\thread)
	ldc1	$f2,  THREAD_FPR2(\thread)
	ldc1	$f4,  THREAD_FPR4(\thread)
	ldc1	$f6,  THREAD_FPR6(\thread)
	ldc1	$f8,  THREAD_FPR8(\thread)
	ldc1	$f10, THREAD_FPR10(\thread)
	ldc1	$f12, THREAD_FPR12(\thread)
	ldc1	$f14, THREAD_FPR14(\thread)
	ldc1	$f16, THREAD_FPR16(\thread)
	ldc1	$f18, THREAD_FPR18(\thread)
	ldc1	$f20, THREAD_FPR20(\thread)
	ldc1	$f22, THREAD_FPR22(\thread)
	ldc1	$f24, THREAD_FPR24(\thread)
	ldc1	$f26, THREAD_FPR26(\thread)
	ldc1	$f28, THREAD_FPR28(\thread)
	ldc1	$f30, THREAD_FPR30(\thread)
	ctc1	\tmp, fcr31
	.endm

	.macro	fpu_restore_16odd thread
	ldc1	$f1,  THREAD_FPR1(\thread)
	ldc1	$f3,  THREAD_FPR3(\thread)
	ldc1	$f5,  THREAD_FPR5(\thread)
	ldc1	$f7,  THREAD_FPR7(\thread)
	ldc1	$f9,  THREAD_FPR9(\thread)
	ldc1	$f11, THREAD_FPR11(\thread)
	ldc1	$f13, THREAD_FPR13(\thread)
	ldc1	$f15, THREAD_FPR15(\thread)
	ldc1	$f17, THREAD_FPR17(\thread)
	ldc1	$f19, THREAD_FPR19(\thread)
	ldc1	$f21, THREAD_FPR21(\thread)
	ldc1	$f23, THREAD_FPR23(\thread)
	ldc1	$f25, THREAD_FPR25(\thread)
	ldc1	$f27, THREAD_FPR27(\thread)
	ldc1	$f29, THREAD_FPR29(\thread)
	ldc1	$f31, THREAD_FPR31(\thread)
	.endm

	.macro	fpu_restore_double thread status tmp
	sll	\tmp, \status, 5
	bgez	\tmp, 1f				# 16 register mode?

	fpu_restore_16odd \thread
1:	fpu_restore_16even \thread \tmp
	.endm

	.macro	cpu_save_nonscratch thread
	LONG_S	s0, THREAD_REG16(\thread)
	LONG_S	s1, THREAD_REG17(\thread)
	LONG_S	s2, THREAD_REG18(\thread)
	LONG_S	s3, THREAD_REG19(\thread)
	LONG_S	s4, THREAD_REG20(\thread)
	LONG_S	s5, THREAD_REG21(\thread)
	LONG_S	s6, THREAD_REG22(\thread)
	LONG_S	s7, THREAD_REG23(\thread)
	LONG_S	sp, THREAD_REG29(\thread)
	LONG_S	fp, THREAD_REG30(\thread)
	.endm

	.macro	cpu_restore_nonscratch thread
	LONG_L	s0, THREAD_REG16(\thread)
	LONG_L	s1, THREAD_REG17(\thread)
	LONG_L	s2, THREAD_REG18(\thread)
	LONG_L	s3, THREAD_REG19(\thread)
	LONG_L	s4, THREAD_REG20(\thread)
	LONG_L	s5, THREAD_REG21(\thread)
	LONG_L	s6, THREAD_REG22(\thread)
	LONG_L	s7, THREAD_REG23(\thread)
	LONG_L	sp, THREAD_REG29(\thread)
	LONG_L	fp, THREAD_REG30(\thread)
	LONG_L	ra, THREAD_REG31(\thread)
	.endm

#endif /* _ASM_ASMMACRO_64_H */
