/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file contains the 64-bit "server" PowerPC variant
 * of the low level exception handling including exception
 * vectors, exception return, part of the slb and stab
 * handling and other fixed offset specific things.
 *
 * This file is meant to be #included from head_64.S due to
 * position dependent assembly.
 *
 * Most of this originates from head_64.S and thus has the same
 * copyright history.
 *
 */

#include <asm/hw_irq.h>
#include <asm/exception-64s.h>
#include <asm/ptrace.h>
#include <asm/cpuidle.h>
#include <asm/head-64.h>
#include <asm/feature-fixups.h>
#include <asm/kup.h>

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
.if EX_SIZE != 10
	.error "EX_SIZE is wrong"
.endif
#else
.if EX_SIZE != 9
	.error "EX_SIZE is wrong"
.endif
#endif

/*
 * Following are fixed section helper macros.
 *
 * EXC_REAL_BEGIN/END  - real, unrelocated exception vectors
 * EXC_VIRT_BEGIN/END  - virt (AIL), unrelocated exception vectors
 * TRAMP_REAL_BEGIN    - real, unrelocated helpers (virt may call these)
 * TRAMP_VIRT_BEGIN    - virt, unreloc helpers (in practice, real can use)
 * TRAMP_KVM_BEGIN     - KVM handlers, these are put into real, unrelocated
 * EXC_COMMON          - After switching to virtual, relocated mode.
 */

