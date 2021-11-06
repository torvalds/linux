/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 1999 Hewlett-Packard (Frank Rowand)
 * Copyright (C) 1999 Philipp Rumpf <prumpf@tux.org>
 * Copyright (C) 1999 SuSE GmbH
 */

#ifndef _PARISC_ASSEMBLY_H
#define _PARISC_ASSEMBLY_H

#define CALLEE_FLOAT_FRAME_SIZE	80

#ifdef CONFIG_64BIT
#define LDREG	ldd
#define STREG	std
#define LDREGX  ldd,s
#define LDREGM	ldd,mb
#define STREGM	std,ma
#define SHRREG	shrd
#define SHLREG	shld
#define ANDCM   andcm,*
#define	COND(x)	* ## x
#define RP_OFFSET	16
#define FRAME_SIZE	128
#define CALLEE_REG_FRAME_SIZE	144
#define REG_SZ		8
#define ASM_ULONG_INSN	.dword
#else	/* CONFIG_64BIT */
#define LDREG	ldw
#define STREG	stw
#define LDREGX  ldwx,s
#define LDREGM	ldwm
#define STREGM	stwm
#define SHRREG	shr
#define SHLREG	shlw
#define ANDCM   andcm
#define COND(x)	x
#define RP_OFFSET	20
#define FRAME_SIZE	64
#define CALLEE_REG_FRAME_SIZE	128
#define REG_SZ		4
#define ASM_ULONG_INSN	.word
#endif

/* Frame alignment for 32- and 64-bit */
#define FRAME_ALIGN     64

#define CALLEE_SAVE_FRAME_SIZE (CALLEE_REG_FRAME_SIZE + CALLEE_FLOAT_FRAME_SIZE)

#ifdef CONFIG_PA20
#define LDCW		ldcw,co
#define BL		b,l
# ifdef CONFIG_64BIT
#  define PA_ASM_LEVEL	2.0w
# else
#  define PA_ASM_LEVEL	2.0
# endif
#else
#define LDCW		ldcw
#define BL		bl
#define PA_ASM_LEVEL	1.1
#endif

/* Privilege level field in the rightmost two bits of the IA queues */
#define PRIV_USER	3
#define PRIV_KERNEL	0

#ifdef __ASSEMBLY__

#ifdef CONFIG_64BIT
/* the 64-bit pa gnu assembler unfortunately defaults to .level 1.1 or 2.0 so
 * work around that for now... */
	.level 2.0w
#endif

#include <asm/asm-offsets.h>
#include <asm/page.h>
#include <asm/types.h>

#include <asm/asmregs.h>
#include <asm/psw.h>

	sp	=	30
	gp	=	27
	ipsw	=	22

	/*
	 * We provide two versions of each macro to convert from physical
	 * to virtual and vice versa. The "_r1" versions take one argument
	 * register, but trashes r1 to do the conversion. The other
	 * version takes two arguments: a src and destination register.
	 * However, the source and destination registers can not be
	 * the same register.
	 */

	.macro  tophys  grvirt, grphys
	ldil    L%(__PAGE_OFFSET), \grphys
	sub     \grvirt, \grphys, \grphys
	.endm
	
	.macro  tovirt  grphys, grvirt
	ldil    L%(__PAGE_OFFSET), \grvirt
	add     \grphys, \grvirt, \grvirt
	.endm

	.macro  tophys_r1  gr
	ldil    L%(__PAGE_OFFSET), %r1
	sub     \gr, %r1, \gr
	.endm
	
	.macro  tovirt_r1  gr
	ldil    L%(__PAGE_OFFSET), %r1
	add     \gr, %r1, \gr
	.endm

	.macro delay value
	ldil	L%\value, 1
	ldo	R%\value(1), 1
	addib,UV,n -1,1,.
	addib,NUV,n -1,1,.+8
	nop
	.endm

	.macro	debug value
	.endm

	.macro shlw r, sa, t
	zdep	\r, 31-(\sa), 32-(\sa), \t
	.endm

	/* And the PA 2.0W shift left */
	.macro shld r, sa, t
	depd,z	\r, 63-(\sa), 64-(\sa), \t
	.endm

	/* Shift Right - note the r and t can NOT be the same! */
	.macro shr r, sa, t
	extru \r, 31-(\sa), 32-(\sa), \t
	.endm

	/* pa20w version of shift right */
	.macro shrd r, sa, t
	extrd,u \r, 63-(\sa), 64-(\sa), \t
	.endm

	/* load 32-bit 'value' into 'reg' compensating for the ldil
	 * sign-extension when running in wide mode.
	 * WARNING!! neither 'value' nor 'reg' can be expressions
	 * containing '.'!!!! */
	.macro	load32 value, reg
	ldil	L%\value, \reg
	ldo	R%\value(\reg), \reg
	.endm

	.macro loadgp
