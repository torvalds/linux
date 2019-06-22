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
#include <asm/head-64.h>
#include <asm/feature-fixups.h>

/* PACA save area offsets (exgen, exmc, etc) */
#define EX_R9		0
#define EX_R10		8
#define EX_R11		16
#define EX_R12		24
#define EX_R13		32
#define EX_DAR		40
#define EX_DSISR	48
#define EX_CCR		52
#define EX_CFAR		56
#define EX_PPR		64
#if defined(CONFIG_RELOCATABLE)
#define EX_CTR		72
#define EX_SIZE		10	/* size in u64 units */
#else
#define EX_SIZE		9	/* size in u64 units */
#endif

/*
 * maximum recursive depth of MCE exceptions
 */
#define MAX_MCE_DEPTH	4

/*
 * EX_R3 is only used by the bad_stack handler. bad_stack reloads and
 * saves DAR from SPRN_DAR, and EX_DAR is not used. So EX_R3 can overlap
 * with EX_DAR.
 */
#define EX_R3		EX_DAR

#ifdef __ASSEMBLY__

#define STF_ENTRY_BARRIER_SLOT						\
	STF_ENTRY_BARRIER_FIXUP_SECTION;				\
	nop;								\
	nop;								\
	nop

#define STF_EXIT_BARRIER_SLOT						\
	STF_EXIT_BARRIER_FIXUP_SECTION;					\
	nop;								\
	nop;								\
	nop;								\
	nop;								\
	nop;								\
	nop

/*
 * r10 must be free to use, r13 must be paca
 */
#define INTERRUPT_TO_KERNEL						\
	STF_ENTRY_BARRIER_SLOT

/*
 * Macros for annotating the expected destination of (h)rfid
 *
 * The nop instructions allow us to insert one or more instructions to flush the
 * L1-D cache when returning to userspace or a guest.
 */
#define RFI_FLUSH_SLOT							\
	RFI_FLUSH_FIXUP_SECTION;					\
	nop;								\
	nop;								\
	nop

#define RFI_TO_KERNEL							\
	rfid

#define RFI_TO_USER							\
	STF_EXIT_BARRIER_SLOT;						\
	RFI_FLUSH_SLOT;							\
	rfid;								\
	b	rfi_flush_fallback

#define RFI_TO_USER_OR_KERNEL						\
	STF_EXIT_BARRIER_SLOT;						\
	RFI_FLUSH_SLOT;							\
	rfid;								\
	b	rfi_flush_fallback

#define RFI_TO_GUEST							\
	STF_EXIT_BARRIER_SLOT;						\
	RFI_FLUSH_SLOT;							\
	rfid;								\
	b	rfi_flush_fallback

#define HRFI_TO_KERNEL							\
	hrfid

#define HRFI_TO_USER							\
	STF_EXIT_BARRIER_SLOT;						\
	RFI_FLUSH_SLOT;							\
	hrfid;								\
	b	hrfi_flush_fallback

#define HRFI_TO_USER_OR_KERNEL						\
	STF_EXIT_BARRIER_SLOT;						\
	RFI_FLUSH_SLOT;							\
	hrfid;								\
	b	hrfi_flush_fallback

#define HRFI_TO_GUEST							\
	STF_EXIT_BARRIER_SLOT;						\
	RFI_FLUSH_SLOT;							\
	hrfid;								\
	b	hrfi_flush_fallback

#define HRFI_TO_UNKNOWN							\
	STF_EXIT_BARRIER_SLOT;						\
	RFI_FLUSH_SLOT;							\
	hrfid;								\
	b	hrfi_flush_fallback

/*
 * We're short on space and time in the exception prolog, so we can't
 * use the normal LOAD_REG_IMMEDIATE macro to load the address of label.
 * Instead we get the base of the kernel from paca->kernelbase and or in the low
 * part of label. This requires that the label be within 64KB of kernelbase, and
 * that kernelbase be 64K aligned.
 */
