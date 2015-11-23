/*
 *  Definitions for use by exception code on Book3-E
 *
 *  Copyright (C) 2008 Ben. Herrenschmidt (benh@kernel.crashing.org), IBM Corp.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_POWERPC_EXCEPTION_64E_H
#define _ASM_POWERPC_EXCEPTION_64E_H

/*
 * SPRGs usage an other considerations...
 *
 * Since TLB miss and other standard exceptions can be interrupted by
 * critical exceptions which can themselves be interrupted by machine
 * checks, and since the two later can themselves cause a TLB miss when
 * hitting the linear mapping for the kernel stacks, we need to be a bit
 * creative on how we use SPRGs.
 *
 * The base idea is that we have one SRPG reserved for critical and one
 * for machine check interrupts. Those are used to save a GPR that can
 * then be used to get the PACA, and store as much context as we need
 * to save in there. That includes saving the SPRGs used by the TLB miss
 * handler for linear mapping misses and the associated SRR0/1 due to
 * the above re-entrancy issue.
 *
 * So here's the current usage pattern. It's done regardless of which
 * SPRGs are user-readable though, thus we might have to change some of
 * this later. In order to do that more easily, we use special constants
 * for naming them
 *
 * WARNING: Some of these SPRGs are user readable. We need to do something
 * about it as some point by making sure they can't be used to leak kernel
 * critical data
 */

#define PACA_EXGDBELL PACA_EXGEN

/* We are out of SPRGs so we save some things in the PACA. The normal
 * exception frame is smaller than the CRIT or MC one though
 */
#define EX_R1		(0 * 8)
#define EX_CR		(1 * 8)
#define EX_R10		(2 * 8)
#define EX_R11		(3 * 8)
#define EX_R14		(4 * 8)
#define EX_R15		(5 * 8)

/*
 * The TLB miss exception uses different slots.
 *
 * The bolted variant uses only the first six fields,
 * which in combination with pgd and kernel_pgd fits in
 * one 64-byte cache line.
 */

#define EX_TLB_R10	( 0 * 8)
#define EX_TLB_R11	( 1 * 8)
#define EX_TLB_R14	( 2 * 8)
#define EX_TLB_R15	( 3 * 8)
#define EX_TLB_R16	( 4 * 8)
#define EX_TLB_CR	( 5 * 8)
#define EX_TLB_R12	( 6 * 8)
#define EX_TLB_R13	( 7 * 8)
#define EX_TLB_DEAR	( 8 * 8) /* Level 0 and 2 only */
#define EX_TLB_ESR	( 9 * 8) /* Level 0 and 2 only */
#define EX_TLB_SRR0	(10 * 8)
#define EX_TLB_SRR1	(11 * 8)
#define EX_TLB_R7	(12 * 8)
#ifdef CONFIG_BOOK3E_MMU_TLB_STATS
#define EX_TLB_R8	(13 * 8)
#define EX_TLB_R9	(14 * 8)
#define EX_TLB_LR	(15 * 8)
#define EX_TLB_SIZE	(16 * 8)
#else
#define EX_TLB_SIZE	(13 * 8)
#endif

#define	START_EXCEPTION(label)						\
	.globl exc_##label##_book3e;					\
exc_##label##_book3e:

/* TLB miss exception prolog
 *
 * This prolog handles re-entrancy (up to 3 levels supported in the PACA
 * though we currently don't test for overflow). It provides you with a
 * re-entrancy safe working space of r10...r16 and CR with r12 being used
 * as the exception area pointer in the PACA for that level of re-entrancy
 * and r13 containing the PACA pointer.
 *
 * SRR0 and SRR1 are saved, but DEAR and ESR are not, since they don't apply
 * as-is for instruction exceptions. It's up to the actual exception code
 * to save them as well if required.
 */
#define TLB_MISS_PROLOG							    \
	mtspr	SPRN_SPRG_TLB_SCRATCH,r12;				    \
	mfspr	r12,SPRN_SPRG_TLB_EXFRAME;				    \
	std	r10,EX_TLB_R10(r12);					    \
	mfcr	r10;							    \
	std	r11,EX_TLB_R11(r12);					    \
	mfspr	r11,SPRN_SPRG_TLB_SCRATCH;				    \
	std	r13,EX_TLB_R13(r12);					    \
	mfspr	r13,SPRN_SPRG_PACA;					    \
	std	r14,EX_TLB_R14(r12);					    \
	addi	r14,r12,EX_TLB_SIZE;					    \
	std	r15,EX_TLB_R15(r12);					    \
	mfspr	r15,SPRN_SRR1;						    \
	std	r16,EX_TLB_R16(r12);					    \
	mfspr	r16,SPRN_SRR0;						    \
	std	r10,EX_TLB_CR(r12);					    \
	std	r11,EX_TLB_R12(r12);					    \
	mtspr	SPRN_SPRG_TLB_EXFRAME,r14;				    \
	std	r15,EX_TLB_SRR1(r12);					    \
	std	r16,EX_TLB_SRR0(r12);					    \
	TLB_MISS_PROLOG_STATS