#ifdef CONFIG_64BIT
	ldil		L%__gp, %r27
	ldo		R%__gp(%r27), %r27
#else
	ldil		L%$global$, %r27
	ldo		R%$global$(%r27), %r27
#endif
	.endm

#define SAVE_SP(r, where) mfsp r, %r1 ! STREG %r1, where
#define REST_SP(r, where) LDREG where, %r1 ! mtsp %r1, r
#define SAVE_CR(r, where) mfctl r, %r1 ! STREG %r1, where
#define REST_CR(r, where) LDREG where, %r1 ! mtctl %r1, r

	.macro	save_general	regs
	STREG %r1, PT_GR1 (\regs)
	STREG %r2, PT_GR2 (\regs)
	STREG %r3, PT_GR3 (\regs)
	STREG %r4, PT_GR4 (\regs)
	STREG %r5, PT_GR5 (\regs)
	STREG %r6, PT_GR6 (\regs)
	STREG %r7, PT_GR7 (\regs)
	STREG %r8, PT_GR8 (\regs)
	STREG %r9, PT_GR9 (\regs)
	STREG %r10, PT_GR10(\regs)
	STREG %r11, PT_GR11(\regs)
	STREG %r12, PT_GR12(\regs)
	STREG %r13, PT_GR13(\regs)
	STREG %r14, PT_GR14(\regs)
	STREG %r15, PT_GR15(\regs)
	STREG %r16, PT_GR16(\regs)
	STREG %r17, PT_GR17(\regs)
	STREG %r18, PT_GR18(\regs)
	STREG %r19, PT_GR19(\regs)
	STREG %r20, PT_GR20(\regs)
	STREG %r21, PT_GR21(\regs)
	STREG %r22, PT_GR22(\regs)
	STREG %r23, PT_GR23(\regs)
	STREG %r24, PT_GR24(\regs)
	STREG %r25, PT_GR25(\regs)
	/* r26 is saved in get_stack and used to preserve a value across virt_map */
	STREG %r27, PT_GR27(\regs)
	STREG %r28, PT_GR28(\regs)
	/* r29 is saved in get_stack and used to point to saved registers */
	/* r30 stack pointer saved in get_stack */
	STREG %r31, PT_GR31(\regs)
	.endm

	.macro	rest_general	regs
	/* r1 used as a temp in rest_stack and is restored there */
	LDREG PT_GR2 (\regs), %r2
	LDREG PT_GR3 (\regs), %r3
	LDREG PT_GR4 (\regs), %r4
	LDREG PT_GR5 (\regs), %r5
	LDREG PT_GR6 (\regs), %r6
	LDREG PT_GR7 (\regs), %r7
	LDREG PT_GR8 (\regs), %r8
	LDREG PT_GR9 (\regs), %r9
	LDREG PT_GR10(\regs), %r10
	LDREG PT_GR11(\regs), %r11
	LDREG PT_GR12(\regs), %r12
	LDREG PT_GR13(\regs), %r13
	LDREG PT_GR14(\regs), %r14
	LDREG PT_GR15(\regs), %r15
	LDREG PT_GR16(\regs), %r16
	LDREG PT_GR17(\regs), %r17
	LDREG PT_GR18(\regs), %r18
	LDREG PT_GR19(\regs), %r19
	LDREG PT_GR20(\regs), %r20
	LDREG PT_GR21(\regs), %r21
	LDREG PT_GR22(\regs), %r22
	LDREG PT_GR23(\regs), %r23
	LDREG PT_GR24(\regs), %r24
	LDREG PT_GR25(\regs), %r25
	LDREG PT_GR26(\regs), %r26
	LDREG PT_GR27(\regs), %r27
	LDREG PT_GR28(\regs), %r28
	/* r29 points to register save area, and is restored in rest_stack */
	/* r30 stack pointer restored in rest_stack */
	LDREG PT_GR31(\regs), %r31
	.endm

	.macro	save_fp 	regs
	fstd,ma  %fr0, 8(\regs)
	fstd,ma	 %fr1, 8(\regs)
	fstd,ma	 %fr2, 8(\regs)
	fstd,ma	 %fr3, 8(\regs)
	fstd,ma	 %fr4, 8(\regs)
	fstd,ma	 %fr5, 8(\regs)
	fstd,ma	 %fr6, 8(\regs)
	fstd,ma	 %fr7, 8(\regs)
	fstd,ma	 %fr8, 8(\regs)
	fstd,ma	 %fr9, 8(\regs)
	fstd,ma	%fr10, 8(\regs)
	fstd,ma	%fr11, 8(\regs)
	fstd,ma	%fr12, 8(\regs)
	fstd,ma	%fr13, 8(\regs)
	fstd,ma	%fr14, 8(\regs)
	fstd,ma	%fr15, 8(\regs)
	fstd,ma	%fr16, 8(\regs)
	fstd,ma	%fr17, 8(\regs)
	fstd,ma	%fr18, 8(\regs)
	fstd,ma	%fr19, 8(\regs)
	fstd,ma	%fr20, 8(\regs)
	fstd,ma	%fr21, 8(\regs)
	fstd,ma	%fr22, 8(\regs)
	fstd,ma	%fr23, 8(\regs)
	fstd,ma	%fr24, 8(\regs)
	fstd,ma	%fr25, 8(\regs)
	fstd,ma	%fr26, 8(\regs)
	fstd,ma	%fr27, 8(\regs)
	fstd,ma	%fr28, 8(\regs)
	fstd,ma	%fr29, 8(\regs)
	fstd,ma	%fr30, 8(\regs)
	fstd	%fr31, 0(\regs)
	.endm

	.macro	rest_fp 	regs
	fldd	0(\regs),	 %fr31
	fldd,mb	-8(\regs),       %fr30
	fldd,mb	-8(\regs),       %fr29
	fldd,mb	-8(\regs),       %fr28
	fldd,mb	-8(\regs),       %fr27
	fldd,mb	-8(\regs),       %fr26
	fldd,mb	-8(\regs),       %fr25
	fldd,mb	-8(\regs),       %fr24
	fldd,mb	-8(\regs),       %fr23
	fldd,mb	-8(\regs),       %fr22
	fldd,mb	-8(\regs),       %fr21
	fldd,mb	-8(\regs),       %fr20
	fldd,mb	-8(\regs),       %fr19
	fldd,mb	-8(\regs),       %fr18
	fldd,mb	-8(\regs),       %fr17
	fldd,mb	-8(\regs),       %fr16
	fldd,mb	-8(\regs),       %fr15
	fldd,mb	-8(\regs),       %fr14
	fldd,mb	-8(\regs),       %fr13
	fldd,mb	-8(\regs),       %fr12
	fldd,mb	-8(\regs),       %fr11
	fldd,mb	-8(\regs),       %fr10
	fldd,mb	-8(\regs),       %fr9
	fldd,mb	-8(\regs),       %fr8
	fldd,mb	-8(\regs),       %fr7
	fldd,mb	-8(\regs),       %fr6
	fldd,mb	-8(\regs),       %fr5
	fldd,mb	-8(\regs),       %fr4
	fldd,mb	-8(\regs),       %fr3
	fldd,mb	-8(\regs),       %fr2
	fldd,mb	-8(\regs),       %fr1
	fldd,mb	-8(\regs),       %fr0
	.endm

	.macro	callee_save_float
	fstd,ma	 %fr12,	8(%r30)
	fstd,ma	 %fr13,	8(%r30)
	fstd,ma	 %fr14,	8(%r30)
	fstd,ma	 %fr15,	8(%r30)
	fstd,ma	 %fr16,	8(%r30)
	fstd,ma	 %fr17,	8(%r30)
	fstd,ma	 %fr18,	8(%r30)
	fstd,ma	 %fr19,	8(%r30)
	fstd,ma	 %fr20,	8(%r30)
	fstd,ma	 %fr21,	8(%r30)
	.endm

	.macro	callee_rest_float
	fldd,mb	-8(%r30),   %fr21
	fldd,mb	-8(%r30),   %fr20
	fldd,mb	-8(%r30),   %fr19
	fldd,mb	-8(%r30),   %fr18
	fldd,mb	-8(%r30),   %fr17
	fldd,mb	-8(%r30),   %fr16
	fldd,mb	-8(%r30),   %fr15
	fldd,mb	-8(%r30),   %fr14
	fldd,mb	-8(%r30),   %fr13
	fldd,mb	-8(%r30),   %fr12
	.endm