#define LOAD_HANDLER(reg, label)					\
	ld	reg,PACAKBASE(r13);	/* get high part of &label */	\
	ori	reg,reg,FIXED_SYMBOL_ABS_ADDR(label)

#define __LOAD_HANDLER(reg, label)					\
	ld	reg,PACAKBASE(r13);					\
	ori	reg,reg,(ABS_ADDR(label))@l

/*
 * Branches from unrelocated code (e.g., interrupts) to labels outside
 * head-y require >64K offsets.
 */
#define __LOAD_FAR_HANDLER(reg, label)					\
	ld	reg,PACAKBASE(r13);					\
	ori	reg,reg,(ABS_ADDR(label))@l;				\
	addis	reg,reg,(ABS_ADDR(label))@h

.macro EXCEPTION_PROLOG_2_REAL label, hsrr, set_ri
	ld	r10,PACAKMSR(r13)	/* get MSR value for kernel */
	.if ! \set_ri
	xori	r10,r10,MSR_RI		/* Clear MSR_RI */
	.endif
	.if \hsrr
	mfspr	r11,SPRN_HSRR0		/* save HSRR0 */
	.else
	mfspr	r11,SPRN_SRR0		/* save SRR0 */
	.endif
	LOAD_HANDLER(r12, \label\())
	.if \hsrr
	mtspr	SPRN_HSRR0,r12
	mfspr	r12,SPRN_HSRR1		/* and HSRR1 */
	mtspr	SPRN_HSRR1,r10
	HRFI_TO_KERNEL
	.else
	mtspr	SPRN_SRR0,r12
	mfspr	r12,SPRN_SRR1		/* and SRR1 */
	mtspr	SPRN_SRR1,r10
	RFI_TO_KERNEL
	.endif
	b	.	/* prevent speculative execution */
.endm

.macro EXCEPTION_PROLOG_2_VIRT label, hsrr
#ifdef CONFIG_RELOCATABLE
	.if \hsrr
	mfspr	r11,SPRN_HSRR0	/* save HSRR0 */
	.else
	mfspr	r11,SPRN_SRR0	/* save SRR0 */
	.endif
	LOAD_HANDLER(r12, \label\())
	mtctr	r12
	.if \hsrr
	mfspr	r12,SPRN_HSRR1	/* and HSRR1 */
	.else
	mfspr	r12,SPRN_SRR1	/* and HSRR1 */
	.endif
	li	r10,MSR_RI
	mtmsrd 	r10,1		/* Set RI (EE=0) */
	bctr
#else
	.if \hsrr
	mfspr	r11,SPRN_HSRR0		/* save HSRR0 */
	mfspr	r12,SPRN_HSRR1		/* and HSRR1 */
	.else
	mfspr	r11,SPRN_SRR0		/* save SRR0 */
	mfspr	r12,SPRN_SRR1		/* and SRR1 */
	.endif
	li	r10,MSR_RI
	mtmsrd 	r10,1			/* Set RI (EE=0) */
	b	\label
#endif
.endm

/*
 * As EXCEPTION_PROLOG(), except we've already got relocation on so no need to
 * rfid. Save CTR in case we're CONFIG_RELOCATABLE, in which case
 * EXCEPTION_PROLOG_2_VIRT will be using CTR.
 */
#define EXCEPTION_RELON_PROLOG(area, label, hsrr, kvm, vec)		\
	SET_SCRATCH0(r13);		/* save r13 */			\
	EXCEPTION_PROLOG_0 area ;					\
	EXCEPTION_PROLOG_1 hsrr, area, kvm, vec, 0 ;			\
	EXCEPTION_PROLOG_2_VIRT label, hsrr

/* Exception register prefixes */
#define EXC_HV		1
#define EXC_STD		0

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
#define SAVE_PPR(area, ra)						\
BEGIN_FTR_SECTION_NESTED(940)						\
	ld	ra,area+EX_PPR(r13);	/* Read PPR from paca */	\
	std	ra,_PPR(r1);						\
