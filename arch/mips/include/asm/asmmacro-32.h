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
	cfc1	\tmp,  fcr31
	swc1	$f0,  THREAD_FPR0(\thread)
	swc1	$f1,  THREAD_FPR1(\thread)
	swc1	$f2,  THREAD_FPR2(\thread)
	swc1	$f3,  THREAD_FPR3(\thread)
	swc1	$f4,  THREAD_FPR4(\thread)
	swc1	$f5,  THREAD_FPR5(\thread)
	swc1	$f6,  THREAD_FPR6(\thread)
	swc1	$f7,  THREAD_FPR7(\thread)
	swc1	$f8,  THREAD_FPR8(\thread)
	swc1	$f9,  THREAD_FPR9(\thread)
	swc1	$f10, THREAD_FPR10(\thread)
	swc1	$f11, THREAD_FPR11(\thread)
	swc1	$f12, THREAD_FPR12(\thread)
	swc1	$f13, THREAD_FPR13(\thread)
	swc1	$f14, THREAD_FPR14(\thread)
	swc1	$f15, THREAD_FPR15(\thread)
	swc1	$f16, THREAD_FPR16(\thread)
	swc1	$f17, THREAD_FPR17(\thread)
	swc1	$f18, THREAD_FPR18(\thread)
	swc1	$f19, THREAD_FPR19(\thread)
	swc1	$f20, THREAD_FPR20(\thread)
	swc1	$f21, THREAD_FPR21(\thread)
	swc1	$f22, THREAD_FPR22(\thread)
	swc1	$f23, THREAD_FPR23(\thread)
	swc1	$f24, THREAD_FPR24(\thread)
	swc1	$f25, THREAD_FPR25(\thread)
	swc1	$f26, THREAD_FPR26(\thread)
	swc1	$f27, THREAD_FPR27(\thread)
	swc1	$f28, THREAD_FPR28(\thread)
	swc1	$f29, THREAD_FPR29(\thread)
	swc1	$f30, THREAD_FPR30(\thread)
	swc1	$f31, THREAD_FPR31(\thread)
	sw	\tmp, THREAD_FCR31(\thread)
	.endm

	.macro	fpu_restore_single thread tmp=t0
	lw	\tmp, THREAD_FCR31(\thread)
	lwc1	$f0,  THREAD_FPR0(\thread)
	lwc1	$f1,  THREAD_FPR1(\thread)
	lwc1	$f2,  THREAD_FPR2(\thread)
	lwc1	$f3,  THREAD_FPR3(\thread)
	lwc1	$f4,  THREAD_FPR4(\thread)
	lwc1	$f5,  THREAD_FPR5(\thread)
	lwc1	$f6,  THREAD_FPR6(\thread)
	lwc1	$f7,  THREAD_FPR7(\thread)
	lwc1	$f8,  THREAD_FPR8(\thread)
	lwc1	$f9,  THREAD_FPR9(\thread)
	lwc1	$f10, THREAD_FPR10(\thread)
	lwc1	$f11, THREAD_FPR11(\thread)
	lwc1	$f12, THREAD_FPR12(\thread)
	lwc1	$f13, THREAD_FPR13(\thread)
	lwc1	$f14, THREAD_FPR14(\thread)
	lwc1	$f15, THREAD_FPR15(\thread)
	lwc1	$f16, THREAD_FPR16(\thread)
	lwc1	$f17, THREAD_FPR17(\thread)
	lwc1	$f18, THREAD_FPR18(\thread)
	lwc1	$f19, THREAD_FPR19(\thread)
	lwc1	$f20, THREAD_FPR20(\thread)
	lwc1	$f21, THREAD_FPR21(\thread)
	lwc1	$f22, THREAD_FPR22(\thread)
	lwc1	$f23, THREAD_FPR23(\thread)
	lwc1	$f24, THREAD_FPR24(\thread)
	lwc1	$f25, THREAD_FPR25(\thread)
	lwc1	$f26, THREAD_FPR26(\thread)
	lwc1	$f27, THREAD_FPR27(\thread)
	lwc1	$f28, THREAD_FPR28(\thread)
	lwc1	$f29, THREAD_FPR29(\thread)
	lwc1	$f30, THREAD_FPR30(\thread)
	lwc1	$f31, THREAD_FPR31(\thread)
	ctc1	\tmp, fcr31
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