#ifdef CONFIG_64BIT
	.macro	callee_save
	std,ma	  %r3,	 CALLEE_REG_FRAME_SIZE(%r30)
	mfctl	  %cr27, %r3
	std	  %r4,	-136(%r30)
	std	  %r5,	-128(%r30)
	std	  %r6,	-120(%r30)
	std	  %r7,	-112(%r30)
	std	  %r8,	-104(%r30)
	std	  %r9,	 -96(%r30)
	std	 %r10,	 -88(%r30)
	std	 %r11,	 -80(%r30)
	std	 %r12,	 -72(%r30)
	std	 %r13,	 -64(%r30)
	std	 %r14,	 -56(%r30)
	std	 %r15,	 -48(%r30)
	std	 %r16,	 -40(%r30)
	std	 %r17,	 -32(%r30)
	std	 %r18,	 -24(%r30)
	std	  %r3,	 -16(%r30)
	.endm

	.macro	callee_rest
	ldd	 -16(%r30),    %r3
	ldd	 -24(%r30),   %r18
	ldd	 -32(%r30),   %r17
	ldd	 -40(%r30),   %r16
	ldd	 -48(%r30),   %r15
	ldd	 -56(%r30),   %r14
	ldd	 -64(%r30),   %r13
	ldd	 -72(%r30),   %r12
	ldd	 -80(%r30),   %r11
	ldd	 -88(%r30),   %r10
	ldd	 -96(%r30),    %r9
	ldd	-104(%r30),    %r8
	ldd	-112(%r30),    %r7
	ldd	-120(%r30),    %r6
	ldd	-128(%r30),    %r5
	ldd	-136(%r30),    %r4
	mtctl	%r3, %cr27
	ldd,mb	-CALLEE_REG_FRAME_SIZE(%r30),    %r3
	.endm