/* And these are the matching epilogs that restores things
 *
 * There are 3 epilogs:
 *
 * - SUCCESS       : Unwinds one level
 * - ERROR         : restore from level 0 and reset
 * - ERROR_SPECIAL : restore from current level and reset
 *
 * Normal errors use ERROR, that is, they restore the initial fault context
 * and trigger a fault. However, there is a special case for linear mapping
 * errors. Those should basically never happen, but if they do happen, we
 * want the error to point out the context that did that linear mapping
 * fault, not the initial level 0 (basically, we got a bogus PGF or something
 * like that). For userland errors on the linear mapping, there is no
 * difference since those are always level 0 anyway
 */

#define TLB_MISS_RESTORE(freg)						    \
	ld	r14,EX_TLB_CR(r12);					    \
	ld	r10,EX_TLB_R10(r12);					    \
	ld	r15,EX_TLB_SRR0(r12);					    \
	ld	r16,EX_TLB_SRR1(r12);					    \
	mtspr	SPRN_SPRG_TLB_EXFRAME,freg;				    \
	ld	r11,EX_TLB_R11(r12);					    \
	mtcr	r14;							    \
	ld	r13,EX_TLB_R13(r12);					    \
	ld	r14,EX_TLB_R14(r12);					    \
	mtspr	SPRN_SRR0,r15;						    \
	ld	r15,EX_TLB_R15(r12);					    \
	mtspr	SPRN_SRR1,r16;						    \
	TLB_MISS_RESTORE_STATS						    \
	ld	r16,EX_TLB_R16(r12);					    \
	ld	r12,EX_TLB_R12(r12);					    \

#define TLB_MISS_EPILOG_SUCCESS						    \
	TLB_MISS_RESTORE(r12)

#define TLB_MISS_EPILOG_ERROR						    \
	addi	r12,r13,PACA_EXTLB;					    \
	TLB_MISS_RESTORE(r12)

#define TLB_MISS_EPILOG_ERROR_SPECIAL					    \
	addi	r11,r13,PACA_EXTLB;					    \
	TLB_MISS_RESTORE(r11)

#ifdef CONFIG_BOOK3E_MMU_TLB_STATS
#define TLB_MISS_PROLOG_STATS						    \
	mflr	r10;							    \
	std	r8,EX_TLB_R8(r12);					    \
	std	r9,EX_TLB_R9(r12);					    \
	std	r10,EX_TLB_LR(r12);
#define TLB_MISS_RESTORE_STATS					            \
	ld	r16,EX_TLB_LR(r12);					    \
	ld	r9,EX_TLB_R9(r12);					    \
	ld	r8,EX_TLB_R8(r12);					    \
	mtlr	r16;
#define TLB_MISS_STATS_D(name)						    \
	addi	r9,r13,MMSTAT_DSTATS+name;				    \
	bl	tlb_stat_inc;
#define TLB_MISS_STATS_I(name)						    \
	addi	r9,r13,MMSTAT_ISTATS+name;				    \
	bl	tlb_stat_inc;
#define TLB_MISS_STATS_X(name)						    \
	ld	r8,PACA_EXTLB+EX_TLB_ESR(r13);				    \
	cmpdi	cr2,r8,-1;						    \
	beq	cr2,61f;						    \
	addi	r9,r13,MMSTAT_DSTATS+name;				    \
	b	62f;							    \
61:	addi	r9,r13,MMSTAT_ISTATS+name;				    \
62:	bl	tlb_stat_inc;
#define TLB_MISS_STATS_SAVE_INFO					    \
	std	r14,EX_TLB_ESR(r12);	/* save ESR */
#define TLB_MISS_STATS_SAVE_INFO_BOLTED					    \
	std	r14,PACA_EXTLB+EX_TLB_ESR(r13);	/* save ESR */
#else
#define TLB_MISS_PROLOG_STATS
#define TLB_MISS_RESTORE_STATS
#define TLB_MISS_PROLOG_STATS_BOLTED
#define TLB_MISS_RESTORE_STATS_BOLTED
#define TLB_MISS_STATS_D(name)
#define TLB_MISS_STATS_I(name)
#define TLB_MISS_STATS_X(name)
#define TLB_MISS_STATS_Y(name)
#define TLB_MISS_STATS_SAVE_INFO
#define TLB_MISS_STATS_SAVE_INFO_BOLTED
#endif

#define SET_IVOR(vector_number, vector_offset)	\
	LOAD_REG_ADDR(r3,interrupt_base_book3e);\
	ori	r3,r3,vector_offset@l;		\
	mtspr	SPRN_IVOR##vector_number,r3;

#endif /* _ASM_POWERPC_EXCEPTION_64E_H */

