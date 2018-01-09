#ifndef _ASM_POWERPC_EXCEPTION_H
#define _ASM_POWERPC_EXCEPTION_H
/*
 * Extracted from head_64.S
 *
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Rewritten by Cort Dougan (cort@cs.nmt.edu) for PReP
 *    Copyright (C) 1996 Cort Dougan <cort@cs.nmt.edu>
 *  Adapted for Power Macintosh by Paul Mackerras.
 *  Low-level exception handlers and MMU support
 *  rewritten by Paul Mackerras.
 *    Copyright (C) 1996 Paul Mackerras.
 *
 *  Adapted for 64bit PowerPC by Dave Engebretsen, Peter Bergner, and
 *    Mike Corrigan {engebret|bergner|mikejc}@us.ibm.com
 *
 *  This file contains the low-level support and setup for the
 *  PowerPC-64 platform, including trap and interrupt dispatch.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
/*
 * The following macros define the code that appears as
 * the prologue to each of the exception handlers.  They
 * are split into two parts to allow a single kernel binary
 * to be used for pSeries and iSeries.
 *
 * We make as much of the exception code common between native
 * exception handlers (including pSeries LPAR) and iSeries LPAR
 * implementations as possible.
 */

#define EX_R9		0
#define EX_R10		8
#define EX_R11		16
#define EX_R12		24
#define EX_R13		32
#define EX_SRR0		40
#define EX_DAR		48
#define EX_DSISR	56
#define EX_CCR		60
#define EX_R3		64
#define EX_LR		72
#define EX_CFAR		80
#define EX_PPR		88	/* SMT thread status register (priority) */
#define EX_CTR		96

/* Macros for annotating the expected destination of (h)rfid */

#define RFI_TO_KERNEL							\
	rfid

#define RFI_TO_USER							\
	rfid

#define RFI_TO_USER_OR_KERNEL						\
	rfid

#define RFI_TO_GUEST							\
	rfid

#define HRFI_TO_KERNEL							\
	hrfid

#define HRFI_TO_USER							\
	hrfid

#define HRFI_TO_USER_OR_KERNEL						\
	hrfid

#define HRFI_TO_GUEST							\
	hrfid

#define HRFI_TO_UNKNOWN							\
	hrfid

#ifdef CONFIG_RELOCATABLE
#define __EXCEPTION_RELON_PROLOG_PSERIES_1(label, h)			\
	ld	r12,PACAKBASE(r13);	/* get high part of &label */	\
	mfspr	r11,SPRN_##h##SRR0;	/* save SRR0 */			\
	LOAD_HANDLER(r12,label);					\
	mtctr	r12;							\
	mfspr	r12,SPRN_##h##SRR1;	/* and SRR1 */			\
	li	r10,MSR_RI;						\
	mtmsrd 	r10,1;			/* Set RI (EE=0) */		\
	bctr;
#else
/* If not relocatable, we can jump directly -- and save messing with LR */
#define __EXCEPTION_RELON_PROLOG_PSERIES_1(label, h)			\
	mfspr	r11,SPRN_##h##SRR0;	/* save SRR0 */			\
	mfspr	r12,SPRN_##h##SRR1;	/* and SRR1 */			\
	li	r10,MSR_RI;						\
	mtmsrd 	r10,1;			/* Set RI (EE=0) */		\
	b	label;
#endif
#define EXCEPTION_RELON_PROLOG_PSERIES_1(label, h)			\
	__EXCEPTION_RELON_PROLOG_PSERIES_1(label, h)			\

/*
 * As EXCEPTION_PROLOG_PSERIES(), except we've already got relocation on
 * so no need to rfid.  Save lr in case we're CONFIG_RELOCATABLE, in which
 * case EXCEPTION_RELON_PROLOG_PSERIES_1 will be using lr.
 */
#define EXCEPTION_RELON_PROLOG_PSERIES(area, label, h, extra, vec)	\
	EXCEPTION_PROLOG_0(area);					\
	EXCEPTION_PROLOG_1(area, extra, vec);				\
	EXCEPTION_RELON_PROLOG_PSERIES_1(label, h)