#define EXC_REAL_BEGIN(name, start, size)			\
	FIXED_SECTION_ENTRY_BEGIN_LOCATION(real_vectors, exc_real_##start##_##name, start, size)

#define EXC_REAL_END(name, start, size)				\
	FIXED_SECTION_ENTRY_END_LOCATION(real_vectors, exc_real_##start##_##name, start, size)

#define EXC_VIRT_BEGIN(name, start, size)			\
	FIXED_SECTION_ENTRY_BEGIN_LOCATION(virt_vectors, exc_virt_##start##_##name, start, size)

#define EXC_VIRT_END(name, start, size)				\
	FIXED_SECTION_ENTRY_END_LOCATION(virt_vectors, exc_virt_##start##_##name, start, size)

#define EXC_COMMON_BEGIN(name)					\
	USE_TEXT_SECTION();					\
	.balign IFETCH_ALIGN_BYTES;				\
	.global name;						\
	_ASM_NOKPROBE_SYMBOL(name);				\
	DEFINE_FIXED_SYMBOL(name);				\
name:

#define TRAMP_REAL_BEGIN(name)					\
	FIXED_SECTION_ENTRY_BEGIN(real_trampolines, name)

#define TRAMP_VIRT_BEGIN(name)					\
	FIXED_SECTION_ENTRY_BEGIN(virt_trampolines, name)

#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
#define TRAMP_KVM_BEGIN(name)					\
	TRAMP_VIRT_BEGIN(name)
#else
#define TRAMP_KVM_BEGIN(name)
#endif

#define EXC_REAL_NONE(start, size)				\
	FIXED_SECTION_ENTRY_BEGIN_LOCATION(real_vectors, exc_real_##start##_##unused, start, size); \
	FIXED_SECTION_ENTRY_END_LOCATION(real_vectors, exc_real_##start##_##unused, start, size)

#define EXC_VIRT_NONE(start, size)				\
	FIXED_SECTION_ENTRY_BEGIN_LOCATION(virt_vectors, exc_virt_##start##_##unused, start, size); \
	FIXED_SECTION_ENTRY_END_LOCATION(virt_vectors, exc_virt_##start##_##unused, start, size)

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

/* Exception register prefixes */
#define EXC_HV_OR_STD	2 /* depends on HVMODE */
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
 * PPR save/restore macros used in exceptions-64s.S
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

/*
 * Branch to label using its 0xC000 address. This results in instruction
 * address suitable for MSR[IR]=0 or 1, which allows relocation to be turned
 * on using mtmsr rather than rfid.
 *
 * This could set the 0xc bits for !RELOCATABLE as an immediate, rather than
 * load KBASE for a slight optimisation.
 */
#define BRANCH_TO_C000(reg, label)					\
	__LOAD_FAR_HANDLER(reg, label);					\
	mtctr	reg;							\
	bctr

.macro INT_KVM_HANDLER name, vec, hsrr, area, skip
	TRAMP_KVM_BEGIN(\name\()_kvm)
	KVM_HANDLER \vec, \hsrr, \area, \skip
.endm

#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
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

.macro KVMTEST name, hsrr, n
	lbz	r10,HSTATE_IN_GUEST(r13)
	cmpwi	r10,0
	bne	\name\()_kvm
.endm

.macro KVM_HANDLER vec, hsrr, area, skip
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
	.if \hsrr == EXC_HV_OR_STD
	BEGIN_FTR_SECTION
	ori	r12,r12,(\vec + 0x2)
	FTR_SECTION_ELSE
	ori	r12,r12,(\vec)
	ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
	.elseif \hsrr
	ori	r12,r12,(\vec + 0x2)
	.else
	ori	r12,r12,(\vec)
	.endif

#ifdef CONFIG_RELOCATABLE
	/*
	 * KVM requires __LOAD_FAR_HANDLER beause kvmppc_interrupt lives
	 * outside the head section. CONFIG_RELOCATABLE KVM expects CTR
	 * to be saved in HSTATE_SCRATCH1.
	 */
	mfctr	r9
	std	r9,HSTATE_SCRATCH1(r13)
	__LOAD_FAR_HANDLER(r9, kvmppc_interrupt)
	mtctr	r9
	ld	r9,\area+EX_R9(r13)
	bctr
#else
	ld	r9,\area+EX_R9(r13)
	b	kvmppc_interrupt
#endif


	.if \skip
89:	mtocrf	0x80,r9
	ld	r9,\area+EX_R9(r13)
	ld	r10,\area+EX_R10(r13)
	.if \hsrr == EXC_HV_OR_STD
	BEGIN_FTR_SECTION
	b	kvmppc_skip_Hinterrupt
	FTR_SECTION_ELSE
	b	kvmppc_skip_interrupt
	ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
	.elseif \hsrr
	b	kvmppc_skip_Hinterrupt
	.else
	b	kvmppc_skip_interrupt
	.endif
	.endif
.endm

#else
.macro KVMTEST name, hsrr, n
.endm
.macro KVM_HANDLER name, vec, hsrr, area, skip
.endm
#endif

.macro INT_SAVE_SRR_AND_JUMP label, hsrr, set_ri
	ld	r10,PACAKMSR(r13)	/* get MSR value for kernel */
	.if ! \set_ri
	xori	r10,r10,MSR_RI		/* Clear MSR_RI */
	.endif
	.if \hsrr == EXC_HV_OR_STD
	BEGIN_FTR_SECTION
	mfspr	r11,SPRN_HSRR0		/* save HSRR0 */
	mfspr	r12,SPRN_HSRR1		/* and HSRR1 */
	mtspr	SPRN_HSRR1,r10
	FTR_SECTION_ELSE
	mfspr	r11,SPRN_SRR0		/* save SRR0 */
	mfspr	r12,SPRN_SRR1		/* and SRR1 */
	mtspr	SPRN_SRR1,r10
	ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
	.elseif \hsrr
	mfspr	r11,SPRN_HSRR0		/* save HSRR0 */
	mfspr	r12,SPRN_HSRR1		/* and HSRR1 */
	mtspr	SPRN_HSRR1,r10
	.else
	mfspr	r11,SPRN_SRR0		/* save SRR0 */
	mfspr	r12,SPRN_SRR1		/* and SRR1 */
	mtspr	SPRN_SRR1,r10
	.endif
	LOAD_HANDLER(r10, \label\())
	.if \hsrr == EXC_HV_OR_STD
	BEGIN_FTR_SECTION
	mtspr	SPRN_HSRR0,r10
	HRFI_TO_KERNEL
	FTR_SECTION_ELSE
	mtspr	SPRN_SRR0,r10
	RFI_TO_KERNEL
	ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
	.elseif \hsrr
	mtspr	SPRN_HSRR0,r10
	HRFI_TO_KERNEL
	.else
	mtspr	SPRN_SRR0,r10
	RFI_TO_KERNEL
	.endif
	b	.	/* prevent speculative execution */
.endm

/* INT_SAVE_SRR_AND_JUMP works for real or virt, this is faster but virt only */
.macro INT_VIRT_SAVE_SRR_AND_JUMP label, hsrr
#ifdef CONFIG_RELOCATABLE
	.if \hsrr == EXC_HV_OR_STD
	BEGIN_FTR_SECTION
	mfspr	r11,SPRN_HSRR0	/* save HSRR0 */
	FTR_SECTION_ELSE
	mfspr	r11,SPRN_SRR0	/* save SRR0 */
	ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
	.elseif \hsrr
	mfspr	r11,SPRN_HSRR0	/* save HSRR0 */
	.else
	mfspr	r11,SPRN_SRR0	/* save SRR0 */
	.endif
	LOAD_HANDLER(r12, \label\())
	mtctr	r12
	.if \hsrr == EXC_HV_OR_STD
	BEGIN_FTR_SECTION
	mfspr	r12,SPRN_HSRR1	/* and HSRR1 */
	FTR_SECTION_ELSE
	mfspr	r12,SPRN_SRR1	/* and HSRR1 */
	ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
	.elseif \hsrr
	mfspr	r12,SPRN_HSRR1	/* and HSRR1 */
	.else
	mfspr	r12,SPRN_SRR1	/* and HSRR1 */
	.endif
	li	r10,MSR_RI
	mtmsrd 	r10,1		/* Set RI (EE=0) */
	bctr
#else
	.if \hsrr == EXC_HV_OR_STD
	BEGIN_FTR_SECTION
	mfspr	r11,SPRN_HSRR0		/* save HSRR0 */
	mfspr	r12,SPRN_HSRR1		/* and HSRR1 */
	FTR_SECTION_ELSE
	mfspr	r11,SPRN_SRR0		/* save SRR0 */
	mfspr	r12,SPRN_SRR1		/* and SRR1 */
	ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
	.elseif \hsrr
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
 * This is the BOOK3S interrupt entry code macro.
 *
 * This can result in one of several things happening:
 * - Branch to the _common handler, relocated, in virtual mode.
 *   These are normal interrupts (synchronous and asynchronous) handled by
 *   the kernel.
 * - Branch to KVM, relocated but real mode interrupts remain in real mode.
 *   These occur when HSTATE_IN_GUEST is set. The interrupt may be caused by
 *   / intended for host or guest kernel, but KVM must always be involved
 *   because the machine state is set for guest execution.
 * - Branch to the masked handler, unrelocated.
 *   These occur when maskable asynchronous interrupts are taken with the
 *   irq_soft_mask set.
 * - Branch to an "early" handler in real mode but relocated.
 *   This is done if early=1. MCE and HMI use these to handle errors in real
 *   mode.
 * - Fall through and continue executing in real, unrelocated mode.
 *   This is done if early=2.
 */
.macro INT_HANDLER name, vec, ool=0, early=0, virt=0, hsrr=0, area=PACA_EXGEN, ri=1, dar=0, dsisr=0, bitmask=0, kvm=0
	SET_SCRATCH0(r13)			/* save r13 */
	GET_PACA(r13)
	std	r9,\area\()+EX_R9(r13)		/* save r9 */
	OPT_GET_SPR(r9, SPRN_PPR, CPU_FTR_HAS_PPR)
	HMT_MEDIUM
	std	r10,\area\()+EX_R10(r13)	/* save r10 - r12 */
	OPT_GET_SPR(r10, SPRN_CFAR, CPU_FTR_CFAR)
	.if \ool
	.if !\virt
	b	tramp_real_\name
	.pushsection .text
	TRAMP_REAL_BEGIN(tramp_real_\name)
	.else
	b	tramp_virt_\name
	.pushsection .text
	TRAMP_VIRT_BEGIN(tramp_virt_\name)
	.endif
	.endif

	OPT_SAVE_REG_TO_PACA(\area\()+EX_PPR, r9, CPU_FTR_HAS_PPR)
	OPT_SAVE_REG_TO_PACA(\area\()+EX_CFAR, r10, CPU_FTR_CFAR)
	INTERRUPT_TO_KERNEL
	SAVE_CTR(r10, \area\())
	mfcr	r9
	.if \kvm
		KVMTEST \name \hsrr \vec
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

		.if \hsrr == EXC_HV_OR_STD
		BEGIN_FTR_SECTION
		bne	masked_Hinterrupt
		FTR_SECTION_ELSE
		bne	masked_interrupt
		ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
		.elseif \hsrr
		bne	masked_Hinterrupt
		.else
		bne	masked_interrupt
		.endif
	.endif

	std	r11,\area\()+EX_R11(r13)
	std	r12,\area\()+EX_R12(r13)

	/*
	 * DAR/DSISR, SCRATCH0 must be read before setting MSR[RI],
	 * because a d-side MCE will clobber those registers so is
	 * not recoverable if they are live.
	 */
	GET_SCRATCH0(r10)
	std	r10,\area\()+EX_R13(r13)
	.if \dar
	.if \hsrr
	mfspr	r10,SPRN_HDAR
	.else
	mfspr	r10,SPRN_DAR
	.endif
	std	r10,\area\()+EX_DAR(r13)
	.endif
	.if \dsisr
	.if \hsrr
	mfspr	r10,SPRN_HDSISR
	.else
	mfspr	r10,SPRN_DSISR
	.endif
	stw	r10,\area\()+EX_DSISR(r13)
	.endif

	.if \early == 2
	/* nothing more */
	.elseif \early
	mfctr	r10			/* save ctr, even for !RELOCATABLE */
	BRANCH_TO_C000(r11, \name\()_early_common)
	.elseif !\virt
	INT_SAVE_SRR_AND_JUMP \name\()_common, \hsrr, \ri
	.else
	INT_VIRT_SAVE_SRR_AND_JUMP \name\()_common, \hsrr
	.endif
	.if \ool
	.popsection
	.endif
.endm

/*
 * On entry r13 points to the paca, r9-r13 are saved in the paca,
 * r9 contains the saved CR, r11 and r12 contain the saved SRR0 and
 * SRR1, and relocation is on.
 *
 * If stack=0, then the stack is already set in r1, and r1 is saved in r10.
 * PPR save and CPU accounting is not done for the !stack case (XXX why not?)
 */
.macro INT_COMMON vec, area, stack, kaup, reconcile, dar, dsisr
	.if \stack
	andi.	r10,r12,MSR_PR		/* See if coming from user	*/
	mr	r10,r1			/* Save r1			*/
	subi	r1,r1,INT_FRAME_SIZE	/* alloc frame on kernel stack	*/
	beq-	100f
	ld	r1,PACAKSAVE(r13)	/* kernel stack to use		*/
100:	tdgei	r1,-INT_FRAME_SIZE	/* trap if r1 is in userspace	*/
	EMIT_BUG_ENTRY 100b,__FILE__,__LINE__,0
	.endif

	std	r9,_CCR(r1)		/* save CR in stackframe	*/
	std	r11,_NIP(r1)		/* save SRR0 in stackframe	*/
	std	r12,_MSR(r1)		/* save SRR1 in stackframe	*/
	std	r10,0(r1)		/* make stack chain pointer	*/
	std	r0,GPR0(r1)		/* save r0 in stackframe	*/
	std	r10,GPR1(r1)		/* save r1 in stackframe	*/

	.if \stack
	.if \kaup
	kuap_save_amr_and_lock r9, r10, cr1, cr0
	.endif
	beq	101f			/* if from kernel mode		*/
	ACCOUNT_CPU_USER_ENTRY(r13, r9, r10)
	SAVE_PPR(\area, r9)
101:
	.else
	.if \kaup
	kuap_save_amr_and_lock r9, r10, cr1
	.endif
	.endif

	/* Save original regs values from save area to stack frame. */
	ld	r9,\area+EX_R9(r13)	/* move r9, r10 to stackframe	*/
	ld	r10,\area+EX_R10(r13)
	std	r9,GPR9(r1)
	std	r10,GPR10(r1)
	ld	r9,\area+EX_R11(r13)	/* move r11 - r13 to stackframe	*/
	ld	r10,\area+EX_R12(r13)
	ld	r11,\area+EX_R13(r13)
	std	r9,GPR11(r1)
	std	r10,GPR12(r1)
	std	r11,GPR13(r1)
	.if \dar
	.if \dar == 2
	ld	r10,_NIP(r1)
	.else
	ld	r10,\area+EX_DAR(r13)
	.endif
	std	r10,_DAR(r1)
	.endif
	.if \dsisr
	.if \dsisr == 2
	ld	r10,_MSR(r1)
	lis	r11,DSISR_SRR1_MATCH_64S@h
	and	r10,r10,r11
	.else
	lwz	r10,\area+EX_DSISR(r13)
	.endif
	std	r10,_DSISR(r1)
	.endif
BEGIN_FTR_SECTION_NESTED(66)
	ld	r10,\area+EX_CFAR(r13)
	std	r10,ORIG_GPR3(r1)
END_FTR_SECTION_NESTED(CPU_FTR_CFAR, CPU_FTR_CFAR, 66)
	GET_CTR(r10, \area)
	std	r10,_CTR(r1)
	std	r2,GPR2(r1)		/* save r2 in stackframe	*/
	SAVE_4GPRS(3, r1)		/* save r3 - r6 in stackframe   */
	SAVE_2GPRS(7, r1)		/* save r7, r8 in stackframe	*/
	mflr	r9			/* Get LR, later save to stack	*/
	ld	r2,PACATOC(r13)		/* get kernel TOC into r2	*/
	std	r9,_LINK(r1)
	lbz	r10,PACAIRQSOFTMASK(r13)
	mfspr	r11,SPRN_XER		/* save XER in stackframe	*/
	std	r10,SOFTE(r1)
	std	r11,_XER(r1)
	li	r9,(\vec)+1
	std	r9,_TRAP(r1)		/* set trap number		*/
	li	r10,0
	ld	r11,exception_marker@toc(r2)
	std	r10,RESULT(r1)		/* clear regs->result		*/
	std	r11,STACK_FRAME_OVERHEAD-16(r1) /* mark the frame	*/

	.if \stack
	ACCOUNT_STOLEN_TIME
	.endif

	.if \reconcile
	RECONCILE_IRQ_STATE(r10, r11)
	.endif
.endm

/*
 * Restore all registers including H/SRR0/1 saved in a stack frame of a
 * standard exception.
 */
.macro EXCEPTION_RESTORE_REGS hsrr
	/* Move original SRR0 and SRR1 into the respective regs */
	ld	r9,_MSR(r1)
	.if \hsrr == EXC_HV_OR_STD
	.error "EXC_HV_OR_STD Not implemented for EXCEPTION_RESTORE_REGS"
	.endif
	.if \hsrr
	mtspr	SPRN_HSRR1,r9
	.else
	mtspr	SPRN_SRR1,r9
	.endif
	ld	r9,_NIP(r1)
	.if \hsrr
	mtspr	SPRN_HSRR0,r9
	.else
	mtspr	SPRN_SRR0,r9
	.endif
	ld	r9,_CTR(r1)
	mtctr	r9
	ld	r9,_XER(r1)
	mtxer	r9
	ld	r9,_LINK(r1)
	mtlr	r9
	ld	r9,_CCR(r1)
	mtcr	r9
	REST_8GPRS(2, r1)
	REST_4GPRS(10, r1)
	REST_GPR(0, r1)
	/* restore original r1. */
	ld	r1,GPR1(r1)
.endm

#define RUNLATCH_ON				\
BEGIN_FTR_SECTION				\
	ld	r3, PACA_THREAD_INFO(r13);	\
	ld	r4,TI_LOCAL_FLAGS(r3);		\
	andi.	r0,r4,_TLF_RUNLATCH;		\
	beql	ppc64_runlatch_on_trampoline;	\
END_FTR_SECTION_IFSET(CPU_FTR_CTRL)

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

#define EXC_COMMON(name, realvec, hdlr)					\
	EXC_COMMON_BEGIN(name);						\
	INT_COMMON realvec, PACA_EXGEN, 1, 1, 1, 0, 0 ;			\
	bl	save_nvgprs;						\
	addi	r3,r1,STACK_FRAME_OVERHEAD;				\
	bl	hdlr;							\
	b	ret_from_except

/*
 * Like EXC_COMMON, but for exceptions that can occur in the idle task and
 * therefore need the special idle handling (finish nap and runlatch)
 */
#define EXC_COMMON_ASYNC(name, realvec, hdlr)				\
	EXC_COMMON_BEGIN(name);						\
	INT_COMMON realvec, PACA_EXGEN, 1, 1, 1, 0, 0 ;			\
	FINISH_NAP;							\
	RUNLATCH_ON;							\
	addi	r3,r1,STACK_FRAME_OVERHEAD;				\
	bl	hdlr;							\
	b	ret_from_except_lite


/*
 * There are a few constraints to be concerned with.
 * - Real mode exceptions code/data must be located at their physical location.
 * - Virtual mode exceptions must be mapped at their 0xc000... location.
 * - Fixed location code must not call directly beyond the __end_interrupts
 *   area when built with CONFIG_RELOCATABLE. LOAD_HANDLER / bctr sequence
 *   must be used.
 * - LOAD_HANDLER targets must be within first 64K of physical 0 /
 *   virtual 0xc00...
 * - Conditional branch targets must be within +/-32K of caller.
 *
 * "Virtual exceptions" run with relocation on (MSR_IR=1, MSR_DR=1), and
 * therefore don't have to run in physically located code or rfid to
 * virtual mode kernel code. However on relocatable kernels they do have
 * to branch to KERNELBASE offset because the rest of the kernel (outside
 * the exception vectors) may be located elsewhere.
 *
 * Virtual exceptions correspond with physical, except their entry points
 * are offset by 0xc000000000000000 and also tend to get an added 0x4000
 * offset applied. Virtual exceptions are enabled with the Alternate
 * Interrupt Location (AIL) bit set in the LPCR. However this does not
 * guarantee they will be delivered virtually. Some conditions (see the ISA)
 * cause exceptions to be delivered in real mode.
 *
 * It's impossible to receive interrupts below 0x300 via AIL.
 *
 * KVM: None of the virtual exceptions are from the guest. Anything that
 * escalated to HV=1 from HV=0 is delivered via real mode handlers.
 *
 *
 * We layout physical memory as follows:
 * 0x0000 - 0x00ff : Secondary processor spin code
 * 0x0100 - 0x18ff : Real mode pSeries interrupt vectors
 * 0x1900 - 0x3fff : Real mode trampolines
 * 0x4000 - 0x58ff : Relon (IR=1,DR=1) mode pSeries interrupt vectors
 * 0x5900 - 0x6fff : Relon mode trampolines
 * 0x7000 - 0x7fff : FWNMI data area
 * 0x8000 -   .... : Common interrupt handlers, remaining early
 *                   setup code, rest of kernel.
 *
 * We could reclaim 0x4000-0x42ff for real mode trampolines if the space
 * is necessary. Until then it's more consistent to explicitly put VIRT_NONE
 * vectors there.
 */
OPEN_FIXED_SECTION(real_vectors,        0x0100, 0x1900)
OPEN_FIXED_SECTION(real_trampolines,    0x1900, 0x4000)
OPEN_FIXED_SECTION(virt_vectors,        0x4000, 0x5900)
OPEN_FIXED_SECTION(virt_trampolines,    0x5900, 0x7000)

#ifdef CONFIG_PPC_POWERNV
	.globl start_real_trampolines
	.globl end_real_trampolines
	.globl start_virt_trampolines
	.globl end_virt_trampolines
#endif

#if defined(CONFIG_PPC_PSERIES) || defined(CONFIG_PPC_POWERNV)
/*
 * Data area reserved for FWNMI option.
 * This address (0x7000) is fixed by the RPA.
 * pseries and powernv need to keep the whole page from
 * 0x7000 to 0x8000 free for use by the firmware
 */
ZERO_FIXED_SECTION(fwnmi_page,          0x7000, 0x8000)
OPEN_TEXT_SECTION(0x8000)
#else
OPEN_TEXT_SECTION(0x7000)
#endif

USE_FIXED_SECTION(real_vectors)

/*
 * This is the start of the interrupt handlers for pSeries
 * This code runs with relocation off.
 * Code from here to __end_interrupts gets copied down to real
 * address 0x100 when we are running a relocatable kernel.
 * Therefore any relative branches in this section must only
 * branch to labels in this section.
 */
	.globl __start_interrupts
__start_interrupts:

/* No virt vectors corresponding with 0x0..0x100 */
EXC_VIRT_NONE(0x4000, 0x100)


EXC_REAL_BEGIN(system_reset, 0x100, 0x100)
#ifdef CONFIG_PPC_P7_NAP
	/*
	 * If running native on arch 2.06 or later, check if we are waking up
	 * from nap/sleep/winkle, and branch to idle handler. This tests SRR1
	 * bits 46:47. A non-0 value indicates that we are coming from a power
	 * saving state. The idle wakeup handler initially runs in real mode,
	 * but we branch to the 0xc000... address so we can turn on relocation
	 * with mtmsrd later, after SPRs are restored.
	 *
	 * Careful to minimise cost for the fast path (idle wakeup) while
	 * also avoiding clobbering CFAR for the debug path (non-idle).
	 *
	 * For the idle wake case volatile registers can be clobbered, which
	 * is why we use those initially. If it turns out to not be an idle
	 * wake, carefully put everything back the way it was, so we can use
	 * common exception macros to handle it.
	 */
BEGIN_FTR_SECTION
	SET_SCRATCH0(r13)
	GET_PACA(r13)
	std	r3,PACA_EXNMI+0*8(r13)
	std	r4,PACA_EXNMI+1*8(r13)
	std	r5,PACA_EXNMI+2*8(r13)
	mfspr	r3,SPRN_SRR1
	mfocrf	r4,0x80
	rlwinm.	r5,r3,47-31,30,31
	bne+	system_reset_idle_wake
	/* Not powersave wakeup. Restore regs for regular interrupt handler. */
	mtocrf	0x80,r4
	ld	r3,PACA_EXNMI+0*8(r13)
	ld	r4,PACA_EXNMI+1*8(r13)
	ld	r5,PACA_EXNMI+2*8(r13)
	GET_SCRATCH0(r13)
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
#endif

	INT_HANDLER system_reset, 0x100, area=PACA_EXNMI, ri=0, kvm=1
	/*
	 * MSR_RI is not enabled, because PACA_EXNMI and nmi stack is
	 * being used, so a nested NMI exception would corrupt it.
	 *
	 * In theory, we should not enable relocation here if it was disabled
	 * in SRR1, because the MMU may not be configured to support it (e.g.,
	 * SLB may have been cleared). In practice, there should only be a few
	 * small windows where that's the case, and sreset is considered to
	 * be dangerous anyway.
	 */
EXC_REAL_END(system_reset, 0x100, 0x100)
EXC_VIRT_NONE(0x4100, 0x100)
INT_KVM_HANDLER system_reset 0x100, EXC_STD, PACA_EXNMI, 0

#ifdef CONFIG_PPC_P7_NAP
TRAMP_REAL_BEGIN(system_reset_idle_wake)
	/* We are waking up from idle, so may clobber any volatile register */
	cmpwi	cr1,r5,2
	bltlr	cr1	/* no state loss, return to idle caller with r3=SRR1 */
	BRANCH_TO_C000(r12, DOTSYM(idle_return_gpr_loss))
#endif

#ifdef CONFIG_PPC_PSERIES
/*
 * Vectors for the FWNMI option.  Share common code.
 */
TRAMP_REAL_BEGIN(system_reset_fwnmi)
	/* See comment at system_reset exception, don't turn on RI */
	INT_HANDLER system_reset, 0x100, area=PACA_EXNMI, ri=0

#endif /* CONFIG_PPC_PSERIES */

EXC_COMMON_BEGIN(system_reset_common)
	/*
	 * Increment paca->in_nmi then enable MSR_RI. SLB or MCE will be able
	 * to recover, but nested NMI will notice in_nmi and not recover
	 * because of the use of the NMI stack. in_nmi reentrancy is tested in
	 * system_reset_exception.
	 */
	lhz	r10,PACA_IN_NMI(r13)
	addi	r10,r10,1
	sth	r10,PACA_IN_NMI(r13)
	li	r10,MSR_RI
	mtmsrd 	r10,1

	mr	r10,r1
	ld	r1,PACA_NMI_EMERG_SP(r13)
	subi	r1,r1,INT_FRAME_SIZE
	INT_COMMON 0x100, PACA_EXNMI, 0, 1, 0, 0, 0
	bl	save_nvgprs
	/*
	 * Set IRQS_ALL_DISABLED unconditionally so arch_irqs_disabled does
	 * the right thing. We do not want to reconcile because that goes
	 * through irq tracing which we don't want in NMI.
	 *
	 * Save PACAIRQHAPPENED because some code will do a hard disable
	 * (e.g., xmon). So we want to restore this back to where it was
	 * when we return. DAR is unused in the stack, so save it there.
	 */
	li	r10,IRQS_ALL_DISABLED
	stb	r10,PACAIRQSOFTMASK(r13)
	lbz	r10,PACAIRQHAPPENED(r13)
	std	r10,_DAR(r1)

	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	system_reset_exception

	/* Clear MSR_RI before setting SRR0 and SRR1. */
	li	r9,0
	mtmsrd	r9,1

	/*
	 * MSR_RI is clear, now we can decrement paca->in_nmi.
	 */
	lhz	r10,PACA_IN_NMI(r13)
	subi	r10,r10,1
	sth	r10,PACA_IN_NMI(r13)

	/*
	 * Restore soft mask settings.
	 */
	ld	r10,_DAR(r1)
	stb	r10,PACAIRQHAPPENED(r13)
	ld	r10,SOFTE(r1)
	stb	r10,PACAIRQSOFTMASK(r13)

	EXCEPTION_RESTORE_REGS EXC_STD
	RFI_TO_USER_OR_KERNEL


EXC_REAL_BEGIN(machine_check, 0x200, 0x100)
	INT_HANDLER machine_check, 0x200, early=1, area=PACA_EXMC, dar=1, dsisr=1
	/*
	 * MSR_RI is not enabled, because PACA_EXMC is being used, so a
	 * nested machine check corrupts it. machine_check_common enables
	 * MSR_RI.
	 */
EXC_REAL_END(machine_check, 0x200, 0x100)
EXC_VIRT_NONE(0x4200, 0x100)

#ifdef CONFIG_PPC_PSERIES
TRAMP_REAL_BEGIN(machine_check_fwnmi)
	/* See comment at machine_check exception, don't turn on RI */
	INT_HANDLER machine_check, 0x200, early=1, area=PACA_EXMC, dar=1, dsisr=1
#endif

INT_KVM_HANDLER machine_check 0x200, EXC_STD, PACA_EXMC, 1

#define MACHINE_CHECK_HANDLER_WINDUP			\
	/* Clear MSR_RI before setting SRR0 and SRR1. */\
	li	r9,0;					\
	mtmsrd	r9,1;		/* Clear MSR_RI */	\
	/* Decrement paca->in_mce now RI is clear. */	\
	lhz	r12,PACA_IN_MCE(r13);			\
	subi	r12,r12,1;				\
	sth	r12,PACA_IN_MCE(r13);			\
	EXCEPTION_RESTORE_REGS EXC_STD

EXC_COMMON_BEGIN(machine_check_early_common)
	mtctr	r10			/* Restore ctr */
	mfspr	r11,SPRN_SRR0
	mfspr	r12,SPRN_SRR1

	/*
	 * Switch to mc_emergency stack and handle re-entrancy (we limit
	 * the nested MCE upto level 4 to avoid stack overflow).
	 * Save MCE registers srr1, srr0, dar and dsisr and then set ME=1
	 *
	 * We use paca->in_mce to check whether this is the first entry or
	 * nested machine check. We increment paca->in_mce to track nested
	 * machine checks.
	 *
	 * If this is the first entry then set stack pointer to
	 * paca->mc_emergency_sp, otherwise r1 is already pointing to
	 * stack frame on mc_emergency stack.
	 *
	 * NOTE: We are here with MSR_ME=0 (off), which means we risk a
	 * checkstop if we get another machine check exception before we do
	 * rfid with MSR_ME=1.
	 *
	 * This interrupt can wake directly from idle. If that is the case,
	 * the machine check is handled then the idle wakeup code is called
	 * to restore state.
	 */
	lhz	r10,PACA_IN_MCE(r13)
	cmpwi	r10,0			/* Are we in nested machine check */
	cmpwi	cr1,r10,MAX_MCE_DEPTH	/* Are we at maximum nesting */
	addi	r10,r10,1		/* increment paca->in_mce */
	sth	r10,PACA_IN_MCE(r13)

	mr	r10,r1			/* Save r1 */
	bne	1f
	/* First machine check entry */
	ld	r1,PACAMCEMERGSP(r13)	/* Use MC emergency stack */
1:	/* Limit nested MCE to level 4 to avoid stack overflow */
	bgt	cr1,unrecoverable_mce	/* Check if we hit limit of 4 */
	subi	r1,r1,INT_FRAME_SIZE	/* alloc stack frame */

	/* We don't touch AMR here, we never go to virtual mode */
	INT_COMMON 0x200, PACA_EXMC, 0, 0, 0, 1, 1

BEGIN_FTR_SECTION
	bl	enable_machine_check
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE)
	li	r10,MSR_RI
	mtmsrd	r10,1

	bl	save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	machine_check_early
	std	r3,RESULT(r1)	/* Save result */
	ld	r12,_MSR(r1)

#ifdef CONFIG_PPC_P7_NAP
	/*
	 * Check if thread was in power saving mode. We come here when any
	 * of the following is true:
	 * a. thread wasn't in power saving mode
	 * b. thread was in power saving mode with no state loss,
	 *    supervisor state loss or hypervisor state loss.
	 *
	 * Go back to nap/sleep/winkle mode again if (b) is true.
	 */
BEGIN_FTR_SECTION
	rlwinm.	r11,r12,47-31,30,31
	bne	machine_check_idle_common
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
#endif

#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
	/*
	 * Check if we are coming from guest. If yes, then run the normal
	 * exception handler which will take the
	 * machine_check_kvm->kvmppc_interrupt branch to deliver the MC event
	 * to guest.
	 */
	lbz	r11,HSTATE_IN_GUEST(r13)
	cmpwi	r11,0			/* Check if coming from guest */
	bne	mce_deliver		/* continue if we are. */
#endif

	/*
	 * Check if we are coming from userspace. If yes, then run the normal
	 * exception handler which will deliver the MC event to this kernel.
	 */
	andi.	r11,r12,MSR_PR		/* See if coming from user. */
	bne	mce_deliver		/* continue in V mode if we are. */

	/*
	 * At this point we are coming from kernel context.
	 * Queue up the MCE event and return from the interrupt.
	 * But before that, check if this is an un-recoverable exception.
	 * If yes, then stay on emergency stack and panic.
	 */
	andi.	r11,r12,MSR_RI
	beq	unrecoverable_mce

	/*
	 * Check if we have successfully handled/recovered from error, if not
	 * then stay on emergency stack and panic.
	 */
	ld	r3,RESULT(r1)	/* Load result */
	cmpdi	r3,0		/* see if we handled MCE successfully */
	beq	unrecoverable_mce /* if !handled then panic */

	/*
	 * Return from MC interrupt.
	 * Queue up the MCE event so that we can log it later, while
	 * returning from kernel or opal call.
	 */
	bl	machine_check_queue_event
	MACHINE_CHECK_HANDLER_WINDUP
	RFI_TO_KERNEL

mce_deliver:
	/*
	 * This is a host user or guest MCE. Restore all registers, then
	 * run the "late" handler. For host user, this will run the
	 * machine_check_exception handler in virtual mode like a normal
	 * interrupt handler. For guest, this will trigger the KVM test
	 * and branch to the KVM interrupt similarly to other interrupts.
	 */
BEGIN_FTR_SECTION
	ld	r10,ORIG_GPR3(r1)
	mtspr	SPRN_CFAR,r10
END_FTR_SECTION_IFSET(CPU_FTR_CFAR)
	MACHINE_CHECK_HANDLER_WINDUP
	/* See comment at machine_check exception, don't turn on RI */
	INT_HANDLER machine_check, 0x200, area=PACA_EXMC, ri=0, dar=1, dsisr=1, kvm=1

EXC_COMMON_BEGIN(machine_check_common)
	/*
	 * Machine check is different because we use a different
	 * save area: PACA_EXMC instead of PACA_EXGEN.
	 */
	INT_COMMON 0x200, PACA_EXMC, 1, 1, 1, 1, 1
	FINISH_NAP
	/* Enable MSR_RI when finished with PACA_EXMC */
	li	r10,MSR_RI
	mtmsrd 	r10,1
	bl	save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	machine_check_exception
	b	ret_from_except

#ifdef CONFIG_PPC_P7_NAP
/*
 * This is an idle wakeup. Low level machine check has already been
 * done. Queue the event then call the idle code to do the wake up.
 */
EXC_COMMON_BEGIN(machine_check_idle_common)
	bl	machine_check_queue_event

	/*
	 * We have not used any non-volatile GPRs here, and as a rule
	 * most exception code including machine check does not.
	 * Therefore PACA_NAPSTATELOST does not need to be set. Idle
	 * wakeup will restore volatile registers.
	 *
	 * Load the original SRR1 into r3 for pnv_powersave_wakeup_mce.
	 *
	 * Then decrement MCE nesting after finishing with the stack.
	 */
	ld	r3,_MSR(r1)
	ld	r4,_LINK(r1)

	lhz	r11,PACA_IN_MCE(r13)
	subi	r11,r11,1
	sth	r11,PACA_IN_MCE(r13)

	mtlr	r4
	rlwinm	r10,r3,47-31,30,31
	cmpwi	cr1,r10,2
	bltlr	cr1	/* no state loss, return to idle caller */
	b	idle_return_gpr_loss
#endif

EXC_COMMON_BEGIN(unrecoverable_mce)
	/*
	 * We are going down. But there are chances that we might get hit by
	 * another MCE during panic path and we may run into unstable state
	 * with no way out. Hence, turn ME bit off while going down, so that
	 * when another MCE is hit during panic path, system will checkstop
	 * and hypervisor will get restarted cleanly by SP.
	 */
BEGIN_FTR_SECTION
	li	r10,0 /* clear MSR_RI */
	mtmsrd	r10,1
	bl	disable_machine_check
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE)
	ld	r10,PACAKMSR(r13)
	li	r3,MSR_ME
	andc	r10,r10,r3
	mtmsrd	r10

	/* Invoke machine_check_exception to print MCE event and panic. */
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	machine_check_exception

	/*
	 * We will not reach here. Even if we did, there is no way out.
	 * Call unrecoverable_exception and die.
	 */
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	unrecoverable_exception
	b	.


EXC_REAL_BEGIN(data_access, 0x300, 0x80)
	INT_HANDLER data_access, 0x300, ool=1, dar=1, dsisr=1, kvm=1
EXC_REAL_END(data_access, 0x300, 0x80)
EXC_VIRT_BEGIN(data_access, 0x4300, 0x80)
	INT_HANDLER data_access, 0x300, virt=1, dar=1, dsisr=1
EXC_VIRT_END(data_access, 0x4300, 0x80)
INT_KVM_HANDLER data_access, 0x300, EXC_STD, PACA_EXGEN, 1
EXC_COMMON_BEGIN(data_access_common)
	/*
	 * Here r13 points to the paca, r9 contains the saved CR,
	 * SRR0 and SRR1 are saved in r11 and r12,
	 * r9 - r13 are saved in paca->exgen.
	 * EX_DAR and EX_DSISR have saved DAR/DSISR
	 */
	INT_COMMON 0x300, PACA_EXGEN, 1, 1, 1, 1, 1
	ld	r4,_DAR(r1)
	ld	r5,_DSISR(r1)
BEGIN_MMU_FTR_SECTION
	ld	r6,_MSR(r1)
	li	r3,0x300
	b	do_hash_page		/* Try to handle as hpte fault */
MMU_FTR_SECTION_ELSE
	b	handle_page_fault
ALT_MMU_FTR_SECTION_END_IFCLR(MMU_FTR_TYPE_RADIX)


EXC_REAL_BEGIN(data_access_slb, 0x380, 0x80)
	INT_HANDLER data_access_slb, 0x380, ool=1, area=PACA_EXSLB, dar=1, kvm=1
EXC_REAL_END(data_access_slb, 0x380, 0x80)
EXC_VIRT_BEGIN(data_access_slb, 0x4380, 0x80)
	INT_HANDLER data_access_slb, 0x380, virt=1, area=PACA_EXSLB, dar=1
EXC_VIRT_END(data_access_slb, 0x4380, 0x80)
INT_KVM_HANDLER data_access_slb, 0x380, EXC_STD, PACA_EXSLB, 1
EXC_COMMON_BEGIN(data_access_slb_common)
	INT_COMMON 0x380, PACA_EXSLB, 1, 1, 0, 1, 0
	ld	r4,_DAR(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
BEGIN_MMU_FTR_SECTION
	/* HPT case, do SLB fault */
	bl	do_slb_fault
	cmpdi	r3,0
	bne-	1f
	b	fast_exception_return
1:	/* Error case */
MMU_FTR_SECTION_ELSE
	/* Radix case, access is outside page table range */
	li	r3,-EFAULT
ALT_MMU_FTR_SECTION_END_IFCLR(MMU_FTR_TYPE_RADIX)
	std	r3,RESULT(r1)
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	ld	r4,_DAR(r1)
	ld	r5,RESULT(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	do_bad_slb_fault
	b	ret_from_except


EXC_REAL_BEGIN(instruction_access, 0x400, 0x80)
	INT_HANDLER instruction_access, 0x400, kvm=1
EXC_REAL_END(instruction_access, 0x400, 0x80)
EXC_VIRT_BEGIN(instruction_access, 0x4400, 0x80)
	INT_HANDLER instruction_access, 0x400, virt=1
EXC_VIRT_END(instruction_access, 0x4400, 0x80)
INT_KVM_HANDLER instruction_access, 0x400, EXC_STD, PACA_EXGEN, 0
EXC_COMMON_BEGIN(instruction_access_common)
	INT_COMMON 0x400, PACA_EXGEN, 1, 1, 1, 2, 2
	ld	r4,_DAR(r1)
	ld	r5,_DSISR(r1)
BEGIN_MMU_FTR_SECTION
	ld      r6,_MSR(r1)
	li	r3,0x400
	b	do_hash_page		/* Try to handle as hpte fault */
MMU_FTR_SECTION_ELSE
	b	handle_page_fault
ALT_MMU_FTR_SECTION_END_IFCLR(MMU_FTR_TYPE_RADIX)


EXC_REAL_BEGIN(instruction_access_slb, 0x480, 0x80)
	INT_HANDLER instruction_access_slb, 0x480, area=PACA_EXSLB, kvm=1
EXC_REAL_END(instruction_access_slb, 0x480, 0x80)
EXC_VIRT_BEGIN(instruction_access_slb, 0x4480, 0x80)
	INT_HANDLER instruction_access_slb, 0x480, virt=1, area=PACA_EXSLB
EXC_VIRT_END(instruction_access_slb, 0x4480, 0x80)
INT_KVM_HANDLER instruction_access_slb, 0x480, EXC_STD, PACA_EXSLB, 0
EXC_COMMON_BEGIN(instruction_access_slb_common)
	INT_COMMON 0x480, PACA_EXSLB, 1, 1, 0, 2, 0
	ld	r4,_DAR(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
BEGIN_MMU_FTR_SECTION
	/* HPT case, do SLB fault */
	bl	do_slb_fault
	cmpdi	r3,0
	bne-	1f
	b	fast_exception_return
1:	/* Error case */
MMU_FTR_SECTION_ELSE
	/* Radix case, access is outside page table range */
	li	r3,-EFAULT
ALT_MMU_FTR_SECTION_END_IFCLR(MMU_FTR_TYPE_RADIX)
	std	r3,RESULT(r1)
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	ld	r4,_DAR(r1)
	ld	r5,RESULT(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	do_bad_slb_fault
	b	ret_from_except

EXC_REAL_BEGIN(hardware_interrupt, 0x500, 0x100)
	INT_HANDLER hardware_interrupt, 0x500, hsrr=EXC_HV_OR_STD, bitmask=IRQS_DISABLED, kvm=1
EXC_REAL_END(hardware_interrupt, 0x500, 0x100)
EXC_VIRT_BEGIN(hardware_interrupt, 0x4500, 0x100)
	INT_HANDLER hardware_interrupt, 0x500, virt=1, hsrr=EXC_HV_OR_STD, bitmask=IRQS_DISABLED, kvm=1
EXC_VIRT_END(hardware_interrupt, 0x4500, 0x100)
INT_KVM_HANDLER hardware_interrupt, 0x500, EXC_HV_OR_STD, PACA_EXGEN, 0
EXC_COMMON_ASYNC(hardware_interrupt_common, 0x500, do_IRQ)


EXC_REAL_BEGIN(alignment, 0x600, 0x100)
	INT_HANDLER alignment, 0x600, dar=1, dsisr=1, kvm=1
EXC_REAL_END(alignment, 0x600, 0x100)
EXC_VIRT_BEGIN(alignment, 0x4600, 0x100)
	INT_HANDLER alignment, 0x600, virt=1, dar=1, dsisr=1
EXC_VIRT_END(alignment, 0x4600, 0x100)
INT_KVM_HANDLER alignment, 0x600, EXC_STD, PACA_EXGEN, 0
EXC_COMMON_BEGIN(alignment_common)
	INT_COMMON 0x600, PACA_EXGEN, 1, 1, 1, 1, 1
	bl	save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	alignment_exception
	b	ret_from_except


EXC_REAL_BEGIN(program_check, 0x700, 0x100)
	INT_HANDLER program_check, 0x700, kvm=1
EXC_REAL_END(program_check, 0x700, 0x100)
EXC_VIRT_BEGIN(program_check, 0x4700, 0x100)
	INT_HANDLER program_check, 0x700, virt=1
EXC_VIRT_END(program_check, 0x4700, 0x100)
INT_KVM_HANDLER program_check, 0x700, EXC_STD, PACA_EXGEN, 0
EXC_COMMON_BEGIN(program_check_common)
	/*
	 * It's possible to receive a TM Bad Thing type program check with
	 * userspace register values (in particular r1), but with SRR1 reporting
	 * that we came from the kernel. Normally that would confuse the bad
	 * stack logic, and we would report a bad kernel stack pointer. Instead
	 * we switch to the emergency stack if we're taking a TM Bad Thing from
	 * the kernel.
	 */

	andi.	r10,r12,MSR_PR
	bne	2f			/* If userspace, go normal path */

	andis.	r10,r12,(SRR1_PROGTM)@h
	bne	1f			/* If TM, emergency		*/

	cmpdi	r1,-INT_FRAME_SIZE	/* check if r1 is in userspace	*/
	blt	2f			/* normal path if not		*/

	/* Use the emergency stack					*/
1:	andi.	r10,r12,MSR_PR		/* Set CR0 correctly for label	*/
					/* 3 in EXCEPTION_PROLOG_COMMON	*/
	mr	r10,r1			/* Save r1			*/
	ld	r1,PACAEMERGSP(r13)	/* Use emergency stack		*/
	subi	r1,r1,INT_FRAME_SIZE	/* alloc stack frame		*/
	INT_COMMON 0x700, PACA_EXGEN, 0, 1, 1, 0, 0
	b 3f
2:
	INT_COMMON 0x700, PACA_EXGEN, 1, 1, 1, 0, 0
3:
	bl	save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	program_check_exception
	b	ret_from_except


EXC_REAL_BEGIN(fp_unavailable, 0x800, 0x100)
	INT_HANDLER fp_unavailable, 0x800, kvm=1
EXC_REAL_END(fp_unavailable, 0x800, 0x100)
EXC_VIRT_BEGIN(fp_unavailable, 0x4800, 0x100)
	INT_HANDLER fp_unavailable, 0x800, virt=1
EXC_VIRT_END(fp_unavailable, 0x4800, 0x100)
INT_KVM_HANDLER fp_unavailable, 0x800, EXC_STD, PACA_EXGEN, 0
EXC_COMMON_BEGIN(fp_unavailable_common)
	INT_COMMON 0x800, PACA_EXGEN, 1, 1, 0, 0, 0
	bne	1f			/* if from user, just load it up */
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	kernel_fp_unavailable_exception
0:	trap
	EMIT_BUG_ENTRY 0b, __FILE__, __LINE__, 0
1:
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
BEGIN_FTR_SECTION
	/* Test if 2 TM state bits are zero.  If non-zero (ie. userspace was in
	 * transaction), go do TM stuff
	 */
	rldicl.	r0, r12, (64-MSR_TS_LG), (64-2)
	bne-	2f
END_FTR_SECTION_IFSET(CPU_FTR_TM)
#endif
	bl	load_up_fpu
	b	fast_exception_return
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
2:	/* User process was in a transaction */
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	fp_unavailable_tm
	b	ret_from_except
#endif


EXC_REAL_BEGIN(decrementer, 0x900, 0x80)
	INT_HANDLER decrementer, 0x900, ool=1, bitmask=IRQS_DISABLED, kvm=1
EXC_REAL_END(decrementer, 0x900, 0x80)
EXC_VIRT_BEGIN(decrementer, 0x4900, 0x80)
	INT_HANDLER decrementer, 0x900, virt=1, bitmask=IRQS_DISABLED
EXC_VIRT_END(decrementer, 0x4900, 0x80)
INT_KVM_HANDLER decrementer, 0x900, EXC_STD, PACA_EXGEN, 0
EXC_COMMON_ASYNC(decrementer_common, 0x900, timer_interrupt)


EXC_REAL_BEGIN(hdecrementer, 0x980, 0x80)
	INT_HANDLER hdecrementer, 0x980, hsrr=EXC_HV, kvm=1
EXC_REAL_END(hdecrementer, 0x980, 0x80)
EXC_VIRT_BEGIN(hdecrementer, 0x4980, 0x80)
	INT_HANDLER hdecrementer, 0x980, virt=1, hsrr=EXC_HV, kvm=1
EXC_VIRT_END(hdecrementer, 0x4980, 0x80)
INT_KVM_HANDLER hdecrementer, 0x980, EXC_HV, PACA_EXGEN, 0
EXC_COMMON(hdecrementer_common, 0x980, hdec_interrupt)


EXC_REAL_BEGIN(doorbell_super, 0xa00, 0x100)
	INT_HANDLER doorbell_super, 0xa00, bitmask=IRQS_DISABLED, kvm=1
EXC_REAL_END(doorbell_super, 0xa00, 0x100)
EXC_VIRT_BEGIN(doorbell_super, 0x4a00, 0x100)
	INT_HANDLER doorbell_super, 0xa00, virt=1, bitmask=IRQS_DISABLED
EXC_VIRT_END(doorbell_super, 0x4a00, 0x100)
INT_KVM_HANDLER doorbell_super, 0xa00, EXC_STD, PACA_EXGEN, 0
#ifdef CONFIG_PPC_DOORBELL
EXC_COMMON_ASYNC(doorbell_super_common, 0xa00, doorbell_exception)
#else
EXC_COMMON_ASYNC(doorbell_super_common, 0xa00, unknown_exception)
#endif


EXC_REAL_NONE(0xb00, 0x100)
EXC_VIRT_NONE(0x4b00, 0x100)

/*
 * system call / hypercall (0xc00, 0x4c00)
 *
 * The system call exception is invoked with "sc 0" and does not alter HV bit.
 *
 * The hypercall is invoked with "sc 1" and sets HV=1.
 *
 * In HPT, sc 1 always goes to 0xc00 real mode. In RADIX, sc 1 can go to
 * 0x4c00 virtual mode.
 *
 * Call convention:
 *
 * syscall register convention is in Documentation/powerpc/syscall64-abi.rst
 *
 * For hypercalls, the register convention is as follows:
 * r0 volatile
 * r1-2 nonvolatile
 * r3 volatile parameter and return value for status
 * r4-r10 volatile input and output value
 * r11 volatile hypercall number and output value
 * r12 volatile input and output value
 * r13-r31 nonvolatile
 * LR nonvolatile
 * CTR volatile
 * XER volatile
 * CR0-1 CR5-7 volatile
 * CR2-4 nonvolatile
 * Other registers nonvolatile
 *
 * The intersection of volatile registers that don't contain possible
 * inputs is: cr0, xer, ctr. We may use these as scratch regs upon entry
 * without saving, though xer is not a good idea to use, as hardware may
 * interpret some bits so it may be costly to change them.
 */
.macro SYSTEM_CALL virt
#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
	/*
	 * There is a little bit of juggling to get syscall and hcall
	 * working well. Save r13 in ctr to avoid using SPRG scratch
	 * register.
	 *
	 * Userspace syscalls have already saved the PPR, hcalls must save
	 * it before setting HMT_MEDIUM.
	 */
	mtctr	r13
	GET_PACA(r13)
	std	r10,PACA_EXGEN+EX_R10(r13)
	INTERRUPT_TO_KERNEL
	KVMTEST system_call EXC_STD 0xc00 /* uses r10, branch to system_call_kvm */
	mfctr	r9
#else
	mr	r9,r13
	GET_PACA(r13)
	INTERRUPT_TO_KERNEL
#endif

#ifdef CONFIG_PPC_FAST_ENDIAN_SWITCH
BEGIN_FTR_SECTION
	cmpdi	r0,0x1ebe
	beq-	1f
END_FTR_SECTION_IFSET(CPU_FTR_REAL_LE)
#endif

	/* We reach here with PACA in r13, r13 in r9. */
	mfspr	r11,SPRN_SRR0
	mfspr	r12,SPRN_SRR1

	HMT_MEDIUM

	.if ! \virt
	__LOAD_HANDLER(r10, system_call_common)
	mtspr	SPRN_SRR0,r10
	ld	r10,PACAKMSR(r13)
	mtspr	SPRN_SRR1,r10
	RFI_TO_KERNEL
	b	.	/* prevent speculative execution */
	.else
	li	r10,MSR_RI
	mtmsrd 	r10,1			/* Set RI (EE=0) */
#ifdef CONFIG_RELOCATABLE
	__LOAD_HANDLER(r10, system_call_common)
	mtctr	r10
	bctr
#else
	b	system_call_common
#endif
	.endif

#ifdef CONFIG_PPC_FAST_ENDIAN_SWITCH
	/* Fast LE/BE switch system call */
1:	mfspr	r12,SPRN_SRR1
	xori	r12,r12,MSR_LE
	mtspr	SPRN_SRR1,r12
	mr	r13,r9
	RFI_TO_USER	/* return to userspace */
	b	.	/* prevent speculative execution */
#endif
.endm

EXC_REAL_BEGIN(system_call, 0xc00, 0x100)
	SYSTEM_CALL 0
EXC_REAL_END(system_call, 0xc00, 0x100)
EXC_VIRT_BEGIN(system_call, 0x4c00, 0x100)
	SYSTEM_CALL 1
EXC_VIRT_END(system_call, 0x4c00, 0x100)

#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
	/*
	 * This is a hcall, so register convention is as above, with these
	 * differences:
	 * r13 = PACA
	 * ctr = orig r13
	 * orig r10 saved in PACA
	 */
TRAMP_KVM_BEGIN(system_call_kvm)
	 /*
	  * Save the PPR (on systems that support it) before changing to
	  * HMT_MEDIUM. That allows the KVM code to save that value into the
	  * guest state (it is the guest's PPR value).
	  */
	OPT_GET_SPR(r10, SPRN_PPR, CPU_FTR_HAS_PPR)
	HMT_MEDIUM
	OPT_SAVE_REG_TO_PACA(PACA_EXGEN+EX_PPR, r10, CPU_FTR_HAS_PPR)
	mfctr	r10
	SET_SCRATCH0(r10)
	std	r9,PACA_EXGEN+EX_R9(r13)
	mfcr	r9
	KVM_HANDLER 0xc00, EXC_STD, PACA_EXGEN, 0
#endif


EXC_REAL_BEGIN(single_step, 0xd00, 0x100)
	INT_HANDLER single_step, 0xd00, kvm=1
EXC_REAL_END(single_step, 0xd00, 0x100)
EXC_VIRT_BEGIN(single_step, 0x4d00, 0x100)
	INT_HANDLER single_step, 0xd00, virt=1
EXC_VIRT_END(single_step, 0x4d00, 0x100)
INT_KVM_HANDLER single_step, 0xd00, EXC_STD, PACA_EXGEN, 0
EXC_COMMON(single_step_common, 0xd00, single_step_exception)


EXC_REAL_BEGIN(h_data_storage, 0xe00, 0x20)
	INT_HANDLER h_data_storage, 0xe00, ool=1, hsrr=EXC_HV, dar=1, dsisr=1, kvm=1
EXC_REAL_END(h_data_storage, 0xe00, 0x20)
EXC_VIRT_BEGIN(h_data_storage, 0x4e00, 0x20)
	INT_HANDLER h_data_storage, 0xe00, ool=1, virt=1, hsrr=EXC_HV, dar=1, dsisr=1, kvm=1
EXC_VIRT_END(h_data_storage, 0x4e00, 0x20)
INT_KVM_HANDLER h_data_storage, 0xe00, EXC_HV, PACA_EXGEN, 1
EXC_COMMON_BEGIN(h_data_storage_common)
	INT_COMMON 0xe00, PACA_EXGEN, 1, 1, 1, 1, 1
	bl      save_nvgprs
	addi    r3,r1,STACK_FRAME_OVERHEAD
BEGIN_MMU_FTR_SECTION
	ld	r4,_DAR(r1)
	li	r5,SIGSEGV
	bl      bad_page_fault
MMU_FTR_SECTION_ELSE
	bl      unknown_exception
ALT_MMU_FTR_SECTION_END_IFSET(MMU_FTR_TYPE_RADIX)
	b       ret_from_except


EXC_REAL_BEGIN(h_instr_storage, 0xe20, 0x20)
	INT_HANDLER h_instr_storage, 0xe20, ool=1, hsrr=EXC_HV, kvm=1
EXC_REAL_END(h_instr_storage, 0xe20, 0x20)
EXC_VIRT_BEGIN(h_instr_storage, 0x4e20, 0x20)
	INT_HANDLER h_instr_storage, 0xe20, ool=1, virt=1, hsrr=EXC_HV, kvm=1
EXC_VIRT_END(h_instr_storage, 0x4e20, 0x20)
INT_KVM_HANDLER h_instr_storage, 0xe20, EXC_HV, PACA_EXGEN, 0
EXC_COMMON(h_instr_storage_common, 0xe20, unknown_exception)


EXC_REAL_BEGIN(emulation_assist, 0xe40, 0x20)
	INT_HANDLER emulation_assist, 0xe40, ool=1, hsrr=EXC_HV, kvm=1
EXC_REAL_END(emulation_assist, 0xe40, 0x20)
EXC_VIRT_BEGIN(emulation_assist, 0x4e40, 0x20)
	INT_HANDLER emulation_assist, 0xe40, ool=1, virt=1, hsrr=EXC_HV, kvm=1
EXC_VIRT_END(emulation_assist, 0x4e40, 0x20)
INT_KVM_HANDLER emulation_assist, 0xe40, EXC_HV, PACA_EXGEN, 0
EXC_COMMON(emulation_assist_common, 0xe40, emulation_assist_interrupt)


/*
 * hmi_exception trampoline is a special case. It jumps to hmi_exception_early
 * first, and then eventaully from there to the trampoline to get into virtual
 * mode.
 */
EXC_REAL_BEGIN(hmi_exception, 0xe60, 0x20)
	INT_HANDLER hmi_exception, 0xe60, ool=1, early=1, hsrr=EXC_HV, ri=0, kvm=1
EXC_REAL_END(hmi_exception, 0xe60, 0x20)
EXC_VIRT_NONE(0x4e60, 0x20)
INT_KVM_HANDLER hmi_exception, 0xe60, EXC_HV, PACA_EXGEN, 0
EXC_COMMON_BEGIN(hmi_exception_early_common)
	mtctr	r10			/* Restore ctr */
	mfspr	r11,SPRN_HSRR0		/* Save HSRR0 */
	mfspr	r12,SPRN_HSRR1		/* Save HSRR1 */
	mr	r10,r1			/* Save r1 */
	ld	r1,PACAEMERGSP(r13)	/* Use emergency stack for realmode */
	subi	r1,r1,INT_FRAME_SIZE	/* alloc stack frame		*/

	/* We don't touch AMR here, we never go to virtual mode */
	INT_COMMON 0xe60, PACA_EXGEN, 0, 0, 0, 0, 0

	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	hmi_exception_realmode
	cmpdi	cr0,r3,0
	bne	1f

	EXCEPTION_RESTORE_REGS EXC_HV
	HRFI_TO_USER_OR_KERNEL

1:
	/*
	 * Go to virtual mode and pull the HMI event information from
	 * firmware.
	 */
	EXCEPTION_RESTORE_REGS EXC_HV
	INT_HANDLER hmi_exception, 0xe60, hsrr=EXC_HV, bitmask=IRQS_DISABLED, kvm=1

EXC_COMMON_BEGIN(hmi_exception_common)
	INT_COMMON 0xe60, PACA_EXGEN, 1, 1, 1, 0, 0
	FINISH_NAP
	RUNLATCH_ON
	bl	save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	handle_hmi_exception
	b	ret_from_except


EXC_REAL_BEGIN(h_doorbell, 0xe80, 0x20)
	INT_HANDLER h_doorbell, 0xe80, ool=1, hsrr=EXC_HV, bitmask=IRQS_DISABLED, kvm=1
EXC_REAL_END(h_doorbell, 0xe80, 0x20)
EXC_VIRT_BEGIN(h_doorbell, 0x4e80, 0x20)
	INT_HANDLER h_doorbell, 0xe80, ool=1, virt=1, hsrr=EXC_HV, bitmask=IRQS_DISABLED, kvm=1
EXC_VIRT_END(h_doorbell, 0x4e80, 0x20)
INT_KVM_HANDLER h_doorbell, 0xe80, EXC_HV, PACA_EXGEN, 0
#ifdef CONFIG_PPC_DOORBELL
EXC_COMMON_ASYNC(h_doorbell_common, 0xe80, doorbell_exception)
#else
EXC_COMMON_ASYNC(h_doorbell_common, 0xe80, unknown_exception)
#endif


EXC_REAL_BEGIN(h_virt_irq, 0xea0, 0x20)
	INT_HANDLER h_virt_irq, 0xea0, ool=1, hsrr=EXC_HV, bitmask=IRQS_DISABLED, kvm=1
EXC_REAL_END(h_virt_irq, 0xea0, 0x20)
EXC_VIRT_BEGIN(h_virt_irq, 0x4ea0, 0x20)
	INT_HANDLER h_virt_irq, 0xea0, ool=1, virt=1, hsrr=EXC_HV, bitmask=IRQS_DISABLED, kvm=1
EXC_VIRT_END(h_virt_irq, 0x4ea0, 0x20)
INT_KVM_HANDLER h_virt_irq, 0xea0, EXC_HV, PACA_EXGEN, 0
EXC_COMMON_ASYNC(h_virt_irq_common, 0xea0, do_IRQ)


EXC_REAL_NONE(0xec0, 0x20)
EXC_VIRT_NONE(0x4ec0, 0x20)
EXC_REAL_NONE(0xee0, 0x20)
EXC_VIRT_NONE(0x4ee0, 0x20)


EXC_REAL_BEGIN(performance_monitor, 0xf00, 0x20)
	INT_HANDLER performance_monitor, 0xf00, ool=1, bitmask=IRQS_PMI_DISABLED, kvm=1
EXC_REAL_END(performance_monitor, 0xf00, 0x20)
EXC_VIRT_BEGIN(performance_monitor, 0x4f00, 0x20)
	INT_HANDLER performance_monitor, 0xf00, ool=1, virt=1, bitmask=IRQS_PMI_DISABLED
EXC_VIRT_END(performance_monitor, 0x4f00, 0x20)
INT_KVM_HANDLER performance_monitor, 0xf00, EXC_STD, PACA_EXGEN, 0
EXC_COMMON_ASYNC(performance_monitor_common, 0xf00, performance_monitor_exception)


EXC_REAL_BEGIN(altivec_unavailable, 0xf20, 0x20)
	INT_HANDLER altivec_unavailable, 0xf20, ool=1, kvm=1
EXC_REAL_END(altivec_unavailable, 0xf20, 0x20)
EXC_VIRT_BEGIN(altivec_unavailable, 0x4f20, 0x20)
	INT_HANDLER altivec_unavailable, 0xf20, ool=1, virt=1
EXC_VIRT_END(altivec_unavailable, 0x4f20, 0x20)
INT_KVM_HANDLER altivec_unavailable, 0xf20, EXC_STD, PACA_EXGEN, 0
EXC_COMMON_BEGIN(altivec_unavailable_common)
	INT_COMMON 0xf20, PACA_EXGEN, 1, 1, 0, 0, 0
#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	beq	1f
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
  BEGIN_FTR_SECTION_NESTED(69)
	/* Test if 2 TM state bits are zero.  If non-zero (ie. userspace was in
	 * transaction), go do TM stuff
	 */
	rldicl.	r0, r12, (64-MSR_TS_LG), (64-2)
	bne-	2f
  END_FTR_SECTION_NESTED(CPU_FTR_TM, CPU_FTR_TM, 69)
#endif
	bl	load_up_altivec
	b	fast_exception_return
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
2:	/* User process was in a transaction */
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	altivec_unavailable_tm
	b	ret_from_except
#endif
1:
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	altivec_unavailable_exception
	b	ret_from_except


EXC_REAL_BEGIN(vsx_unavailable, 0xf40, 0x20)
	INT_HANDLER vsx_unavailable, 0xf40, ool=1, kvm=1
EXC_REAL_END(vsx_unavailable, 0xf40, 0x20)
EXC_VIRT_BEGIN(vsx_unavailable, 0x4f40, 0x20)
	INT_HANDLER vsx_unavailable, 0xf40, ool=1, virt=1
EXC_VIRT_END(vsx_unavailable, 0x4f40, 0x20)
INT_KVM_HANDLER vsx_unavailable, 0xf40, EXC_STD, PACA_EXGEN, 0
EXC_COMMON_BEGIN(vsx_unavailable_common)
	INT_COMMON 0xf40, PACA_EXGEN, 1, 1, 0, 0, 0
#ifdef CONFIG_VSX
BEGIN_FTR_SECTION
	beq	1f
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
  BEGIN_FTR_SECTION_NESTED(69)
	/* Test if 2 TM state bits are zero.  If non-zero (ie. userspace was in
	 * transaction), go do TM stuff
	 */
	rldicl.	r0, r12, (64-MSR_TS_LG), (64-2)
	bne-	2f
  END_FTR_SECTION_NESTED(CPU_FTR_TM, CPU_FTR_TM, 69)
#endif
	b	load_up_vsx
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
2:	/* User process was in a transaction */
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	vsx_unavailable_tm
	b	ret_from_except
#endif
1:
END_FTR_SECTION_IFSET(CPU_FTR_VSX)
#endif
	bl	save_nvgprs
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	vsx_unavailable_exception
	b	ret_from_except


EXC_REAL_BEGIN(facility_unavailable, 0xf60, 0x20)
	INT_HANDLER facility_unavailable, 0xf60, ool=1, kvm=1
EXC_REAL_END(facility_unavailable, 0xf60, 0x20)
EXC_VIRT_BEGIN(facility_unavailable, 0x4f60, 0x20)
	INT_HANDLER facility_unavailable, 0xf60, ool=1, virt=1
EXC_VIRT_END(facility_unavailable, 0x4f60, 0x20)
INT_KVM_HANDLER facility_unavailable, 0xf60, EXC_STD, PACA_EXGEN, 0
EXC_COMMON(facility_unavailable_common, 0xf60, facility_unavailable_exception)


EXC_REAL_BEGIN(h_facility_unavailable, 0xf80, 0x20)
	INT_HANDLER h_facility_unavailable, 0xf80, ool=1, hsrr=EXC_HV, kvm=1
EXC_REAL_END(h_facility_unavailable, 0xf80, 0x20)
EXC_VIRT_BEGIN(h_facility_unavailable, 0x4f80, 0x20)
	INT_HANDLER h_facility_unavailable, 0xf80, ool=1, virt=1, hsrr=EXC_HV, kvm=1
EXC_VIRT_END(h_facility_unavailable, 0x4f80, 0x20)
INT_KVM_HANDLER h_facility_unavailable, 0xf80, EXC_HV, PACA_EXGEN, 0
EXC_COMMON(h_facility_unavailable_common, 0xf80, facility_unavailable_exception)


EXC_REAL_NONE(0xfa0, 0x20)
EXC_VIRT_NONE(0x4fa0, 0x20)
EXC_REAL_NONE(0xfc0, 0x20)
EXC_VIRT_NONE(0x4fc0, 0x20)
EXC_REAL_NONE(0xfe0, 0x20)
EXC_VIRT_NONE(0x4fe0, 0x20)

EXC_REAL_NONE(0x1000, 0x100)
EXC_VIRT_NONE(0x5000, 0x100)
EXC_REAL_NONE(0x1100, 0x100)
EXC_VIRT_NONE(0x5100, 0x100)

#ifdef CONFIG_CBE_RAS
EXC_REAL_BEGIN(cbe_system_error, 0x1200, 0x100)
	INT_HANDLER cbe_system_error, 0x1200, ool=1, hsrr=EXC_HV, kvm=1
EXC_REAL_END(cbe_system_error, 0x1200, 0x100)
EXC_VIRT_NONE(0x5200, 0x100)
INT_KVM_HANDLER cbe_system_error, 0x1200, EXC_HV, PACA_EXGEN, 1
EXC_COMMON(cbe_system_error_common, 0x1200, cbe_system_error_exception)
#else /* CONFIG_CBE_RAS */
EXC_REAL_NONE(0x1200, 0x100)
EXC_VIRT_NONE(0x5200, 0x100)
#endif


EXC_REAL_BEGIN(instruction_breakpoint, 0x1300, 0x100)
	INT_HANDLER instruction_breakpoint, 0x1300, kvm=1
EXC_REAL_END(instruction_breakpoint, 0x1300, 0x100)
EXC_VIRT_BEGIN(instruction_breakpoint, 0x5300, 0x100)
	INT_HANDLER instruction_breakpoint, 0x1300, virt=1
EXC_VIRT_END(instruction_breakpoint, 0x5300, 0x100)
INT_KVM_HANDLER instruction_breakpoint, 0x1300, EXC_STD, PACA_EXGEN, 1
EXC_COMMON(instruction_breakpoint_common, 0x1300, instruction_breakpoint_exception)


EXC_REAL_NONE(0x1400, 0x100)
EXC_VIRT_NONE(0x5400, 0x100)

EXC_REAL_BEGIN(denorm_exception_hv, 0x1500, 0x100)
	INT_HANDLER denorm_exception_hv, 0x1500, early=2, hsrr=EXC_HV
#ifdef CONFIG_PPC_DENORMALISATION
	mfspr	r10,SPRN_HSRR1
	andis.	r10,r10,(HSRR1_DENORM)@h /* denorm? */
	bne+	denorm_assist
#endif
	KVMTEST denorm_exception_hv, EXC_HV 0x1500
	INT_SAVE_SRR_AND_JUMP denorm_common, EXC_HV, 1
EXC_REAL_END(denorm_exception_hv, 0x1500, 0x100)

#ifdef CONFIG_PPC_DENORMALISATION
EXC_VIRT_BEGIN(denorm_exception, 0x5500, 0x100)
	INT_HANDLER denorm_exception, 0x1500, 0, 2, 1, EXC_HV, PACA_EXGEN, 1, 0, 0, 0, 0
	mfspr	r10,SPRN_HSRR1
	andis.	r10,r10,(HSRR1_DENORM)@h /* denorm? */
	bne+	denorm_assist
	INT_VIRT_SAVE_SRR_AND_JUMP denorm_common, EXC_HV
EXC_VIRT_END(denorm_exception, 0x5500, 0x100)
#else
EXC_VIRT_NONE(0x5500, 0x100)
#endif

INT_KVM_HANDLER denorm_exception_hv, 0x1500, EXC_HV, PACA_EXGEN, 0

#ifdef CONFIG_PPC_DENORMALISATION
TRAMP_REAL_BEGIN(denorm_assist)
BEGIN_FTR_SECTION
/*
 * To denormalise we need to move a copy of the register to itself.
 * For POWER6 do that here for all FP regs.
 */
	mfmsr	r10
	ori	r10,r10,(MSR_FP|MSR_FE0|MSR_FE1)
	xori	r10,r10,(MSR_FE0|MSR_FE1)
	mtmsrd	r10
	sync

	.Lreg=0
	.rept 32
	fmr	.Lreg,.Lreg
	.Lreg=.Lreg+1
	.endr

FTR_SECTION_ELSE
/*
 * To denormalise we need to move a copy of the register to itself.
 * For POWER7 do that here for the first 32 VSX registers only.
 */
	mfmsr	r10
	oris	r10,r10,MSR_VSX@h
	mtmsrd	r10
	sync

	.Lreg=0
	.rept 32
	XVCPSGNDP(.Lreg,.Lreg,.Lreg)
	.Lreg=.Lreg+1
	.endr

ALT_FTR_SECTION_END_IFCLR(CPU_FTR_ARCH_206)

BEGIN_FTR_SECTION
	b	denorm_done
END_FTR_SECTION_IFCLR(CPU_FTR_ARCH_207S)
/*
 * To denormalise we need to move a copy of the register to itself.
 * For POWER8 we need to do that for all 64 VSX registers
 */
	.Lreg=32
	.rept 32
	XVCPSGNDP(.Lreg,.Lreg,.Lreg)
	.Lreg=.Lreg+1
	.endr

denorm_done:
	mfspr	r11,SPRN_HSRR0
	subi	r11,r11,4
	mtspr	SPRN_HSRR0,r11
	mtcrf	0x80,r9
	ld	r9,PACA_EXGEN+EX_R9(r13)
	RESTORE_PPR_PACA(PACA_EXGEN, r10)
BEGIN_FTR_SECTION
	ld	r10,PACA_EXGEN+EX_CFAR(r13)
	mtspr	SPRN_CFAR,r10
END_FTR_SECTION_IFSET(CPU_FTR_CFAR)
	ld	r10,PACA_EXGEN+EX_R10(r13)
	ld	r11,PACA_EXGEN+EX_R11(r13)
	ld	r12,PACA_EXGEN+EX_R12(r13)
	ld	r13,PACA_EXGEN+EX_R13(r13)
	HRFI_TO_UNKNOWN
	b	.
#endif

EXC_COMMON(denorm_common, 0x1500, unknown_exception)


#ifdef CONFIG_CBE_RAS
EXC_REAL_BEGIN(cbe_maintenance, 0x1600, 0x100)
	INT_HANDLER cbe_maintenance, 0x1600, ool=1, hsrr=EXC_HV, kvm=1
EXC_REAL_END(cbe_maintenance, 0x1600, 0x100)
EXC_VIRT_NONE(0x5600, 0x100)
INT_KVM_HANDLER cbe_maintenance, 0x1600, EXC_HV, PACA_EXGEN, 1
EXC_COMMON(cbe_maintenance_common, 0x1600, cbe_maintenance_exception)
#else /* CONFIG_CBE_RAS */
EXC_REAL_NONE(0x1600, 0x100)
EXC_VIRT_NONE(0x5600, 0x100)
#endif


EXC_REAL_BEGIN(altivec_assist, 0x1700, 0x100)
	INT_HANDLER altivec_assist, 0x1700, kvm=1
EXC_REAL_END(altivec_assist, 0x1700, 0x100)
EXC_VIRT_BEGIN(altivec_assist, 0x5700, 0x100)
	INT_HANDLER altivec_assist, 0x1700, virt=1
EXC_VIRT_END(altivec_assist, 0x5700, 0x100)
INT_KVM_HANDLER altivec_assist, 0x1700, EXC_STD, PACA_EXGEN, 0
#ifdef CONFIG_ALTIVEC
EXC_COMMON(altivec_assist_common, 0x1700, altivec_assist_exception)
#else
EXC_COMMON(altivec_assist_common, 0x1700, unknown_exception)
#endif


#ifdef CONFIG_CBE_RAS
EXC_REAL_BEGIN(cbe_thermal, 0x1800, 0x100)
	INT_HANDLER cbe_thermal, 0x1800, ool=1, hsrr=EXC_HV, kvm=1
EXC_REAL_END(cbe_thermal, 0x1800, 0x100)
EXC_VIRT_NONE(0x5800, 0x100)
INT_KVM_HANDLER cbe_thermal, 0x1800, EXC_HV, PACA_EXGEN, 1
EXC_COMMON(cbe_thermal_common, 0x1800, cbe_thermal_exception)
#else /* CONFIG_CBE_RAS */
EXC_REAL_NONE(0x1800, 0x100)
EXC_VIRT_NONE(0x5800, 0x100)
#endif


#ifdef CONFIG_PPC_WATCHDOG

#define MASKED_DEC_HANDLER_LABEL 3f

#define MASKED_DEC_HANDLER(_H)				\
3: /* soft-nmi */					\
	std	r12,PACA_EXGEN+EX_R12(r13);		\
	GET_SCRATCH0(r10);				\
	std	r10,PACA_EXGEN+EX_R13(r13);		\
	INT_SAVE_SRR_AND_JUMP soft_nmi_common, _H, 1

/*
 * Branch to soft_nmi_interrupt using the emergency stack. The emergency
 * stack is one that is usable by maskable interrupts so long as MSR_EE
 * remains off. It is used for recovery when something has corrupted the
 * normal kernel stack, for example. The "soft NMI" must not use the process
 * stack because we want irq disabled sections to avoid touching the stack
 * at all (other than PMU interrupts), so use the emergency stack for this,
 * and run it entirely with interrupts hard disabled.
 */
EXC_COMMON_BEGIN(soft_nmi_common)
	mr	r10,r1
	ld	r1,PACAEMERGSP(r13)
	subi	r1,r1,INT_FRAME_SIZE
	INT_COMMON 0x900, PACA_EXGEN, 0, 1, 1, 0, 0
	bl	save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	soft_nmi_interrupt
	b	ret_from_except

#else /* CONFIG_PPC_WATCHDOG */
#define MASKED_DEC_HANDLER_LABEL 2f /* normal return */
#define MASKED_DEC_HANDLER(_H)
#endif /* CONFIG_PPC_WATCHDOG */

/*
 * An interrupt came in while soft-disabled. We set paca->irq_happened, then:
 * - If it was a decrementer interrupt, we bump the dec to max and and return.
 * - If it was a doorbell we return immediately since doorbells are edge
 *   triggered and won't automatically refire.
 * - If it was a HMI we return immediately since we handled it in realmode
 *   and it won't refire.
 * - Else it is one of PACA_IRQ_MUST_HARD_MASK, so hard disable and return.
 * This is called with r10 containing the value to OR to the paca field.
 */
.macro MASKED_INTERRUPT hsrr
	.if \hsrr
masked_Hinterrupt:
	.else
masked_interrupt:
	.endif
	std	r11,PACA_EXGEN+EX_R11(r13)
	lbz	r11,PACAIRQHAPPENED(r13)
	or	r11,r11,r10
	stb	r11,PACAIRQHAPPENED(r13)
	cmpwi	r10,PACA_IRQ_DEC
	bne	1f
	lis	r10,0x7fff
	ori	r10,r10,0xffff
	mtspr	SPRN_DEC,r10
	b	MASKED_DEC_HANDLER_LABEL
1:	andi.	r10,r10,PACA_IRQ_MUST_HARD_MASK
	beq	2f
	.if \hsrr
	mfspr	r10,SPRN_HSRR1
	xori	r10,r10,MSR_EE	/* clear MSR_EE */
	mtspr	SPRN_HSRR1,r10
	.else
	mfspr	r10,SPRN_SRR1
	xori	r10,r10,MSR_EE	/* clear MSR_EE */
	mtspr	SPRN_SRR1,r10
	.endif
	ori	r11,r11,PACA_IRQ_HARD_DIS
	stb	r11,PACAIRQHAPPENED(r13)
2:	/* done */
	mtcrf	0x80,r9
	std	r1,PACAR1(r13)
	ld	r9,PACA_EXGEN+EX_R9(r13)
	ld	r10,PACA_EXGEN+EX_R10(r13)
	ld	r11,PACA_EXGEN+EX_R11(r13)
	/* returns to kernel where r13 must be set up, so don't restore it */
	.if \hsrr
	HRFI_TO_KERNEL
	.else
	RFI_TO_KERNEL
	.endif
	b	.
	MASKED_DEC_HANDLER(\hsrr\())
.endm

TRAMP_REAL_BEGIN(stf_barrier_fallback)
	std	r9,PACA_EXRFI+EX_R9(r13)
	std	r10,PACA_EXRFI+EX_R10(r13)
	sync
	ld	r9,PACA_EXRFI+EX_R9(r13)
	ld	r10,PACA_EXRFI+EX_R10(r13)
	ori	31,31,0
	.rept 14
	b	1f
1:
	.endr
	blr

TRAMP_REAL_BEGIN(rfi_flush_fallback)
	SET_SCRATCH0(r13);
	GET_PACA(r13);
	std	r1,PACA_EXRFI+EX_R12(r13)
	ld	r1,PACAKSAVE(r13)
	std	r9,PACA_EXRFI+EX_R9(r13)
	std	r10,PACA_EXRFI+EX_R10(r13)
	std	r11,PACA_EXRFI+EX_R11(r13)
	mfctr	r9
	ld	r10,PACA_RFI_FLUSH_FALLBACK_AREA(r13)
	ld	r11,PACA_L1D_FLUSH_SIZE(r13)
	srdi	r11,r11,(7 + 3) /* 128 byte lines, unrolled 8x */
	mtctr	r11
	DCBT_BOOK3S_STOP_ALL_STREAM_IDS(r11) /* Stop prefetch streams */

	/* order ld/st prior to dcbt stop all streams with flushing */
	sync

	/*
	 * The load adresses are at staggered offsets within cachelines,
	 * which suits some pipelines better (on others it should not
	 * hurt).
	 */
1:
	ld	r11,(0x80 + 8)*0(r10)
	ld	r11,(0x80 + 8)*1(r10)
	ld	r11,(0x80 + 8)*2(r10)
	ld	r11,(0x80 + 8)*3(r10)
	ld	r11,(0x80 + 8)*4(r10)
	ld	r11,(0x80 + 8)*5(r10)
	ld	r11,(0x80 + 8)*6(r10)
	ld	r11,(0x80 + 8)*7(r10)
	addi	r10,r10,0x80*8
	bdnz	1b

	mtctr	r9
	ld	r9,PACA_EXRFI+EX_R9(r13)
	ld	r10,PACA_EXRFI+EX_R10(r13)
	ld	r11,PACA_EXRFI+EX_R11(r13)
	ld	r1,PACA_EXRFI+EX_R12(r13)
	GET_SCRATCH0(r13);
	rfid

TRAMP_REAL_BEGIN(hrfi_flush_fallback)
	SET_SCRATCH0(r13);
	GET_PACA(r13);
	std	r1,PACA_EXRFI+EX_R12(r13)
	ld	r1,PACAKSAVE(r13)
	std	r9,PACA_EXRFI+EX_R9(r13)
	std	r10,PACA_EXRFI+EX_R10(r13)
	std	r11,PACA_EXRFI+EX_R11(r13)
	mfctr	r9
	ld	r10,PACA_RFI_FLUSH_FALLBACK_AREA(r13)
	ld	r11,PACA_L1D_FLUSH_SIZE(r13)
	srdi	r11,r11,(7 + 3) /* 128 byte lines, unrolled 8x */
	mtctr	r11
	DCBT_BOOK3S_STOP_ALL_STREAM_IDS(r11) /* Stop prefetch streams */

	/* order ld/st prior to dcbt stop all streams with flushing */
	sync

	/*
	 * The load adresses are at staggered offsets within cachelines,
	 * which suits some pipelines better (on others it should not
	 * hurt).
	 */
1:
	ld	r11,(0x80 + 8)*0(r10)
	ld	r11,(0x80 + 8)*1(r10)
	ld	r11,(0x80 + 8)*2(r10)
	ld	r11,(0x80 + 8)*3(r10)
	ld	r11,(0x80 + 8)*4(r10)
	ld	r11,(0x80 + 8)*5(r10)
	ld	r11,(0x80 + 8)*6(r10)
	ld	r11,(0x80 + 8)*7(r10)
	addi	r10,r10,0x80*8
	bdnz	1b

	mtctr	r9
	ld	r9,PACA_EXRFI+EX_R9(r13)
	ld	r10,PACA_EXRFI+EX_R10(r13)
	ld	r11,PACA_EXRFI+EX_R11(r13)
	ld	r1,PACA_EXRFI+EX_R12(r13)
	GET_SCRATCH0(r13);
	hrfid

/*
 * Real mode exceptions actually use this too, but alternate
 * instruction code patches (which end up in the common .text area)
 * cannot reach these if they are put there.
 */
USE_FIXED_SECTION(virt_trampolines)
	MASKED_INTERRUPT EXC_STD
	MASKED_INTERRUPT EXC_HV

#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
TRAMP_REAL_BEGIN(kvmppc_skip_interrupt)
	/*
	 * Here all GPRs are unchanged from when the interrupt happened
	 * except for r13, which is saved in SPRG_SCRATCH0.
	 */
	mfspr	r13, SPRN_SRR0
	addi	r13, r13, 4
	mtspr	SPRN_SRR0, r13
	GET_SCRATCH0(r13)
	RFI_TO_KERNEL
	b	.

TRAMP_REAL_BEGIN(kvmppc_skip_Hinterrupt)
	/*
	 * Here all GPRs are unchanged from when the interrupt happened
	 * except for r13, which is saved in SPRG_SCRATCH0.
	 */
	mfspr	r13, SPRN_HSRR0
	addi	r13, r13, 4
	mtspr	SPRN_HSRR0, r13
	GET_SCRATCH0(r13)
	HRFI_TO_KERNEL
	b	.
#endif

/*
 * Ensure that any handlers that get invoked from the exception prologs
 * above are below the first 64KB (0x10000) of the kernel image because
 * the prologs assemble the addresses of these handlers using the
 * LOAD_HANDLER macro, which uses an ori instruction.
 */

/*** Common interrupt handlers ***/


	/*
	 * Relocation-on interrupts: A subset of the interrupts can be delivered
	 * with IR=1/DR=1, if AIL==2 and MSR.HV won't be changed by delivering
	 * it.  Addresses are the same as the original interrupt addresses, but
	 * offset by 0xc000000000004000.
	 * It's impossible to receive interrupts below 0x300 via this mechanism.
	 * KVM: None of these traps are from the guest ; anything that escalated
	 * to HV=1 from HV=0 is delivered via real mode handlers.
	 */

	/*
	 * This uses the standard macro, since the original 0x300 vector
	 * only has extra guff for STAB-based processors -- which never
	 * come here.
	 */

EXC_COMMON_BEGIN(ppc64_runlatch_on_trampoline)
	b	__ppc64_runlatch_on

USE_FIXED_SECTION(virt_trampolines)
	/*
	 * The __end_interrupts marker must be past the out-of-line (OOL)
	 * handlers, so that they are copied to real address 0x100 when running
	 * a relocatable kernel. This ensures they can be reached from the short
	 * trampoline handlers (like 0x4f00, 0x4f20, etc.) which branch
	 * directly, without using LOAD_HANDLER().
	 */
	.align	7
	.globl	__end_interrupts
__end_interrupts:
DEFINE_FIXED_SYMBOL(__end_interrupts)

#ifdef CONFIG_PPC_970_NAP
EXC_COMMON_BEGIN(power4_fixup_nap)
	andc	r9,r9,r10
	std	r9,TI_LOCAL_FLAGS(r11)
	ld	r10,_LINK(r1)		/* make idle task do the */
	std	r10,_NIP(r1)		/* equivalent of a blr */
	blr
#endif

CLOSE_FIXED_SECTION(real_vectors);
CLOSE_FIXED_SECTION(real_trampolines);
CLOSE_FIXED_SECTION(virt_vectors);
CLOSE_FIXED_SECTION(virt_trampolines);

USE_TEXT_SECTION()

/* MSR[RI] should be clear because this uses SRR[01] */
enable_machine_check:
	mflr	r0
	bcl	20,31,$+4
0:	mflr	r3
	addi	r3,r3,(1f - 0b)
	mtspr	SPRN_SRR0,r3
	mfmsr	r3
	ori	r3,r3,MSR_ME
	mtspr	SPRN_SRR1,r3
	RFI_TO_KERNEL
1:	mtlr	r0
	blr

/* MSR[RI] should be clear because this uses SRR[01] */
disable_machine_check:
	mflr	r0
	bcl	20,31,$+4
0:	mflr	r3
	addi	r3,r3,(1f - 0b)
	mtspr	SPRN_SRR0,r3
	mfmsr	r3
	li	r4,MSR_ME
	andc	r3,r3,r4
	mtspr	SPRN_SRR1,r3
	RFI_TO_KERNEL
1:	mtlr	r0
	blr

/*
 * Hash table stuff
 */
	.balign	IFETCH_ALIGN_BYTES
do_hash_page:
#ifdef CONFIG_PPC_BOOK3S_64
	lis	r0,(DSISR_BAD_FAULT_64S | DSISR_DABRMATCH | DSISR_KEYFAULT)@h
	ori	r0,r0,DSISR_BAD_FAULT_64S@l
	and.	r0,r5,r0		/* weird error? */
	bne-	handle_page_fault	/* if not, try to insert a HPTE */
	ld	r11, PACA_THREAD_INFO(r13)
	lwz	r0,TI_PREEMPT(r11)	/* If we're in an "NMI" */
	andis.	r0,r0,NMI_MASK@h	/* (i.e. an irq when soft-disabled) */
	bne	77f			/* then don't call hash_page now */

	/*
	 * r3 contains the trap number
	 * r4 contains the faulting address
	 * r5 contains dsisr
	 * r6 msr
	 *
	 * at return r3 = 0 for success, 1 for page fault, negative for error
	 */
	bl	__hash_page		/* build HPTE if possible */
        cmpdi	r3,0			/* see if __hash_page succeeded */

	/* Success */
	beq	fast_exc_return_irq	/* Return from exception on success */

	/* Error */
	blt-	13f

	/* Reload DAR/DSISR into r4/r5 for the DABR check below */
	ld	r4,_DAR(r1)
	ld      r5,_DSISR(r1)
#endif /* CONFIG_PPC_BOOK3S_64 */

/* Here we have a page fault that hash_page can't handle. */
handle_page_fault:
11:	andis.  r0,r5,DSISR_DABRMATCH@h
	bne-    handle_dabr_fault
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	do_page_fault
	cmpdi	r3,0
	beq+	ret_from_except_lite
	bl	save_nvgprs
	mr	r5,r3
	addi	r3,r1,STACK_FRAME_OVERHEAD
	ld	r4,_DAR(r1)
	bl	bad_page_fault
	b	ret_from_except

/* We have a data breakpoint exception - handle it */
handle_dabr_fault:
	bl	save_nvgprs
	ld      r4,_DAR(r1)
	ld      r5,_DSISR(r1)
	addi    r3,r1,STACK_FRAME_OVERHEAD
	bl      do_break
	/*
	 * do_break() may have changed the NV GPRS while handling a breakpoint.
	 * If so, we need to restore them with their updated values. Don't use
	 * ret_from_except_lite here.
	 */
	b       ret_from_except


#ifdef CONFIG_PPC_BOOK3S_64
/* We have a page fault that hash_page could handle but HV refused
 * the PTE insertion
 */
13:	bl	save_nvgprs
	mr	r5,r3
	addi	r3,r1,STACK_FRAME_OVERHEAD
	ld	r4,_DAR(r1)
	bl	low_hash_fault
	b	ret_from_except
#endif

/*
 * We come here as a result of a DSI at a point where we don't want
 * to call hash_page, such as when we are accessing memory (possibly
 * user memory) inside a PMU interrupt that occurred while interrupts
 * were soft-disabled.  We want to invoke the exception handler for
 * the access, or panic if there isn't a handler.
 */
77:	bl	save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r5,SIGSEGV
	bl	bad_page_fault
	b	ret_from_except

/*
 * When doorbell is triggered from system reset wakeup, the message is
 * not cleared, so it would fire again when EE is enabled.
 *
 * When coming from local_irq_enable, there may be the same problem if
 * we were hard disabled.
 *
 * Execute msgclr to clear pending exceptions before handling it.
 */
h_doorbell_common_msgclr:
	LOAD_REG_IMMEDIATE(r3, PPC_DBELL_MSGTYPE << (63-36))
	PPC_MSGCLR(3)
	b 	h_doorbell_common

doorbell_super_common_msgclr:
	LOAD_REG_IMMEDIATE(r3, PPC_DBELL_MSGTYPE << (63-36))
	PPC_MSGCLRP(3)
	b 	doorbell_super_common

/*
 * Called from arch_local_irq_enable when an interrupt needs
 * to be resent. r3 contains 0x500, 0x900, 0xa00 or 0xe80 to indicate
 * which kind of interrupt. MSR:EE is already off. We generate a
 * stackframe like if a real interrupt had happened.
 *
 * Note: While MSR:EE is off, we need to make sure that _MSR
 * in the generated frame has EE set to 1 or the exception
 * handler will not properly re-enable them.
 *
 * Note that we don't specify LR as the NIP (return address) for
 * the interrupt because that would unbalance the return branch
 * predictor.
 */
_GLOBAL(__replay_interrupt)
	/* We are going to jump to the exception common code which
	 * will retrieve various register values from the PACA which
	 * we don't give a damn about, so we don't bother storing them.
	 */
	mfmsr	r12
	LOAD_REG_ADDR(r11, replay_interrupt_return)
	mfcr	r9
	ori	r12,r12,MSR_EE
	cmpwi	r3,0x900
	beq	decrementer_common
	cmpwi	r3,0x500
BEGIN_FTR_SECTION
	beq	h_virt_irq_common
FTR_SECTION_ELSE
	beq	hardware_interrupt_common
ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_300)
	cmpwi	r3,0xf00
	beq	performance_monitor_common
BEGIN_FTR_SECTION
	cmpwi	r3,0xa00
	beq	h_doorbell_common_msgclr
	cmpwi	r3,0xe60
	beq	hmi_exception_common
FTR_SECTION_ELSE
	cmpwi	r3,0xa00
	beq	doorbell_super_common_msgclr
ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE)
replay_interrupt_return:
	blr

_ASM_NOKPROBE_SYMBOL(__replay_interrupt)