END_FTR_SECTION_NESTED(CPU_FTR_HAS_PPR,CPU_FTR_HAS_PPR,940)

#define RESTORE_PPR_PACA(area, ra)					\
BEGIN_FTR_SECTION_NESTED(941)						\
	ld	ra,area+EX_PPR(r13);					\
	mtspr	SPRN_PPR,ra;						\
END_FTR_SECTION_NESTED(CPU_FTR_HAS_PPR,CPU_FTR_HAS_PPR,941)

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

.macro EXCEPTION_PROLOG_0 area
	GET_PACA(r13)
	std	r9,\area\()+EX_R9(r13)		/* save r9 */
	OPT_GET_SPR(r9, SPRN_PPR, CPU_FTR_HAS_PPR)
	HMT_MEDIUM
	std	r10,\area\()+EX_R10(r13)	/* save r10 - r12 */
	OPT_GET_SPR(r10, SPRN_CFAR, CPU_FTR_CFAR)
.endm

.macro EXCEPTION_PROLOG_1 hsrr, area, kvm, vec, bitmask
	OPT_SAVE_REG_TO_PACA(\area\()+EX_PPR, r9, CPU_FTR_HAS_PPR)
	OPT_SAVE_REG_TO_PACA(\area\()+EX_CFAR, r10, CPU_FTR_CFAR)
	INTERRUPT_TO_KERNEL
	SAVE_CTR(r10, \area\())
	mfcr	r9
	.if \kvm
		KVMTEST \hsrr \vec
	.endif

	.if \bitmask
		lbz	r10,PACAIRQSOFTMASK(r13)
		andi.	r10,r10,\bitmask
		/* Associate vector numbers with bits in paca->irq_happened */
		.if \vec == 0x500 || \vec == 0xea0
		li	r10,PACA_IRQ_EE
		.elseif \vec == 0x900
		li	r10,PACA_IRQ_DEC
		.elseif \vec == 0xa00 || \vec == 0xe80
		li	r10,PACA_IRQ_DBELL
		.elseif \vec == 0xe60
		li	r10,PACA_IRQ_HMI
		.elseif \vec == 0xf00
		li	r10,PACA_IRQ_PMI
		.else
		.abort "Bad maskable vector"
		.endif

		.if \hsrr
		bne	masked_Hinterrupt
		.else
		bne	masked_interrupt
		.endif
	.endif

	std	r11,\area\()+EX_R11(r13)
	std	r12,\area\()+EX_R12(r13)
	GET_SCRATCH0(r10)
	std	r10,\area\()+EX_R13(r13)
.endm

#define EXCEPTION_PROLOG(area, label, hsrr, kvm, vec)			\
	SET_SCRATCH0(r13);		/* save r13 */			\
	EXCEPTION_PROLOG_0 area	;					\
	EXCEPTION_PROLOG_1 hsrr, area, kvm, vec, 0 ;			\
	EXCEPTION_PROLOG_2_REAL label, hsrr, 1

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

/*
 * Branch to label using its 0xC000 address. This results in instruction
 * address suitable for MSR[IR]=0 or 1, which allows relocation to be turned
 * on using mtmsr rather than rfid.
 *
 * This could set the 0xc bits for !RELOCATABLE as an immediate, rather than
 * load KBASE for a slight optimisation.
 */
#define BRANCH_TO_C000(reg, label)					\
	__LOAD_HANDLER(reg, label);					\
	mtctr	reg;							\
	bctr

#ifdef CONFIG_RELOCATABLE
#define BRANCH_TO_COMMON(reg, label)					\
	__LOAD_HANDLER(reg, label);					\
	mtctr	reg;							\
	bctr