/*
 * We're short on space and time in the exception prolog, so we can't
 * use the normal SET_REG_IMMEDIATE macro. Normally we just need the
 * low halfword of the address, but for Kdump we need the whole low
 * word.
 */
#define LOAD_HANDLER(reg, label)					\
	/* Handlers must be within 64K of kbase, which must be 64k aligned */ \
	ori	reg,reg,(label)-_stext;	/* virt addr of handler ... */

/* Exception register prefixes */
#define EXC_HV	H
#define EXC_STD

#if defined(CONFIG_RELOCATABLE)
/*
 * If we support interrupts with relocation on AND we're a relocatable kernel,
 * we need to use CTR to get to the 2nd level handler.  So, save/restore it
 * when required.
 */
#define SAVE_CTR(reg, area)	mfctr	reg ; 	std	reg,area+EX_CTR(r13)
#define GET_CTR(reg, area) 			ld	reg,area+EX_CTR(r13)
#define RESTORE_CTR(reg, area)	ld	reg,area+EX_CTR(r13) ; mtctr reg
#else
/* ...else CTR is unused and in register. */
#define SAVE_CTR(reg, area)
#define GET_CTR(reg, area) 	mfctr	reg
#define RESTORE_CTR(reg, area)
#endif

/*
 * PPR save/restore macros used in exceptions_64s.S  
 * Used for P7 or later processors
 */
#define SAVE_PPR(area, ra, rb)						\
BEGIN_FTR_SECTION_NESTED(940)						\
	ld	ra,PACACURRENT(r13);					\
	ld	rb,area+EX_PPR(r13);	/* Read PPR from paca */	\
	std	rb,TASKTHREADPPR(ra);					\
END_FTR_SECTION_NESTED(CPU_FTR_HAS_PPR,CPU_FTR_HAS_PPR,940)

#define RESTORE_PPR_PACA(area, ra)					\
BEGIN_FTR_SECTION_NESTED(941)						\
	ld	ra,area+EX_PPR(r13);					\
	mtspr	SPRN_PPR,ra;						\
END_FTR_SECTION_NESTED(CPU_FTR_HAS_PPR,CPU_FTR_HAS_PPR,941)

/*
 * Increase the priority on systems where PPR save/restore is not
 * implemented/ supported.
 */
#define HMT_MEDIUM_PPR_DISCARD						\
BEGIN_FTR_SECTION_NESTED(942)						\
	HMT_MEDIUM;							\
END_FTR_SECTION_NESTED(CPU_FTR_HAS_PPR,0,942)  /*non P7*/		

/*
 * Get an SPR into a register if the CPU has the given feature
 */
#define OPT_GET_SPR(ra, spr, ftr)					\
BEGIN_FTR_SECTION_NESTED(943)						\
	mfspr	ra,spr;							\
END_FTR_SECTION_NESTED(ftr,ftr,943)

/*
 * Set an SPR from a register if the CPU has the given feature
 */
#define OPT_SET_SPR(ra, spr, ftr)					\
BEGIN_FTR_SECTION_NESTED(943)						\
	mtspr	spr,ra;							\
END_FTR_SECTION_NESTED(ftr,ftr,943)

/*
 * Save a register to the PACA if the CPU has the given feature
 */
#define OPT_SAVE_REG_TO_PACA(offset, ra, ftr)				\
BEGIN_FTR_SECTION_NESTED(943)						\
	std	ra,offset(r13);						\
END_FTR_SECTION_NESTED(ftr,ftr,943)

#define EXCEPTION_PROLOG_0(area)					\
	GET_PACA(r13);							\
	std	r9,area+EX_R9(r13);	/* save r9 */			\
	OPT_GET_SPR(r9, SPRN_PPR, CPU_FTR_HAS_PPR);			\
	HMT_MEDIUM;							\
	std	r10,area+EX_R10(r13);	/* save r10 - r12 */		\
	OPT_GET_SPR(r10, SPRN_CFAR, CPU_FTR_CFAR)

