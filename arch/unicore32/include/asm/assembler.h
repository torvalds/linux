/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/assembler.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 *  Do not include any C declarations in this file - it is included by
 *  assembler source.
 */
#ifndef __ASSEMBLY__
#error "Only include this from assembly code"
#endif

#include <asm/ptrace.h>

/*
 * Little Endian independent macros for shifting bytes within registers.
 */
#define pull            >>
#define push            <<
#define get_byte_0      << #0
#define get_byte_1	>> #8
#define get_byte_2	>> #16
#define get_byte_3	>> #24
#define put_byte_0      << #0
#define put_byte_1	<< #8
#define put_byte_2	<< #16
#define put_byte_3	<< #24

#define cadd		cmpadd
#define cand		cmpand
#define csub		cmpsub
#define cxor		cmpxor

/*
 * Enable and disable interrupts
 */
	.macro disable_irq, temp
	mov	\temp, asr
	andn     \temp, \temp, #0xFF
	or	\temp, \temp, #PSR_I_BIT | PRIV_MODE
	mov.a	asr, \temp
	.endm

	.macro enable_irq, temp
	mov	\temp, asr
	andn     \temp, \temp, #0xFF
	or	\temp, \temp, #PRIV_MODE
	mov.a	asr, \temp
	.endm

#define USER(x...)				\
9999:	x;					\
	.pushsection __ex_table, "a";		\
	.align	3;				\
	.long	9999b, 9001f;			\
	.popsection

	.macro	notcond, cond, nexti = .+8
	.ifc	\cond, eq
		bne	\nexti
	.else;	.ifc	\cond, ne
		beq	\nexti
	.else;	.ifc	\cond, ea
		bub	\nexti
	.else;	.ifc	\cond, ub
		bea	\nexti
	.else;	.ifc	\cond, fs
		bns	\nexti
	.else;	.ifc	\cond, ns
		bfs	\nexti
	.else;	.ifc	\cond, fv
		bnv	\nexti
	.else;	.ifc	\cond, nv
		bfv	\nexti
	.else;	.ifc	\cond, ua
		beb	\nexti
	.else;	.ifc	\cond, eb
		bua	\nexti
	.else;	.ifc	\cond, eg
		bsl	\nexti
	.else;	.ifc	\cond, sl
		beg	\nexti
	.else;	.ifc	\cond, sg
		bel	\nexti
	.else;	.ifc	\cond, el
		bsg	\nexti
	.else;	.ifnc	\cond, al
		.error  "Unknown cond in notcond macro argument"
	.endif;	.endif;	.endif;	.endif;	.endif;	.endif;	.endif
	.endif;	.endif;	.endif;	.endif;	.endif;	.endif;	.endif
	.endif
	.endm

	.macro	usracc, instr, reg, ptr, inc, cond, rept, abort
	.rept	\rept
	notcond	\cond, .+8
9999 :
	.if	\inc == 1
	\instr\()b.u \reg, [\ptr], #\inc
	.elseif	\inc == 4
	\instr\()w.u \reg, [\ptr], #\inc
	.else
	.error	"Unsupported inc macro argument"
	.endif

	.pushsection __ex_table, "a"
	.align	3
	.long	9999b, \abort
	.popsection
	.endr
	.endm

	.macro	strusr, reg, ptr, inc, cond = al, rept = 1, abort = 9001f
	usracc	st, \reg, \ptr, \inc, \cond, \rept, \abort
	.endm

	.macro	ldrusr, reg, ptr, inc, cond = al, rept = 1, abort = 9001f
	usracc	ld, \reg, \ptr, \inc, \cond, \rept, \abort
	.endm

	.macro	nop8
	.rept	8
		nop
	.endr
	.endm