#define BRANCH_LINK_TO_FAR(label)					\
	__LOAD_FAR_HANDLER(r12, label);					\
	mtctr	r12;							\
	bctrl

/*
 * KVM requires __LOAD_FAR_HANDLER.
 *
 * __BRANCH_TO_KVM_EXIT branches are also a special case because they
 * explicitly use r9 then reload it from PACA before branching. Hence
 * the double-underscore.
 */
#define __BRANCH_TO_KVM_EXIT(area, label)				\
	mfctr	r9;							\
	std	r9,HSTATE_SCRATCH1(r13);				\
	__LOAD_FAR_HANDLER(r9, label);					\
	mtctr	r9;							\
	ld	r9,area+EX_R9(r13);					\
	bctr

#else
#define BRANCH_TO_COMMON(reg, label)					\
	b	label

#define BRANCH_LINK_TO_FAR(label)					\
	bl	label

#define __BRANCH_TO_KVM_EXIT(area, label)				\
	ld	r9,area+EX_R9(r13);					\
	b	label

#endif

/* Do not enable RI */
#define EXCEPTION_PROLOG_NORI(area, label, hsrr, kvm, vec)		\
	EXCEPTION_PROLOG_0 area ;					\
	EXCEPTION_PROLOG_1 hsrr, area, kvm, vec, 0 ;			\
	EXCEPTION_PROLOG_2_REAL label, hsrr, 0

#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
.macro KVMTEST hsrr, n
	lbz	r10,HSTATE_IN_GUEST(r13)
	cmpwi	r10,0
	.if \hsrr
	bne	do_kvm_H\n
	.else
	bne	do_kvm_\n
	.endif
.endm

.macro KVM_HANDLER area, hsrr, n, skip
	.if \skip
	cmpwi	r10,KVM_GUEST_MODE_SKIP
	beq	89f
	.else
	BEGIN_FTR_SECTION_NESTED(947)
	ld	r10,\area+EX_CFAR(r13)
	std	r10,HSTATE_CFAR(r13)
	END_FTR_SECTION_NESTED(CPU_FTR_CFAR,CPU_FTR_CFAR,947)
	.endif

	BEGIN_FTR_SECTION_NESTED(948)
	ld	r10,\area+EX_PPR(r13)
	std	r10,HSTATE_PPR(r13)
	END_FTR_SECTION_NESTED(CPU_FTR_HAS_PPR,CPU_FTR_HAS_PPR,948)
	ld	r10,\area+EX_R10(r13)
	std	r12,HSTATE_SCRATCH0(r13)
	sldi	r12,r9,32
	/* HSRR variants have the 0x2 bit added to their trap number */
	.if \hsrr
	ori	r12,r12,(\n + 0x2)
	.else
	ori	r12,r12,(\n)
	.endif
	/* This reloads r9 before branching to kvmppc_interrupt */
	__BRANCH_TO_KVM_EXIT(\area, kvmppc_interrupt)

	.if \skip
89:	mtocrf	0x80,r9
	ld	r9,\area+EX_R9(r13)
	ld	r10,\area+EX_R10(r13)
	.if \hsrr
	b	kvmppc_skip_Hinterrupt
	.else
	b	kvmppc_skip_interrupt
	.endif
	.endif
.endm

#else
.macro KVMTEST hsrr, n
.endm
.macro KVM_HANDLER area, hsrr, n, skip
.endm
#endif

#define EXCEPTION_PROLOG_COMMON_1()					   \
	std	r9,_CCR(r1);		/* save CR in stackframe	*/ \
	std	r11,_NIP(r1);		/* save SRR0 in stackframe	*/ \
	std	r12,_MSR(r1);		/* save SRR1 in stackframe	*/ \
	std	r10,0(r1);		/* make stack chain pointer	*/ \
	std	r0,GPR0(r1);		/* save r0 in stackframe	*/ \
	std	r10,GPR1(r1);		/* save r1 in stackframe	*/ \


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
3:	EXCEPTION_PROLOG_COMMON_1();					   \
	kuap_save_amr_and_lock r9, r10, cr1, cr0;			   \
	beq	4f;			/* if from kernel mode		*/ \
	ACCOUNT_CPU_USER_ENTRY(r13, r9, r10);				   \
	SAVE_PPR(area, r9);						   \
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
	lbz	r10,PACAIRQSOFTMASK(r13);				   \
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
#define STD_EXCEPTION(vec, label)				\
	EXCEPTION_PROLOG(PACA_EXGEN, label, EXC_STD, 1, vec);