#define __EXCEPTION_PROLOG_1(area, extra, vec)				\
	OPT_SAVE_REG_TO_PACA(area+EX_PPR, r9, CPU_FTR_HAS_PPR);		\
	OPT_SAVE_REG_TO_PACA(area+EX_CFAR, r10, CPU_FTR_CFAR);		\
	SAVE_CTR(r10, area);						\
	mfcr	r9;							\
	extra(vec);							\
	std	r11,area+EX_R11(r13);					\
	std	r12,area+EX_R12(r13);					\
	GET_SCRATCH0(r10);						\
	std	r10,area+EX_R13(r13)
#define EXCEPTION_PROLOG_1(area, extra, vec)				\
	__EXCEPTION_PROLOG_1(area, extra, vec)

#define __EXCEPTION_PROLOG_PSERIES_1(label, h)				\
	ld	r12,PACAKBASE(r13);	/* get high part of &label */	\
	ld	r10,PACAKMSR(r13);	/* get MSR value for kernel */	\
	mfspr	r11,SPRN_##h##SRR0;	/* save SRR0 */			\
	LOAD_HANDLER(r12,label)						\
	mtspr	SPRN_##h##SRR0,r12;					\
	mfspr	r12,SPRN_##h##SRR1;	/* and SRR1 */			\
	mtspr	SPRN_##h##SRR1,r10;					\
	h##RFI_TO_KERNEL;						\
	b	.	/* prevent speculative execution */
#define EXCEPTION_PROLOG_PSERIES_1(label, h)				\
	__EXCEPTION_PROLOG_PSERIES_1(label, h)

#define EXCEPTION_PROLOG_PSERIES(area, label, h, extra, vec)		\
	EXCEPTION_PROLOG_0(area);					\
	EXCEPTION_PROLOG_1(area, extra, vec);				\
	EXCEPTION_PROLOG_PSERIES_1(label, h);

#define __KVMTEST(n)							\
	lbz	r10,HSTATE_IN_GUEST(r13);			\
	cmpwi	r10,0;							\
	bne	do_kvm_##n

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
/*
 * If hv is possible, interrupts come into to the hv version
 * of the kvmppc_interrupt code, which then jumps to the PR handler,
 * kvmppc_interrupt_pr, if the guest is a PR guest.
 */
#define kvmppc_interrupt kvmppc_interrupt_hv
#else
#define kvmppc_interrupt kvmppc_interrupt_pr
#endif

#define __KVM_HANDLER(area, h, n)					\
do_kvm_##n:								\
	BEGIN_FTR_SECTION_NESTED(947)					\
	ld	r10,area+EX_CFAR(r13);					\
	std	r10,HSTATE_CFAR(r13);					\
	END_FTR_SECTION_NESTED(CPU_FTR_CFAR,CPU_FTR_CFAR,947);		\
	BEGIN_FTR_SECTION_NESTED(948)					\
	ld	r10,area+EX_PPR(r13);					\
	std	r10,HSTATE_PPR(r13);					\
	END_FTR_SECTION_NESTED(CPU_FTR_HAS_PPR,CPU_FTR_HAS_PPR,948);	\
	ld	r10,area+EX_R10(r13);					\
	stw	r9,HSTATE_SCRATCH1(r13);				\
	ld	r9,area+EX_R9(r13);					\
	std	r12,HSTATE_SCRATCH0(r13);				\
	li	r12,n;							\
	b	kvmppc_interrupt

#define __KVM_HANDLER_SKIP(area, h, n)					\
do_kvm_##n:								\
	cmpwi	r10,KVM_GUEST_MODE_SKIP;				\
	ld	r10,area+EX_R10(r13);					\
	beq	89f;							\
	stw	r9,HSTATE_SCRATCH1(r13);			\
	BEGIN_FTR_SECTION_NESTED(948)					\
	ld	r9,area+EX_PPR(r13);					\
	std	r9,HSTATE_PPR(r13);					\
	END_FTR_SECTION_NESTED(CPU_FTR_HAS_PPR,CPU_FTR_HAS_PPR,948);	\
	ld	r9,area+EX_R9(r13);					\
	std	r12,HSTATE_SCRATCH0(r13);			\
	li	r12,n;							\
	b	kvmppc_interrupt;					\
89:	mtocrf	0x80,r9;						\
	ld	r9,area+EX_R9(r13);					\
	b	kvmppc_skip_##h##interrupt