#else /* ! CONFIG_64BIT */

	.macro	callee_save
	stw,ma	 %r3,	CALLEE_REG_FRAME_SIZE(%r30)
	mfctl	 %cr27, %r3
	stw	 %r4,	-124(%r30)
	stw	 %r5,	-120(%r30)
	stw	 %r6,	-116(%r30)
	stw	 %r7,	-112(%r30)
	stw	 %r8,	-108(%r30)
	stw	 %r9,	-104(%r30)
	stw	 %r10,	-100(%r30)
	stw	 %r11,	 -96(%r30)
	stw	 %r12,	 -92(%r30)
	stw	 %r13,	 -88(%r30)
	stw	 %r14,	 -84(%r30)
	stw	 %r15,	 -80(%r30)
	stw	 %r16,	 -76(%r30)
	stw	 %r17,	 -72(%r30)
	stw	 %r18,	 -68(%r30)
	stw	  %r3,	 -64(%r30)
	.endm

	.macro	callee_rest
	ldw	 -64(%r30),    %r3
	ldw	 -68(%r30),   %r18
	ldw	 -72(%r30),   %r17
	ldw	 -76(%r30),   %r16
	ldw	 -80(%r30),   %r15
	ldw	 -84(%r30),   %r14
	ldw	 -88(%r30),   %r13
	ldw	 -92(%r30),   %r12
	ldw	 -96(%r30),   %r11
	ldw	-100(%r30),   %r10
	ldw	-104(%r30),   %r9
	ldw	-108(%r30),   %r8
	ldw	-112(%r30),   %r7
	ldw	-116(%r30),   %r6
	ldw	-120(%r30),   %r5
	ldw	-124(%r30),   %r4
	mtctl	%r3, %cr27
	ldw,mb	-CALLEE_REG_FRAME_SIZE(%r30),   %r3
	.endm
#endif /* ! CONFIG_64BIT */

	.macro	save_specials	regs

	SAVE_SP  (%sr0, PT_SR0 (\regs))
	SAVE_SP  (%sr1, PT_SR1 (\regs))
	SAVE_SP  (%sr2, PT_SR2 (\regs))
	SAVE_SP  (%sr3, PT_SR3 (\regs))
	SAVE_SP  (%sr4, PT_SR4 (\regs))
	SAVE_SP  (%sr5, PT_SR5 (\regs))
	SAVE_SP  (%sr6, PT_SR6 (\regs))

	SAVE_CR  (%cr17, PT_IASQ0(\regs))
	mtctl	 %r0,	%cr17
	SAVE_CR  (%cr17, PT_IASQ1(\regs))

	SAVE_CR  (%cr18, PT_IAOQ0(\regs))
	mtctl	 %r0,	%cr18
	SAVE_CR  (%cr18, PT_IAOQ1(\regs))