/* Version of above for when we have to branch out-of-line */
#define __OOL_EXCEPTION(vec, label, hdlr)			\
	SET_SCRATCH0(r13);					\
	EXCEPTION_PROLOG_0 PACA_EXGEN ;				\
	b hdlr

#define STD_EXCEPTION_OOL(vec, label)				\
	EXCEPTION_PROLOG_1 EXC_STD, PACA_EXGEN, 1, vec, 0 ;	\
	EXCEPTION_PROLOG_2_REAL label, EXC_STD, 1

#define STD_EXCEPTION_HV(loc, vec, label)			\
	EXCEPTION_PROLOG(PACA_EXGEN, label, EXC_HV, 1, vec)

#define STD_EXCEPTION_HV_OOL(vec, label)			\
	EXCEPTION_PROLOG_1 EXC_HV, PACA_EXGEN, 1, vec, 0 ;	\
	EXCEPTION_PROLOG_2_REAL label, EXC_HV, 1

#define STD_RELON_EXCEPTION(loc, vec, label)		\
	/* No guest interrupts come through here */	\
	EXCEPTION_RELON_PROLOG(PACA_EXGEN, label, EXC_STD, 0, vec)

#define STD_RELON_EXCEPTION_OOL(vec, label)			\
	EXCEPTION_PROLOG_1 EXC_STD, PACA_EXGEN, 0, vec, 0 ;	\
	EXCEPTION_PROLOG_2_VIRT label, EXC_STD

#define STD_RELON_EXCEPTION_HV(loc, vec, label)			\
	EXCEPTION_RELON_PROLOG(PACA_EXGEN, label, EXC_HV, 1, vec)

#define STD_RELON_EXCEPTION_HV_OOL(vec, label)			\
	EXCEPTION_PROLOG_1 EXC_HV, PACA_EXGEN, 1, vec, 0 ;	\
	EXCEPTION_PROLOG_2_VIRT label, EXC_HV

#define __MASKABLE_EXCEPTION(vec, label, hsrr, kvm, bitmask)		\
	SET_SCRATCH0(r13);    /* save r13 */				\
	EXCEPTION_PROLOG_0 PACA_EXGEN ;					\
	EXCEPTION_PROLOG_1 hsrr, PACA_EXGEN, kvm, vec, bitmask ;	\
	EXCEPTION_PROLOG_2_REAL label, hsrr, 1

#define MASKABLE_EXCEPTION(vec, label, bitmask)				\
	__MASKABLE_EXCEPTION(vec, label, EXC_STD, 1, bitmask)

#define MASKABLE_EXCEPTION_OOL(vec, label, bitmask)			\
	EXCEPTION_PROLOG_1 EXC_STD, PACA_EXGEN, 1, vec, bitmask ;	\
	EXCEPTION_PROLOG_2_REAL label, EXC_STD, 1

#define MASKABLE_EXCEPTION_HV(vec, label, bitmask)			\
	__MASKABLE_EXCEPTION(vec, label, EXC_HV, 1, bitmask)

#define MASKABLE_EXCEPTION_HV_OOL(vec, label, bitmask)			\
	EXCEPTION_PROLOG_1 EXC_HV, PACA_EXGEN, 1, vec, bitmask ;	\
	EXCEPTION_PROLOG_2_REAL label, EXC_HV, 1