#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
#define KVMTEST(n)			__KVMTEST(n)
#define KVM_HANDLER(area, h, n)		__KVM_HANDLER(area, h, n)
#define KVM_HANDLER_SKIP(area, h, n)	__KVM_HANDLER_SKIP(area, h, n)

#else
#define KVMTEST(n)
#define KVM_HANDLER(area, h, n)
#define KVM_HANDLER_SKIP(area, h, n)
#endif

#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
#define KVMTEST_PR(n)			__KVMTEST(n)
#define KVM_HANDLER_PR(area, h, n)	__KVM_HANDLER(area, h, n)
#define KVM_HANDLER_PR_SKIP(area, h, n)	__KVM_HANDLER_SKIP(area, h, n)

#else
#define KVMTEST_PR(n)
#define KVM_HANDLER_PR(area, h, n)
#define KVM_HANDLER_PR_SKIP(area, h, n)
#endif

#define NOTEST(n)

/*
 * The common exception prolog is used for all except a few exceptions
 * such as a segment miss on a kernel address.  We have to be prepared
 * to take another exception from the point where we first touch the
 * kernel stack onwards.
 *
 * On entry r13 points to the paca, r9-r13 are saved in the paca,
 * r9 contains the saved CR, r11 and r12 contain the saved SRR0 and
 * SRR1, and relocation is on.
 */
#define EXCEPTION_PROLOG_COMMON(n, area)				   \
	andi.	r10,r12,MSR_PR;		/* See if coming from user	*/ \
	mr	r10,r1;			/* Save r1			*/ \
	subi	r1,r1,INT_FRAME_SIZE;	/* alloc frame on kernel stack	*/ \
	beq-	1f;							   \
	ld	r1,PACAKSAVE(r13);	/* kernel stack to use		*/ \
1:	cmpdi	cr1,r1,-INT_FRAME_SIZE;	/* check if r1 is in userspace	*/ \
	blt+	cr1,3f;			/* abort if it is		*/ \
	li	r1,(n);			/* will be reloaded later	*/ \
	sth	r1,PACA_TRAP_SAVE(r13);					   \
	std	r3,area+EX_R3(r13);					   \
	addi	r3,r13,area;		/* r3 -> where regs are saved*/	   \
	RESTORE_CTR(r1, area);						   \
	b	bad_stack;						   \
3:	std	r9,_CCR(r1);		/* save CR in stackframe	*/ \
	std	r11,_NIP(r1);		/* save SRR0 in stackframe	*/ \
	std	r12,_MSR(r1);		/* save SRR1 in stackframe	*/ \
	std	r10,0(r1);		/* make stack chain pointer	*/ \
	std	r0,GPR0(r1);		/* save r0 in stackframe	*/ \
	std	r10,GPR1(r1);		/* save r1 in stackframe	*/ \
	beq	4f;			/* if from kernel mode		*/ \
	ACCOUNT_CPU_USER_ENTRY(r9, r10);				   \
	SAVE_PPR(area, r9, r10);					   \
4:	EXCEPTION_PROLOG_COMMON_2(area)					   \
	EXCEPTION_PROLOG_COMMON_3(n)					   \
	ACCOUNT_STOLEN_TIME

/* Save original regs values from save area to stack frame. */
#define EXCEPTION_PROLOG_COMMON_2(area)					   \
	ld	r9,area+EX_R9(r13);	/* move r9, r10 to stackframe	*/ \
	ld	r10,area+EX_R10(r13);					   \
	std	r9,GPR9(r1);						   \
	std	r10,GPR10(r1);						   \
	ld	r9,area+EX_R11(r13);	/* move r11 - r13 to stackframe	*/ \
	ld	r10,area+EX_R12(r13);					   \
	ld	r11,area+EX_R13(r13);					   \
	std	r9,GPR11(r1);						   \
	std	r10,GPR12(r1);						   \
	std	r11,GPR13(r1);						   \
	BEGIN_FTR_SECTION_NESTED(66);					   \
	ld	r10,area+EX_CFAR(r13);					   \
	std	r10,ORIG_GPR3(r1);					   \
	END_FTR_SECTION_NESTED(CPU_FTR_CFAR, CPU_FTR_CFAR, 66);		   \
	GET_CTR(r10, area);						   \
	std	r10,_CTR(r1);

