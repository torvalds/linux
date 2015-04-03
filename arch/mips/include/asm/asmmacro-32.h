/*
 * asmmacro.h: Assembler macros to make things easier to read.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1998, 1999, 2003 Ralf Baechle
 */
#ifndef _ASM_ASMMACRO_32_H
#define _ASM_ASMMACRO_32_H

#include <asm/asm-offsets.h>
#include <asm/regdef.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>

	.macro	fpu_save_single thread tmp=t0
	.set push
	SET_HARDFLOAT
	cfc1	\tmp,  fcr31
	s.d	$f0,  THREAD_FPR0_LS64(\thread)
	s.d	$f2,  THREAD_FPR2_LS64(\thread)
	s.d	$f4,  THREAD_FPR4_LS64(\thread)
	s.d	$f6,  THREAD_FPR6_LS64(\thread)
	s.d	$f8,  THREAD_FPR8_LS64(\thread)
	s.d	$f10, THREAD_FPR10_LS64(\thread)
	s.d	$f12, THREAD_FPR12_LS64(\thread)
	s.d	$f14, THREAD_FPR14_LS64(\thread)
	s.d	$f16, THREAD_FPR16_LS64(\thread)
	s.d	$f18, THREAD_FPR18_LS64(\thread)
	s.d	$f20, THREAD_FPR20_LS64(\thread)
	s.d	$f22, THREAD_FPR22_LS64(\thread)
	s.d	$f24, THREAD_FPR24_LS64(\thread)
	s.d	$f26, THREAD_FPR26_LS64(\thread)
	s.d	$f28, THREAD_FPR28_LS64(\thread)
	s.d	$f30, THREAD_FPR30_LS64(\thread)
	sw	\tmp, THREAD_FCR31(\thread)
	.set pop
	.endm

	.macro	fpu_restore_single thread tmp=t0
	.set push
	SET_HARDFLOAT
	lw	\tmp, THREAD_FCR31(\thread)
	l.d	$f0,  THREAD_FPR0_LS64(\thread)
	l.d	$f2,  THREAD_FPR2_LS64(\thread)
	l.d	$f4,  THREAD_FPR4_LS64(\thread)
	l.d	$f6,  THREAD_FPR6_LS64(\thread)
	l.d	$f8,  THREAD_FPR8_LS64(\thread)
	l.d	$f10, THREAD_FPR10_LS64(\thread)
	l.d	$f12, THREAD_FPR12_LS64(\thread)
	l.d	$f14, THREAD_FPR14_LS64(\thread)
	l.d	$f16, THREAD_FPR16_LS64(\thread)
	l.d	$f18, THREAD_FPR18_LS64(\thread)
	l.d	$f20, THREAD_FPR20_LS64(\thread)
	l.d	$f22, THREAD_FPR22_LS64(\thread)
	l.d	$f24, THREAD_FPR24_LS64(\thread)
	l.d	$f26, THREAD_FPR26_LS64(\thread)
	l.d	$f28, THREAD_FPR28_LS64(\thread)
	l.d	$f30, THREAD_FPR30_LS64(\thread)
	ctc1	\tmp, fcr31
	.set pop
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

#endif /* _ASM_ASMMACRO_32_H */