#define __MASKABLE_RELON_EXCEPTION(vec, label, hsrr, kvm, bitmask)	\
	SET_SCRATCH0(r13);    /* save r13 */				\
	EXCEPTION_PROLOG_0 PACA_EXGEN ;					\
	EXCEPTION_PROLOG_1 hsrr, PACA_EXGEN, kvm, vec, bitmask ;	\
	EXCEPTION_PROLOG_2_VIRT label, hsrr

#define MASKABLE_RELON_EXCEPTION(vec, label, bitmask)			\
	__MASKABLE_RELON_EXCEPTION(vec, label, EXC_STD, 0, bitmask)

#define MASKABLE_RELON_EXCEPTION_OOL(vec, label, bitmask)		\
	EXCEPTION_PROLOG_1 EXC_STD, PACA_EXGEN, 0, vec, bitmask ;	\
	EXCEPTION_PROLOG_2_REAL label, EXC_STD, 1

#define MASKABLE_RELON_EXCEPTION_HV(vec, label, bitmask)		\
	__MASKABLE_RELON_EXCEPTION(vec, label, EXC_HV, 1, bitmask)

#define MASKABLE_RELON_EXCEPTION_HV_OOL(vec, label, bitmask)		\
	EXCEPTION_PROLOG_1 EXC_HV, PACA_EXGEN, 1, vec, bitmask ;	\
	EXCEPTION_PROLOG_2_VIRT label, EXC_HV

#define RUNLATCH_ON				\
BEGIN_FTR_SECTION				\
	ld	r3, PACA_THREAD_INFO(r13);	\
	ld	r4,TI_LOCAL_FLAGS(r3);		\
	andi.	r0,r4,_TLF_RUNLATCH;		\
	beql	ppc64_runlatch_on_trampoline;	\
END_FTR_SECTION_IFSET(CPU_FTR_CTRL)

#define EXCEPTION_COMMON(area, trap)				\
	EXCEPTION_PROLOG_COMMON(trap, area);			\

/*
 * Exception where stack is already set in r1, r1 is saved in r10
 */
#define EXCEPTION_COMMON_STACK(area, trap)			\
	EXCEPTION_PROLOG_COMMON_1();				\
	kuap_save_amr_and_lock r9, r10, cr1;			\
	EXCEPTION_PROLOG_COMMON_2(area);			\
	EXCEPTION_PROLOG_COMMON_3(trap)

#define STD_EXCEPTION_COMMON(trap, hdlr)			\
	EXCEPTION_COMMON(PACA_EXGEN, trap);			\
	bl	save_nvgprs;					\
	RECONCILE_IRQ_STATE(r10, r11);				\
	addi	r3,r1,STACK_FRAME_OVERHEAD;			\
	bl	hdlr;						\
	b	ret_from_except

/*
 * Like STD_EXCEPTION_COMMON, but for exceptions that can occur
 * in the idle task and therefore need the special idle handling
 * (finish nap and runlatch)
 */
#define STD_EXCEPTION_COMMON_ASYNC(trap, hdlr)			\
	EXCEPTION_COMMON(PACA_EXGEN, trap);			\
	FINISH_NAP;						\
	RECONCILE_IRQ_STATE(r10, r11);				\
	RUNLATCH_ON;						\
	addi	r3,r1,STACK_FRAME_OVERHEAD;			\
	bl	hdlr;						\
	b	ret_from_except_lite

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
	ld	r11, PACA_THREAD_INFO(r13);	\
	ld	r9,TI_LOCAL_FLAGS(r11);		\
	andi.	r10,r9,_TLF_NAPPING;		\
	bnel	power4_fixup_nap;		\
END_FTR_SECTION_IFSET(CPU_FTR_CAN_NAP)
#else
#define FINISH_NAP
#endif

#endif /* __ASSEMBLY__ */

#endif	/* _ASM_POWERPC_EXCEPTION_H */