#define EXCEPTION_PROLOG_COMMON_3(n)					   \
	std	r2,GPR2(r1);		/* save r2 in stackframe	*/ \
	SAVE_4GPRS(3, r1);		/* save r3 - r6 in stackframe   */ \
	SAVE_2GPRS(7, r1);		/* save r7, r8 in stackframe	*/ \
	mflr	r9;			/* Get LR, later save to stack	*/ \
	ld	r2,PACATOC(r13);	/* get kernel TOC into r2	*/ \
	std	r9,_LINK(r1);						   \
	lbz	r10,PACASOFTIRQEN(r13);				   \
	mfspr	r11,SPRN_XER;		/* save XER in stackframe	*/ \
	std	r10,SOFTE(r1);						   \
	std	r11,_XER(r1);						   \
	li	r9,(n)+1;						   \
	std	r9,_TRAP(r1);		/* set trap number		*/ \
	li	r10,0;							   \
	ld	r11,exception_marker@toc(r2);				   \
	std	r10,RESULT(r1);		/* clear regs->result		*/ \
	std	r11,STACK_FRAME_OVERHEAD-16(r1); /* mark the frame	*/

/*
 * Exception vectors.
 */
#define STD_EXCEPTION_PSERIES(loc, vec, label)		\
	. = loc;					\
	.globl label##_pSeries;				\
label##_pSeries:					\
	HMT_MEDIUM_PPR_DISCARD;				\
	SET_SCRATCH0(r13);		/* save r13 */		\
	EXCEPTION_PROLOG_PSERIES(PACA_EXGEN, label##_common,	\
				 EXC_STD, KVMTEST_PR, vec)

/* Version of above for when we have to branch out-of-line */
#define STD_EXCEPTION_PSERIES_OOL(vec, label)			\
	.globl label##_pSeries;					\