#ifdef CONFIG_64BIT
	/* cr11 (sar) is a funny one.  5 bits on PA1.1 and 6 bit on PA2.0
	 * For PA2.0 mtsar or mtctl always write 6 bits, but mfctl only
	 * reads 5 bits.  Use mfctl,w to read all six bits.  Otherwise
	 * we lose the 6th bit on a save/restore over interrupt.
	 */
	mfctl,w  %cr11, %r1
	STREG    %r1, PT_SAR (\regs)
#else
	SAVE_CR  (%cr11, PT_SAR  (\regs))
#endif
	SAVE_CR  (%cr19, PT_IIR  (\regs))

	/*
	 * Code immediately following this macro (in intr_save) relies
	 * on r8 containing ipsw.
	 */
	mfctl    %cr22, %r8
	STREG    %r8,   PT_PSW(\regs)
	.endm

	.macro	rest_specials	regs

	REST_SP  (%sr0, PT_SR0 (\regs))
	REST_SP  (%sr1, PT_SR1 (\regs))
	REST_SP  (%sr2, PT_SR2 (\regs))
	REST_SP  (%sr3, PT_SR3 (\regs))
	REST_SP  (%sr4, PT_SR4 (\regs))
	REST_SP  (%sr5, PT_SR5 (\regs))
	REST_SP  (%sr6, PT_SR6 (\regs))
	REST_SP  (%sr7, PT_SR7 (\regs))

	REST_CR	(%cr17, PT_IASQ0(\regs))
	REST_CR	(%cr17, PT_IASQ1(\regs))

	REST_CR	(%cr18, PT_IAOQ0(\regs))
	REST_CR	(%cr18, PT_IAOQ1(\regs))

	REST_CR (%cr11, PT_SAR	(\regs))

	REST_CR	(%cr22, PT_PSW	(\regs))
	.endm


	/* First step to create a "relied upon translation"
	 * See PA 2.0 Arch. page F-4 and F-5.
	 *
	 * The ssm was originally necessary due to a "PCxT bug".
	 * But someone decided it needed to be added to the architecture
	 * and this "feature" went into rev3 of PA-RISC 1.1 Arch Manual.
	 * It's been carried forward into PA 2.0 Arch as well. :^(
	 *
	 * "ssm 0,%r0" is a NOP with side effects (prefetch barrier).
	 * rsm/ssm prevents the ifetch unit from speculatively fetching
	 * instructions past this line in the code stream.
	 * PA 2.0 processor will single step all insn in the same QUAD (4 insn).
	 */
	.macro	pcxt_ssm_bug
	rsm	PSW_SM_I,%r0
	nop	/* 1 */
	nop	/* 2 */
	nop	/* 3 */
	nop	/* 4 */
	nop	/* 5 */
	nop	/* 6 */
	nop	/* 7 */
	.endm

	/* Switch to virtual mapping, trashing only %r1 */
	.macro  virt_map
	/* pcxt_ssm_bug */
	rsm	PSW_SM_I, %r0		/* barrier for "Relied upon Translation */
	mtsp	%r0, %sr4
	mtsp	%r0, %sr5
	mtsp	%r0, %sr6
	tovirt_r1 %r29
	load32	KERNEL_PSW, %r1

	rsm     PSW_SM_QUIET,%r0	/* second "heavy weight" ctl op */
	mtctl	%r0, %cr17		/* Clear IIASQ tail */
	mtctl	%r0, %cr17		/* Clear IIASQ head */
	mtctl	%r1, %ipsw
	load32	4f, %r1
	mtctl	%r1, %cr18		/* Set IIAOQ tail */
	ldo	4(%r1), %r1
	mtctl	%r1, %cr18		/* Set IIAOQ head */
	rfir
	nop
4:
	.endm


	/*
	 * ASM_EXCEPTIONTABLE_ENTRY
	 *
	 * Creates an exception table entry.
	 * Do not convert to a assembler macro. This won't work.
	 */
#define ASM_EXCEPTIONTABLE_ENTRY(fault_addr, except_addr)	\
	.section __ex_table,"aw"			!	\
	.word (fault_addr - .), (except_addr - .)	!	\
	.previous


#endif /* __ASSEMBLY__ */
#endif