label##_pSeries:						\
	EXCEPTION_PROLOG_1(PACA_EXGEN, KVMTEST_PR, vec);	\
	EXCEPTION_PROLOG_PSERIES_1(label##_common, EXC_STD)

#define STD_EXCEPTION_HV(loc, vec, label)		\
	. = loc;					\
	.globl label##_hv;				\
label##_hv:						\
	HMT_MEDIUM_PPR_DISCARD;				\
	SET_SCRATCH0(r13);	/* save r13 */			\
	EXCEPTION_PROLOG_PSERIES(PACA_EXGEN, label##_common,	\
				 EXC_HV, KVMTEST, vec)

/* Version of above for when we have to branch out-of-line */
#define STD_EXCEPTION_HV_OOL(vec, label)		\
	.globl label##_hv;				\
label##_hv:						\
	EXCEPTION_PROLOG_1(PACA_EXGEN, KVMTEST, vec);	\
	EXCEPTION_PROLOG_PSERIES_1(label##_common, EXC_HV)

#define STD_RELON_EXCEPTION_PSERIES(loc, vec, label)	\
	. = loc;					\
	.globl label##_relon_pSeries;			\
label##_relon_pSeries:					\
	HMT_MEDIUM_PPR_DISCARD;				\
	/* No guest interrupts come through here */	\
	SET_SCRATCH0(r13);		/* save r13 */	\
	EXCEPTION_RELON_PROLOG_PSERIES(PACA_EXGEN, label##_common, \
				       EXC_STD, NOTEST, vec)

#define STD_RELON_EXCEPTION_PSERIES_OOL(vec, label)		\
	.globl label##_relon_pSeries;				\
label##_relon_pSeries:						\
	EXCEPTION_PROLOG_1(PACA_EXGEN, NOTEST, vec);		\
	EXCEPTION_RELON_PROLOG_PSERIES_1(label##_common, EXC_STD)

#define STD_RELON_EXCEPTION_HV(loc, vec, label)		\
	. = loc;					\
	.globl label##_relon_hv;			\
label##_relon_hv:					\
	HMT_MEDIUM_PPR_DISCARD;				\
	/* No guest interrupts come through here */	\
	SET_SCRATCH0(r13);	/* save r13 */		\
	EXCEPTION_RELON_PROLOG_PSERIES(PACA_EXGEN, label##_common, \
				       EXC_HV, NOTEST, vec)

#define STD_RELON_EXCEPTION_HV_OOL(vec, label)			\
	.globl label##_relon_hv;				\
label##_relon_hv:						\
	EXCEPTION_PROLOG_1(PACA_EXGEN, NOTEST, vec);		\
	EXCEPTION_RELON_PROLOG_PSERIES_1(label##_common, EXC_HV)

/* This associate vector numbers with bits in paca->irq_happened */
#define SOFTEN_VALUE_0x500	PACA_IRQ_EE
#define SOFTEN_VALUE_0x502	PACA_IRQ_EE
#define SOFTEN_VALUE_0x900	PACA_IRQ_DEC
#define SOFTEN_VALUE_0x982	PACA_IRQ_DEC
#define SOFTEN_VALUE_0xa00	PACA_IRQ_DBELL
#define SOFTEN_VALUE_0xe80	PACA_IRQ_DBELL
#define SOFTEN_VALUE_0xe82	PACA_IRQ_DBELL
#define SOFTEN_VALUE_0xe60	PACA_IRQ_HMI
#define SOFTEN_VALUE_0xe62	PACA_IRQ_HMI

#define __SOFTEN_TEST(h, vec)						\
	lbz	r10,PACASOFTIRQEN(r13);					\
	cmpwi	r10,0;							\
	li	r10,SOFTEN_VALUE_##vec;					\
	beq	masked_##h##interrupt
#define _SOFTEN_TEST(h, vec)	__SOFTEN_TEST(h, vec)

#define SOFTEN_TEST_PR(vec)						\
	KVMTEST_PR(vec);						\
	_SOFTEN_TEST(EXC_STD, vec)

#define SOFTEN_TEST_HV(vec)						\
	KVMTEST(vec);							\
	_SOFTEN_TEST(EXC_HV, vec)

#define SOFTEN_TEST_HV_201(vec)						\
	KVMTEST(vec);							\
	_SOFTEN_TEST(EXC_STD, vec)

#define SOFTEN_NOTEST_PR(vec)		_SOFTEN_TEST(EXC_STD, vec)
#define SOFTEN_NOTEST_HV(vec)		_SOFTEN_TEST(EXC_HV, vec)

#define __MASKABLE_EXCEPTION_PSERIES(vec, label, h, extra)		\
	SET_SCRATCH0(r13);    /* save r13 */				\
	EXCEPTION_PROLOG_0(PACA_EXGEN);					\
	__EXCEPTION_PROLOG_1(PACA_EXGEN, extra, vec);			\
	EXCEPTION_PROLOG_PSERIES_1(label##_common, h);

#define _MASKABLE_EXCEPTION_PSERIES(vec, label, h, extra)		\
	__MASKABLE_EXCEPTION_PSERIES(vec, label, h, extra)

#define MASKABLE_EXCEPTION_PSERIES(loc, vec, label)			\
	. = loc;							\
	.globl label##_pSeries;						\
label##_pSeries:							\
	HMT_MEDIUM_PPR_DISCARD;						\
	_MASKABLE_EXCEPTION_PSERIES(vec, label,				\
				    EXC_STD, SOFTEN_TEST_PR)

#define MASKABLE_EXCEPTION_HV(loc, vec, label)				\
	. = loc;							\
	.globl label##_hv;						\
label##_hv:								\
	_MASKABLE_EXCEPTION_PSERIES(vec, label,				\
				    EXC_HV, SOFTEN_TEST_HV)

#define MASKABLE_EXCEPTION_HV_OOL(vec, label)				\
	.globl label##_hv;						\
label##_hv:								\
	EXCEPTION_PROLOG_1(PACA_EXGEN, SOFTEN_TEST_HV, vec);		\
	EXCEPTION_PROLOG_PSERIES_1(label##_common, EXC_HV);

#define __MASKABLE_RELON_EXCEPTION_PSERIES(vec, label, h, extra)	\
	HMT_MEDIUM_PPR_DISCARD;						\
	SET_SCRATCH0(r13);    /* save r13 */				\
	EXCEPTION_PROLOG_0(PACA_EXGEN);					\
	__EXCEPTION_PROLOG_1(PACA_EXGEN, extra, vec);		\
	EXCEPTION_RELON_PROLOG_PSERIES_1(label##_common, h);
#define _MASKABLE_RELON_EXCEPTION_PSERIES(vec, label, h, extra)	\
	__MASKABLE_RELON_EXCEPTION_PSERIES(vec, label, h, extra)

#define MASKABLE_RELON_EXCEPTION_PSERIES(loc, vec, label)		\
	. = loc;							\
	.globl label##_relon_pSeries;					\
label##_relon_pSeries:							\
	_MASKABLE_RELON_EXCEPTION_PSERIES(vec, label,			\
					  EXC_STD, SOFTEN_NOTEST_PR)

#define MASKABLE_RELON_EXCEPTION_HV(loc, vec, label)			\
	. = loc;							\
	.globl label##_relon_hv;					\
label##_relon_hv:							\
	_MASKABLE_RELON_EXCEPTION_PSERIES(vec, label,			\
					  EXC_HV, SOFTEN_NOTEST_HV)

#define MASKABLE_RELON_EXCEPTION_HV_OOL(vec, label)			\
	.globl label##_relon_hv;					\
label##_relon_hv:							\
	EXCEPTION_PROLOG_1(PACA_EXGEN, SOFTEN_NOTEST_HV, vec);		\
	EXCEPTION_PROLOG_PSERIES_1(label##_common, EXC_HV);

/*
 * Our exception common code can be passed various "additions"
 * to specify the behaviour of interrupts, whether to kick the
 * runlatch, etc...
 */

/*
 * This addition reconciles our actual IRQ state with the various software
 * flags that track it. This may call C code.
 */
#define ADD_RECONCILE	RECONCILE_IRQ_STATE(r10,r11)

#define ADD_NVGPRS				\
	bl	save_nvgprs

#define RUNLATCH_ON				\
BEGIN_FTR_SECTION				\
	CURRENT_THREAD_INFO(r3, r1);		\
	ld	r4,TI_LOCAL_FLAGS(r3);		\
	andi.	r0,r4,_TLF_RUNLATCH;		\
	beql	ppc64_runlatch_on_trampoline;	\
END_FTR_SECTION_IFSET(CPU_FTR_CTRL)

#define EXCEPTION_COMMON(trap, label, hdlr, ret, additions)	\
	.align	7;						\
	.globl label##_common;					\
label##_common:							\
	EXCEPTION_PROLOG_COMMON(trap, PACA_EXGEN);		\
	/* Volatile regs are potentially clobbered here */	\
	additions;						\
	addi	r3,r1,STACK_FRAME_OVERHEAD;			\
	bl	hdlr;						\
	b	ret

#define STD_EXCEPTION_COMMON(trap, label, hdlr)			\
	EXCEPTION_COMMON(trap, label, hdlr, ret_from_except,	\
			 ADD_NVGPRS;ADD_RECONCILE)

/*
 * Like STD_EXCEPTION_COMMON, but for exceptions that can occur
 * in the idle task and therefore need the special idle handling
 * (finish nap and runlatch)
 */
#define STD_EXCEPTION_COMMON_ASYNC(trap, label, hdlr)		  \
	EXCEPTION_COMMON(trap, label, hdlr, ret_from_except_lite, \
			 FINISH_NAP;ADD_RECONCILE;RUNLATCH_ON)

/*
 * When the idle code in power4_idle puts the CPU into NAP mode,
 * it has to do so in a loop, and relies on the external interrupt
 * and decrementer interrupt entry code to get it out of the loop.
 * It sets the _TLF_NAPPING bit in current_thread_info()->local_flags
 * to signal that it is in the loop and needs help to get out.
 */
#ifdef CONFIG_PPC_970_NAP
#define FINISH_NAP				\
BEGIN_FTR_SECTION				\
	CURRENT_THREAD_INFO(r11, r1);		\
	ld	r9,TI_LOCAL_FLAGS(r11);		\
	andi.	r10,r9,_TLF_NAPPING;		\
	bnel	power4_fixup_nap;		\
END_FTR_SECTION_IFSET(CPU_FTR_CAN_NAP)
#else
#define FINISH_NAP
#endif

#endif	/* _ASM_POWERPC_EXCEPTION_H */
