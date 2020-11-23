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
#define EX_CTR		72
.if EX_SIZE != 10
	.error "EX_SIZE is wrong"
.endif

/*
 * Following are fixed section helper macros.
 *
 * EXC_REAL_BEGIN/END  - real, unrelocated exception vectors
 * EXC_VIRT_BEGIN/END  - virt (AIL), unrelocated exception vectors
 * TRAMP_REAL_BEGIN    - real, unrelocated helpers (virt may call these)
 * TRAMP_VIRT_BEGIN    - virt, unreloc helpers (in practice, real can use)
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

/*
 * Interrupt code generation macros
 */
#define IVEC		.L_IVEC_\name\()	/* Interrupt vector address */
#define IHSRR		.L_IHSRR_\name\()	/* Sets SRR or HSRR registers */
#define IHSRR_IF_HVMODE	.L_IHSRR_IF_HVMODE_\name\() /* HSRR if HV else SRR */
#define IAREA		.L_IAREA_\name\()	/* PACA save area */
#define IVIRT		.L_IVIRT_\name\()	/* Has virt mode entry point */
#define IISIDE		.L_IISIDE_\name\()	/* Uses SRR0/1 not DAR/DSISR */
#define IDAR		.L_IDAR_\name\()	/* Uses DAR (or SRR0) */
#define IDSISR		.L_IDSISR_\name\()	/* Uses DSISR (or SRR1) */
#define ISET_RI		.L_ISET_RI_\name\()	/* Run common code w/ MSR[RI]=1 */
#define IBRANCH_TO_COMMON	.L_IBRANCH_TO_COMMON_\name\() /* ENTRY branch to common */
#define IREALMODE_COMMON	.L_IREALMODE_COMMON_\name\() /* Common runs in realmode */
#define IMASK		.L_IMASK_\name\()	/* IRQ soft-mask bit */
#define IKVM_SKIP	.L_IKVM_SKIP_\name\()	/* Generate KVM skip handler */
#define IKVM_REAL	.L_IKVM_REAL_\name\()	/* Real entry tests KVM */
#define __IKVM_REAL(name)	.L_IKVM_REAL_ ## name
#define IKVM_VIRT	.L_IKVM_VIRT_\name\()	/* Virt entry tests KVM */
#define ISTACK		.L_ISTACK_\name\()	/* Set regular kernel stack */
#define __ISTACK(name)	.L_ISTACK_ ## name
#define IRECONCILE	.L_IRECONCILE_\name\()	/* Do RECONCILE_IRQ_STATE */
#define IKUAP		.L_IKUAP_\name\()	/* Do KUAP lock */

#define INT_DEFINE_BEGIN(n)						\
.macro int_define_ ## n name

#define INT_DEFINE_END(n)						\
.endm ;									\
int_define_ ## n n ;							\
do_define_int n

.macro do_define_int name
	.ifndef IVEC
		.error "IVEC not defined"
	.endif
	.ifndef IHSRR
		IHSRR=0
	.endif
	.ifndef IHSRR_IF_HVMODE
		IHSRR_IF_HVMODE=0
	.endif
	.ifndef IAREA
		IAREA=PACA_EXGEN
	.endif
	.ifndef IVIRT
		IVIRT=1
	.endif
	.ifndef IISIDE
		IISIDE=0
	.endif
	.ifndef IDAR
		IDAR=0
	.endif
	.ifndef IDSISR
		IDSISR=0
	.endif
	.ifndef ISET_RI
		ISET_RI=1
	.endif
	.ifndef IBRANCH_TO_COMMON
		IBRANCH_TO_COMMON=1
	.endif
	.ifndef IREALMODE_COMMON
		IREALMODE_COMMON=0
	.else
		.if ! IBRANCH_TO_COMMON
			.error "IREALMODE_COMMON=1 but IBRANCH_TO_COMMON=0"
		.endif
	.endif
	.ifndef IMASK
		IMASK=0
	.endif
	.ifndef IKVM_SKIP
		IKVM_SKIP=0
	.endif
	.ifndef IKVM_REAL
		IKVM_REAL=0
	.endif
	.ifndef IKVM_VIRT
		IKVM_VIRT=0
	.endif
	.ifndef ISTACK
		ISTACK=1
	.endif
	.ifndef IRECONCILE
		IRECONCILE=1
	.endif
	.ifndef IKUAP
		IKUAP=1
	.endif
.endm

#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
/*
 * All interrupts which set HSRR registers, as well as SRESET and MCE and
 * syscall when invoked with "sc 1" switch to MSR[HV]=1 (HVMODE) to be taken,
 * so they all generally need to test whether they were taken in guest context.
 *
 * Note: SRESET and MCE may also be sent to the guest by the hypervisor, and be
 * taken with MSR[HV]=0.
 *
 * Interrupts which set SRR registers (with the above exceptions) do not
 * elevate to MSR[HV]=1 mode, though most can be taken when running with
 * MSR[HV]=1  (e.g., bare metal kernel and userspace). So these interrupts do
 * not need to test whether a guest is running because they get delivered to
 * the guest directly, including nested HV KVM guests.
 *
 * The exception is PR KVM, where the guest runs with MSR[PR]=1 and the host
 * runs with MSR[HV]=0, so the host takes all interrupts on behalf of the
 * guest. PR KVM runs with LPCR[AIL]=0 which causes interrupts to always be
 * delivered to the real-mode entry point, therefore such interrupts only test
 * KVM in their real mode handlers, and only when PR KVM is possible.
 *
 * Interrupts that are taken in MSR[HV]=0 and escalate to MSR[HV]=1 are always
 * delivered in real-mode when the MMU is in hash mode because the MMU
 * registers are not set appropriately to translate host addresses. In nested
 * radix mode these can be delivered in virt-mode as the host translations are
 * used implicitly (see: effective LPID, effective PID).
 */

/*
 * If an interrupt is taken while a guest is running, it is immediately routed
 * to KVM to handle. If both HV and PR KVM arepossible, KVM interrupts go first
 * to kvmppc_interrupt_hv, which handles the PR guest case.
 */
#define kvmppc_interrupt kvmppc_interrupt_hv
#else
#define kvmppc_interrupt kvmppc_interrupt_pr
#endif

.macro KVMTEST name
	lbz	r10,HSTATE_IN_GUEST(r13)
	cmpwi	r10,0
	bne	\name\()_kvm
.endm

.macro GEN_KVM name
	.balign IFETCH_ALIGN_BYTES
\name\()_kvm:

	.if IKVM_SKIP
	cmpwi	r10,KVM_GUEST_MODE_SKIP
	beq	89f
	.else
BEGIN_FTR_SECTION
	ld	r10,IAREA+EX_CFAR(r13)
	std	r10,HSTATE_CFAR(r13)
END_FTR_SECTION_IFSET(CPU_FTR_CFAR)
	.endif

	ld	r10,IAREA+EX_CTR(r13)
	mtctr	r10
BEGIN_FTR_SECTION
	ld	r10,IAREA+EX_PPR(r13)
	std	r10,HSTATE_PPR(r13)
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)
	ld	r11,IAREA+EX_R11(r13)
	ld	r12,IAREA+EX_R12(r13)
	std	r12,HSTATE_SCRATCH0(r13)
	sldi	r12,r9,32
	ld	r9,IAREA+EX_R9(r13)
	ld	r10,IAREA+EX_R10(r13)
	/* HSRR variants have the 0x2 bit added to their trap number */
	.if IHSRR_IF_HVMODE
	BEGIN_FTR_SECTION
	ori	r12,r12,(IVEC + 0x2)
	FTR_SECTION_ELSE
	ori	r12,r12,(IVEC)
	ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
	.elseif IHSRR
	ori	r12,r12,(IVEC+ 0x2)
	.else
	ori	r12,r12,(IVEC)
	.endif
	b	kvmppc_interrupt

	.if IKVM_SKIP
89:	mtocrf	0x80,r9
	ld	r10,IAREA+EX_CTR(r13)
	mtctr	r10
	ld	r9,IAREA+EX_R9(r13)
	ld	r10,IAREA+EX_R10(r13)
	ld	r11,IAREA+EX_R11(r13)
	ld	r12,IAREA+EX_R12(r13)
	.if IHSRR_IF_HVMODE
	BEGIN_FTR_SECTION
	b	kvmppc_skip_Hinterrupt
	FTR_SECTION_ELSE
	b	kvmppc_skip_interrupt
	ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
	.elseif IHSRR
	b	kvmppc_skip_Hinterrupt
	.else
	b	kvmppc_skip_interrupt
	.endif
	.endif
.endm

#else
.macro KVMTEST name
.endm
.macro GEN_KVM name
.endm
#endif

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

.macro GEN_BRANCH_TO_COMMON name, virt
	.if IREALMODE_COMMON
	LOAD_HANDLER(r10, \name\()_common)
	mtctr	r10
	bctr
	.else
	.if \virt
#ifndef CONFIG_RELOCATABLE
	b	\name\()_common_virt
#else
	LOAD_HANDLER(r10, \name\()_common_virt)
	mtctr	r10
	bctr
#endif
	.else
	LOAD_HANDLER(r10, \name\()_common_real)
	mtctr	r10
	bctr
	.endif
	.endif
.endm

.macro GEN_INT_ENTRY name, virt, ool=0
	SET_SCRATCH0(r13)			/* save r13 */
	GET_PACA(r13)
	std	r9,IAREA+EX_R9(r13)		/* save r9 */
BEGIN_FTR_SECTION
	mfspr	r9,SPRN_PPR
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)
	HMT_MEDIUM
	std	r10,IAREA+EX_R10(r13)		/* save r10 - r12 */
BEGIN_FTR_SECTION
	mfspr	r10,SPRN_CFAR
END_FTR_SECTION_IFSET(CPU_FTR_CFAR)
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

BEGIN_FTR_SECTION
	std	r9,IAREA+EX_PPR(r13)
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)
BEGIN_FTR_SECTION
	std	r10,IAREA+EX_CFAR(r13)
END_FTR_SECTION_IFSET(CPU_FTR_CFAR)
	INTERRUPT_TO_KERNEL
	mfctr	r10
	std	r10,IAREA+EX_CTR(r13)
	mfcr	r9
	std	r11,IAREA+EX_R11(r13)
	std	r12,IAREA+EX_R12(r13)

	/*
	 * DAR/DSISR, SCRATCH0 must be read before setting MSR[RI],
	 * because a d-side MCE will clobber those registers so is
	 * not recoverable if they are live.
	 */
	GET_SCRATCH0(r10)
	std	r10,IAREA+EX_R13(r13)
	.if IDAR && !IISIDE
	.if IHSRR
	mfspr	r10,SPRN_HDAR
	.else
	mfspr	r10,SPRN_DAR
	.endif
	std	r10,IAREA+EX_DAR(r13)
	.endif
	.if IDSISR && !IISIDE
	.if IHSRR
	mfspr	r10,SPRN_HDSISR
	.else
	mfspr	r10,SPRN_DSISR
	.endif
	stw	r10,IAREA+EX_DSISR(r13)
	.endif

	.if IHSRR_IF_HVMODE
	BEGIN_FTR_SECTION
	mfspr	r11,SPRN_HSRR0		/* save HSRR0 */
	mfspr	r12,SPRN_HSRR1		/* and HSRR1 */
	FTR_SECTION_ELSE
	mfspr	r11,SPRN_SRR0		/* save SRR0 */
	mfspr	r12,SPRN_SRR1		/* and SRR1 */
	ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
	.elseif IHSRR
	mfspr	r11,SPRN_HSRR0		/* save HSRR0 */
	mfspr	r12,SPRN_HSRR1		/* and HSRR1 */
	.else
	mfspr	r11,SPRN_SRR0		/* save SRR0 */
	mfspr	r12,SPRN_SRR1		/* and SRR1 */
	.endif

	.if IBRANCH_TO_COMMON
	GEN_BRANCH_TO_COMMON \name \virt
	.endif

	.if \ool
	.popsection
	.endif
.endm

/*
 * __GEN_COMMON_ENTRY is required to receive the branch from interrupt
 * entry, except in the case of the real-mode handlers which require
 * __GEN_REALMODE_COMMON_ENTRY.
 *
 * This switches to virtual mode and sets MSR[RI].
 */
.macro __GEN_COMMON_ENTRY name
DEFINE_FIXED_SYMBOL(\name\()_common_real)
\name\()_common_real:
	.if IKVM_REAL
		KVMTEST \name
	.endif

	ld	r10,PACAKMSR(r13)	/* get MSR value for kernel */
	/* MSR[RI] is clear iff using SRR regs */
	.if IHSRR == EXC_HV_OR_STD
	BEGIN_FTR_SECTION
	xori	r10,r10,MSR_RI
	END_FTR_SECTION_IFCLR(CPU_FTR_HVMODE)
	.elseif ! IHSRR
	xori	r10,r10,MSR_RI
	.endif
	mtmsrd	r10

	.if IVIRT
	.if IKVM_VIRT
	b	1f /* skip the virt test coming from real */
	.endif

	.balign IFETCH_ALIGN_BYTES
DEFINE_FIXED_SYMBOL(\name\()_common_virt)
\name\()_common_virt:
	.if IKVM_VIRT
		KVMTEST \name
1:
	.endif
	.endif /* IVIRT */
.endm

/*
 * Don't switch to virt mode. Used for early MCE and HMI handlers that
 * want to run in real mode.
 */
.macro __GEN_REALMODE_COMMON_ENTRY name
DEFINE_FIXED_SYMBOL(\name\()_common_real)
\name\()_common_real:
	.if IKVM_REAL
		KVMTEST \name
	.endif
.endm

.macro __GEN_COMMON_BODY name
	.if IMASK
		.if ! ISTACK
		.error "No support for masked interrupt to use custom stack"
		.endif

		/* If coming from user, skip soft-mask tests. */
		andi.	r10,r12,MSR_PR
		bne	2f

		/* Kernel code running below __end_interrupts is implicitly
		 * soft-masked */
		LOAD_HANDLER(r10, __end_interrupts)
		cmpld	r11,r10
		li	r10,IMASK
		blt-	1f

		/* Test the soft mask state against our interrupt's bit */
		lbz	r10,PACAIRQSOFTMASK(r13)
1:		andi.	r10,r10,IMASK
		/* Associate vector numbers with bits in paca->irq_happened */
		.if IVEC == 0x500 || IVEC == 0xea0
		li	r10,PACA_IRQ_EE
		.elseif IVEC == 0x900
		li	r10,PACA_IRQ_DEC
		.elseif IVEC == 0xa00 || IVEC == 0xe80
		li	r10,PACA_IRQ_DBELL
		.elseif IVEC == 0xe60
		li	r10,PACA_IRQ_HMI
		.elseif IVEC == 0xf00
		li	r10,PACA_IRQ_PMI
		.else
		.abort "Bad maskable vector"
		.endif

		.if IHSRR_IF_HVMODE
		BEGIN_FTR_SECTION
		bne	masked_Hinterrupt
		FTR_SECTION_ELSE
		bne	masked_interrupt
		ALT_FTR_SECTION_END_IFSET(CPU_FTR_HVMODE | CPU_FTR_ARCH_206)
		.elseif IHSRR
		bne	masked_Hinterrupt
		.else
		bne	masked_interrupt
		.endif
	.endif

	.if ISTACK
	andi.	r10,r12,MSR_PR		/* See if coming from user	*/
2:	mr	r10,r1			/* Save r1			*/
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

	.if ISET_RI
	li	r10,MSR_RI
	mtmsrd	r10,1			/* Set MSR_RI */
	.endif

	.if ISTACK
	.if IKUAP
	kuap_save_amr_and_lock r9, r10, cr1, cr0
	.endif
	beq	101f			/* if from kernel mode		*/
	ACCOUNT_CPU_USER_ENTRY(r13, r9, r10)
BEGIN_FTR_SECTION
	ld	r9,IAREA+EX_PPR(r13)	/* Read PPR from paca		*/
	std	r9,_PPR(r1)
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)
101:
	.else
	.if IKUAP
	kuap_save_amr_and_lock r9, r10, cr1
	.endif
	.endif

	/* Save original regs values from save area to stack frame. */
	ld	r9,IAREA+EX_R9(r13)	/* move r9, r10 to stackframe	*/
	ld	r10,IAREA+EX_R10(r13)
	std	r9,GPR9(r1)
	std	r10,GPR10(r1)
	ld	r9,IAREA+EX_R11(r13)	/* move r11 - r13 to stackframe	*/
	ld	r10,IAREA+EX_R12(r13)
	ld	r11,IAREA+EX_R13(r13)
	std	r9,GPR11(r1)
	std	r10,GPR12(r1)
	std	r11,GPR13(r1)

	SAVE_NVGPRS(r1)

	.if IDAR
	.if IISIDE
	ld	r10,_NIP(r1)
	.else
	ld	r10,IAREA+EX_DAR(r13)
	.endif
	std	r10,_DAR(r1)
	.endif

	.if IDSISR
	.if IISIDE
	ld	r10,_MSR(r1)
	lis	r11,DSISR_SRR1_MATCH_64S@h
	and	r10,r10,r11
	.else
	lwz	r10,IAREA+EX_DSISR(r13)
	.endif
	std	r10,_DSISR(r1)
	.endif

BEGIN_FTR_SECTION
	ld	r10,IAREA+EX_CFAR(r13)
	std	r10,ORIG_GPR3(r1)
END_FTR_SECTION_IFSET(CPU_FTR_CFAR)
	ld	r10,IAREA+EX_CTR(r13)
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
	li	r9,IVEC
	std	r9,_TRAP(r1)		/* set trap number		*/
	li	r10,0
	ld	r11,exception_marker@toc(r2)
	std	r10,RESULT(r1)		/* clear regs->result		*/
	std	r11,STACK_FRAME_OVERHEAD-16(r1) /* mark the frame	*/

	.if ISTACK
	ACCOUNT_STOLEN_TIME
	.endif

	.if IRECONCILE
	RECONCILE_IRQ_STATE(r10, r11)
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
.macro GEN_COMMON name
	__GEN_COMMON_ENTRY \name
	__GEN_COMMON_BODY \name
.endm

/*
 * Restore all registers including H/SRR0/1 saved in a stack frame of a
 * standard exception.
 */
.macro EXCEPTION_RESTORE_REGS hsrr=0
	/* Move original SRR0 and SRR1 into the respective regs */
	ld	r9,_MSR(r1)
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
 * The scv instructions are a special case. They get a 0x3000 offset applied.
 * scv exceptions have unique reentrancy properties, see below.
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
 * 0x1900 - 0x2fff : Real mode trampolines
 * 0x3000 - 0x58ff : Relon (IR=1,DR=1) mode pSeries interrupt vectors
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
OPEN_FIXED_SECTION(real_trampolines,    0x1900, 0x3000)
OPEN_FIXED_SECTION(virt_vectors,        0x3000, 0x5900)
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

/**
 * Interrupt 0x3000 - System Call Vectored Interrupt (syscall).
 * This is a synchronous interrupt invoked with the "scv" instruction. The
 * system call does not alter the HV bit, so it is directed to the OS.
 *
 * Handling:
 * scv instructions enter the kernel without changing EE, RI, ME, or HV.
 * In particular, this means we can take a maskable interrupt at any point
 * in the scv handler, which is unlike any other interrupt. This is solved
 * by treating the instruction addresses below __end_interrupts as being
 * soft-masked.
 *
 * AIL-0 mode scv exceptions go to 0x17000-0x17fff, but we set AIL-3 and
 * ensure scv is never executed with relocation off, which means AIL-0
 * should never happen.
 *
 * Before leaving the below __end_interrupts text, at least of the following
 * must be true:
 * - MSR[PR]=1 (i.e., return to userspace)
 * - MSR_EE|MSR_RI is set (no reentrant exceptions)
 * - Standard kernel environment is set up (stack, paca, etc)
 *
 * Call convention:
 *
 * syscall register convention is in Documentation/powerpc/syscall64-abi.rst
 */
EXC_VIRT_BEGIN(system_call_vectored, 0x3000, 0x1000)
	/* SCV 0 */
	mr	r9,r13
	GET_PACA(r13)
	mflr	r11
	mfctr	r12
	li	r10,IRQS_ALL_DISABLED
	stb	r10,PACAIRQSOFTMASK(r13)
#ifdef CONFIG_RELOCATABLE
	b	system_call_vectored_tramp
#else
	b	system_call_vectored_common
#endif
	nop

	/* SCV 1 - 127 */
	.rept	127
	mr	r9,r13
	GET_PACA(r13)
	mflr	r11
	mfctr	r12
	li	r10,IRQS_ALL_DISABLED
	stb	r10,PACAIRQSOFTMASK(r13)
	li	r0,-1 /* cause failure */
#ifdef CONFIG_RELOCATABLE
	b	system_call_vectored_sigill_tramp
#else
	b	system_call_vectored_sigill
#endif
	.endr
EXC_VIRT_END(system_call_vectored, 0x3000, 0x1000)

#ifdef CONFIG_RELOCATABLE
TRAMP_VIRT_BEGIN(system_call_vectored_tramp)
	__LOAD_HANDLER(r10, system_call_vectored_common)
	mtctr	r10
	bctr

TRAMP_VIRT_BEGIN(system_call_vectored_sigill_tramp)
	__LOAD_HANDLER(r10, system_call_vectored_sigill)
	mtctr	r10
	bctr
#endif


/* No virt vectors corresponding with 0x0..0x100 */
EXC_VIRT_NONE(0x4000, 0x100)


/**
 * Interrupt 0x100 - System Reset Interrupt (SRESET aka NMI).
 * This is a non-maskable, asynchronous interrupt always taken in real-mode.
 * It is caused by:
 * - Wake from power-saving state, on powernv.
 * - An NMI from another CPU, triggered by firmware or hypercall.
 * - As crash/debug signal injected from BMC, firmware or hypervisor.
 *
 * Handling:
 * Power-save wakeup is the only performance critical path, so this is
 * determined quickly as possible first. In this case volatile registers
 * can be discarded and SPRs like CFAR don't need to be read.
 *
 * If not a powersave wakeup, then it's run as a regular interrupt, however
 * it uses its own stack and PACA save area to preserve the regular kernel
 * environment for debugging.
 *
 * This interrupt is not maskable, so triggering it when MSR[RI] is clear,
 * or SCRATCH0 is in use, etc. may cause a crash. It's also not entirely
 * correct to switch to virtual mode to run the regular interrupt handler
 * because it might be interrupted when the MMU is in a bad state (e.g., SLB
 * is clear).
 *
 * FWNMI:
 * PAPR specifies a "fwnmi" facility which sends the sreset to a different
 * entry point with a different register set up. Some hypervisors will
 * send the sreset to 0x100 in the guest if it is not fwnmi capable.
 *
 * KVM:
 * Unlike most SRR interrupts, this may be taken by the host while executing
 * in a guest, so a KVM test is required. KVM will pull the CPU out of guest
 * mode and then raise the sreset.
 */
INT_DEFINE_BEGIN(system_reset)
	IVEC=0x100
	IAREA=PACA_EXNMI
	IVIRT=0 /* no virt entry point */
	/*
	 * MSR_RI is not enabled, because PACA_EXNMI and nmi stack is
	 * being used, so a nested NMI exception would corrupt it.
	 */
	ISET_RI=0
	ISTACK=0
	IRECONCILE=0
	IKVM_REAL=1
INT_DEFINE_END(system_reset)

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

	GEN_INT_ENTRY system_reset, virt=0
	/*
	 * In theory, we should not enable relocation here if it was disabled
	 * in SRR1, because the MMU may not be configured to support it (e.g.,
	 * SLB may have been cleared). In practice, there should only be a few
	 * small windows where that's the case, and sreset is considered to
	 * be dangerous anyway.
	 */
EXC_REAL_END(system_reset, 0x100, 0x100)
EXC_VIRT_NONE(0x4100, 0x100)

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
	/* XXX: fwnmi guest could run a nested/PR guest, so why no test?  */
	__IKVM_REAL(system_reset)=0
	GEN_INT_ENTRY system_reset, virt=0

#endif /* CONFIG_PPC_PSERIES */

EXC_COMMON_BEGIN(system_reset_common)
	__GEN_COMMON_ENTRY system_reset
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
	__GEN_COMMON_BODY system_reset
	/*
	 * Set IRQS_ALL_DISABLED unconditionally so irqs_disabled() does
	 * the right thing. We do not want to reconcile because that goes
	 * through irq tracing which we don't want in NMI.
	 *
	 * Save PACAIRQHAPPENED to RESULT (otherwise unused), and set HARD_DIS
	 * as we are running with MSR[EE]=0.
	 */
	li	r10,IRQS_ALL_DISABLED
	stb	r10,PACAIRQSOFTMASK(r13)
	lbz	r10,PACAIRQHAPPENED(r13)
	std	r10,RESULT(r1)
	ori	r10,r10,PACA_IRQ_HARD_DIS
	stb	r10,PACAIRQHAPPENED(r13)

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
	ld	r10,RESULT(r1)
	stb	r10,PACAIRQHAPPENED(r13)
	ld	r10,SOFTE(r1)
	stb	r10,PACAIRQSOFTMASK(r13)

	kuap_restore_amr r9, r10
	EXCEPTION_RESTORE_REGS
	RFI_TO_USER_OR_KERNEL

	GEN_KVM system_reset


/**
 * Interrupt 0x200 - Machine Check Interrupt (MCE).
 * This is a non-maskable interrupt always taken in real-mode. It can be
 * synchronous or asynchronous, caused by hardware or software, and it may be
 * taken in a power-saving state.
 *
 * Handling:
 * Similarly to system reset, this uses its own stack and PACA save area,
 * the difference is re-entrancy is allowed on the machine check stack.
 *
 * machine_check_early is run in real mode, and carefully decodes the
 * machine check and tries to handle it (e.g., flush the SLB if there was an
 * error detected there), determines if it was recoverable and logs the
 * event.
 *
 * This early code does not "reconcile" irq soft-mask state like SRESET or
 * regular interrupts do, so irqs_disabled() among other things may not work
 * properly (irq disable/enable already doesn't work because irq tracing can
 * not work in real mode).
 *
 * Then, depending on the execution context when the interrupt is taken, there
 * are 3 main actions:
 * - Executing in kernel mode. The event is queued with irq_work, which means
 *   it is handled when it is next safe to do so (i.e., the kernel has enabled
 *   interrupts), which could be immediately when the interrupt returns. This
 *   avoids nasty issues like switching to virtual mode when the MMU is in a
 *   bad state, or when executing OPAL code. (SRESET is exposed to such issues,
 *   but it has different priorities). Check to see if the CPU was in power
 *   save, and return via the wake up code if it was.
 *
 * - Executing in user mode. machine_check_exception is run like a normal
 *   interrupt handler, which processes the data generated by the early handler.
 *
 * - Executing in guest mode. The interrupt is run with its KVM test, and
 *   branches to KVM to deal with. KVM may queue the event for the host
 *   to report later.
 *
 * This interrupt is not maskable, so if it triggers when MSR[RI] is clear,
 * or SCRATCH0 is in use, it may cause a crash.
 *
 * KVM:
 * See SRESET.
 */
INT_DEFINE_BEGIN(machine_check_early)
	IVEC=0x200
	IAREA=PACA_EXMC
	IVIRT=0 /* no virt entry point */
	IREALMODE_COMMON=1
	/*
	 * MSR_RI is not enabled, because PACA_EXMC is being used, so a
	 * nested machine check corrupts it. machine_check_common enables
	 * MSR_RI.
	 */
	ISET_RI=0
	ISTACK=0
	IDAR=1
	IDSISR=1
	IRECONCILE=0
	IKUAP=0 /* We don't touch AMR here, we never go to virtual mode */
INT_DEFINE_END(machine_check_early)

INT_DEFINE_BEGIN(machine_check)
	IVEC=0x200
	IAREA=PACA_EXMC
	IVIRT=0 /* no virt entry point */
	ISET_RI=0
	IDAR=1
	IDSISR=1
	IKVM_SKIP=1
	IKVM_REAL=1
INT_DEFINE_END(machine_check)

EXC_REAL_BEGIN(machine_check, 0x200, 0x100)
	GEN_INT_ENTRY machine_check_early, virt=0
EXC_REAL_END(machine_check, 0x200, 0x100)
EXC_VIRT_NONE(0x4200, 0x100)

#ifdef CONFIG_PPC_PSERIES
TRAMP_REAL_BEGIN(machine_check_fwnmi)
	/* See comment at machine_check exception, don't turn on RI */
	GEN_INT_ENTRY machine_check_early, virt=0
#endif

#define MACHINE_CHECK_HANDLER_WINDUP			\
	/* Clear MSR_RI before setting SRR0 and SRR1. */\
	li	r9,0;					\
	mtmsrd	r9,1;		/* Clear MSR_RI */	\
	/* Decrement paca->in_mce now RI is clear. */	\
	lhz	r12,PACA_IN_MCE(r13);			\
	subi	r12,r12,1;				\
	sth	r12,PACA_IN_MCE(r13);			\
	EXCEPTION_RESTORE_REGS

EXC_COMMON_BEGIN(machine_check_early_common)
	__GEN_REALMODE_COMMON_ENTRY machine_check_early

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

	__GEN_COMMON_BODY machine_check_early

BEGIN_FTR_SECTION
	bl	enable_machine_check
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE)
	li	r10,MSR_RI
	mtmsrd	r10,1

	/*
	 * Set IRQS_ALL_DISABLED and save PACAIRQHAPPENED (see
	 * system_reset_common)
	 */
	li	r10,IRQS_ALL_DISABLED
	stb	r10,PACAIRQSOFTMASK(r13)
	lbz	r10,PACAIRQHAPPENED(r13)
	std	r10,RESULT(r1)
	ori	r10,r10,PACA_IRQ_HARD_DIS
	stb	r10,PACAIRQHAPPENED(r13)

	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	machine_check_early
	std	r3,RESULT(r1)	/* Save result */
	ld	r12,_MSR(r1)

	/*
	 * Restore soft mask settings.
	 */
	ld	r10,RESULT(r1)
	stb	r10,PACAIRQHAPPENED(r13)
	ld	r10,SOFTE(r1)
	stb	r10,PACAIRQSOFTMASK(r13)

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
	GEN_INT_ENTRY machine_check, virt=0

EXC_COMMON_BEGIN(machine_check_common)
	/*
	 * Machine check is different because we use a different
	 * save area: PACA_EXMC instead of PACA_EXGEN.
	 */
	GEN_COMMON machine_check

	FINISH_NAP
	/* Enable MSR_RI when finished with PACA_EXMC */
	li	r10,MSR_RI
	mtmsrd 	r10,1
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	machine_check_exception
	b	interrupt_return

	GEN_KVM machine_check


#ifdef CONFIG_PPC_P7_NAP
/*
 * This is an idle wakeup. Low level machine check has already been
 * done. Queue the event then call the idle code to do the wake up.
 */
EXC_COMMON_BEGIN(machine_check_idle_common)
	bl	machine_check_queue_event

	/*
	 * GPR-loss wakeups are relatively straightforward, because the
	 * idle sleep code has saved all non-volatile registers on its
	 * own stack, and r1 in PACAR1.
	 *
	 * For no-loss wakeups the r1 and lr registers used by the
	 * early machine check handler have to be restored first. r2 is
	 * the kernel TOC, so no need to restore it.
	 *
	 * Then decrement MCE nesting after finishing with the stack.
	 */
	ld	r3,_MSR(r1)
	ld	r4,_LINK(r1)
	ld	r1,GPR1(r1)

	lhz	r11,PACA_IN_MCE(r13)
	subi	r11,r11,1
	sth	r11,PACA_IN_MCE(r13)

	mtlr	r4
	rlwinm	r10,r3,47-31,30,31
	cmpwi	cr1,r10,2
	bltlr	cr1	/* no state loss, return to idle caller with r3=SRR1 */
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

	lhz	r12,PACA_IN_MCE(r13)
	subi	r12,r12,1
	sth	r12,PACA_IN_MCE(r13)

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


/**
 * Interrupt 0x300 - Data Storage Interrupt (DSI).
 * This is a synchronous interrupt generated due to a data access exception,
 * e.g., a load orstore which does not have a valid page table entry with
 * permissions. DAWR matches also fault here, as do RC updates, and minor misc
 * errors e.g., copy/paste, AMO, certain invalid CI accesses, etc.
 *
 * Handling:
 * - Hash MMU
 *   Go to do_hash_page first to see if the HPT can be filled from an entry in
 *   the Linux page table. Hash faults can hit in kernel mode in a fairly
 *   arbitrary state (e.g., interrupts disabled, locks held) when accessing
 *   "non-bolted" regions, e.g., vmalloc space. However these should always be
 *   backed by Linux page tables.
 *
 *   If none is found, do a Linux page fault. Linux page faults can happen in
 *   kernel mode due to user copy operations of course.
 *
 * - Radix MMU
 *   The hardware loads from the Linux page table directly, so a fault goes
 *   immediately to Linux page fault.
 *
 * Conditions like DAWR match are handled on the way in to Linux page fault.
 */
INT_DEFINE_BEGIN(data_access)
	IVEC=0x300
	IDAR=1
	IDSISR=1
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_SKIP=1
	IKVM_REAL=1
#endif
INT_DEFINE_END(data_access)

EXC_REAL_BEGIN(data_access, 0x300, 0x80)
	GEN_INT_ENTRY data_access, virt=0
EXC_REAL_END(data_access, 0x300, 0x80)
EXC_VIRT_BEGIN(data_access, 0x4300, 0x80)
	GEN_INT_ENTRY data_access, virt=1
EXC_VIRT_END(data_access, 0x4300, 0x80)
EXC_COMMON_BEGIN(data_access_common)
	GEN_COMMON data_access
	ld	r4,_DAR(r1)
	ld	r5,_DSISR(r1)
BEGIN_MMU_FTR_SECTION
	ld	r6,_MSR(r1)
	li	r3,0x300
	b	do_hash_page		/* Try to handle as hpte fault */
MMU_FTR_SECTION_ELSE
	b	handle_page_fault
ALT_MMU_FTR_SECTION_END_IFCLR(MMU_FTR_TYPE_RADIX)

	GEN_KVM data_access


/**
 * Interrupt 0x380 - Data Segment Interrupt (DSLB).
 * This is a synchronous interrupt in response to an MMU fault missing SLB
 * entry for HPT, or an address outside RPT translation range.
 *
 * Handling:
 * - HPT:
 *   This refills the SLB, or reports an access fault similarly to a bad page
 *   fault. When coming from user-mode, the SLB handler may access any kernel
 *   data, though it may itself take a DSLB. When coming from kernel mode,
 *   recursive faults must be avoided so access is restricted to the kernel
 *   image text/data, kernel stack, and any data allocated below
 *   ppc64_bolted_size (first segment). The kernel handler must avoid stomping
 *   on user-handler data structures.
 *
 * A dedicated save area EXSLB is used (XXX: but it actually need not be
 * these days, we could use EXGEN).
 */
INT_DEFINE_BEGIN(data_access_slb)
	IVEC=0x380
	IAREA=PACA_EXSLB
	IRECONCILE=0
	IDAR=1
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_SKIP=1
	IKVM_REAL=1
#endif
INT_DEFINE_END(data_access_slb)

EXC_REAL_BEGIN(data_access_slb, 0x380, 0x80)
	GEN_INT_ENTRY data_access_slb, virt=0
EXC_REAL_END(data_access_slb, 0x380, 0x80)
EXC_VIRT_BEGIN(data_access_slb, 0x4380, 0x80)
	GEN_INT_ENTRY data_access_slb, virt=1
EXC_VIRT_END(data_access_slb, 0x4380, 0x80)
EXC_COMMON_BEGIN(data_access_slb_common)
	GEN_COMMON data_access_slb
	ld	r4,_DAR(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
BEGIN_MMU_FTR_SECTION
	/* HPT case, do SLB fault */
	bl	do_slb_fault
	cmpdi	r3,0
	bne-	1f
	b	fast_interrupt_return
1:	/* Error case */
MMU_FTR_SECTION_ELSE
	/* Radix case, access is outside page table range */
	li	r3,-EFAULT
ALT_MMU_FTR_SECTION_END_IFCLR(MMU_FTR_TYPE_RADIX)
	std	r3,RESULT(r1)
	RECONCILE_IRQ_STATE(r10, r11)
	ld	r4,_DAR(r1)
	ld	r5,RESULT(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	do_bad_slb_fault
	b	interrupt_return

	GEN_KVM data_access_slb


/**
 * Interrupt 0x400 - Instruction Storage Interrupt (ISI).
 * This is a synchronous interrupt in response to an MMU fault due to an
 * instruction fetch.
 *
 * Handling:
 * Similar to DSI, though in response to fetch. The faulting address is found
 * in SRR0 (rather than DAR), and status in SRR1 (rather than DSISR).
 */
INT_DEFINE_BEGIN(instruction_access)
	IVEC=0x400
	IISIDE=1
	IDAR=1
	IDSISR=1
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_REAL=1
#endif
INT_DEFINE_END(instruction_access)

EXC_REAL_BEGIN(instruction_access, 0x400, 0x80)
	GEN_INT_ENTRY instruction_access, virt=0
EXC_REAL_END(instruction_access, 0x400, 0x80)
EXC_VIRT_BEGIN(instruction_access, 0x4400, 0x80)
	GEN_INT_ENTRY instruction_access, virt=1
EXC_VIRT_END(instruction_access, 0x4400, 0x80)
EXC_COMMON_BEGIN(instruction_access_common)
	GEN_COMMON instruction_access
	ld	r4,_DAR(r1)
	ld	r5,_DSISR(r1)
BEGIN_MMU_FTR_SECTION
	ld      r6,_MSR(r1)
	li	r3,0x400
	b	do_hash_page		/* Try to handle as hpte fault */
MMU_FTR_SECTION_ELSE
	b	handle_page_fault
ALT_MMU_FTR_SECTION_END_IFCLR(MMU_FTR_TYPE_RADIX)

	GEN_KVM instruction_access


/**
 * Interrupt 0x480 - Instruction Segment Interrupt (ISLB).
 * This is a synchronous interrupt in response to an MMU fault due to an
 * instruction fetch.
 *
 * Handling:
 * Similar to DSLB, though in response to fetch. The faulting address is found
 * in SRR0 (rather than DAR).
 */
INT_DEFINE_BEGIN(instruction_access_slb)
	IVEC=0x480
	IAREA=PACA_EXSLB
	IRECONCILE=0
	IISIDE=1
	IDAR=1
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_REAL=1
#endif
INT_DEFINE_END(instruction_access_slb)

EXC_REAL_BEGIN(instruction_access_slb, 0x480, 0x80)
	GEN_INT_ENTRY instruction_access_slb, virt=0
EXC_REAL_END(instruction_access_slb, 0x480, 0x80)
EXC_VIRT_BEGIN(instruction_access_slb, 0x4480, 0x80)
	GEN_INT_ENTRY instruction_access_slb, virt=1
EXC_VIRT_END(instruction_access_slb, 0x4480, 0x80)
EXC_COMMON_BEGIN(instruction_access_slb_common)
	GEN_COMMON instruction_access_slb
	ld	r4,_DAR(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
BEGIN_MMU_FTR_SECTION
	/* HPT case, do SLB fault */
	bl	do_slb_fault
	cmpdi	r3,0
	bne-	1f
	b	fast_interrupt_return
1:	/* Error case */
MMU_FTR_SECTION_ELSE
	/* Radix case, access is outside page table range */
	li	r3,-EFAULT
ALT_MMU_FTR_SECTION_END_IFCLR(MMU_FTR_TYPE_RADIX)
	std	r3,RESULT(r1)
	RECONCILE_IRQ_STATE(r10, r11)
	ld	r4,_DAR(r1)
	ld	r5,RESULT(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	do_bad_slb_fault
	b	interrupt_return

	GEN_KVM instruction_access_slb


/**
 * Interrupt 0x500 - External Interrupt.
 * This is an asynchronous maskable interrupt in response to an "external
 * exception" from the interrupt controller or hypervisor (e.g., device
 * interrupt). It is maskable in hardware by clearing MSR[EE], and
 * soft-maskable with IRQS_DISABLED mask (i.e., local_irq_disable()).
 *
 * When running in HV mode, Linux sets up the LPCR[LPES] bit such that
 * interrupts are delivered with HSRR registers, guests use SRRs, which
 * reqiures IHSRR_IF_HVMODE.
 *
 * On bare metal POWER9 and later, Linux sets the LPCR[HVICE] bit such that
 * external interrupts are delivered as Hypervisor Virtualization Interrupts
 * rather than External Interrupts.
 *
 * Handling:
 * This calls into Linux IRQ handler. NVGPRs are not saved to reduce overhead,
 * because registers at the time of the interrupt are not so important as it is
 * asynchronous.
 *
 * If soft masked, the masked handler will note the pending interrupt for
 * replay, and clear MSR[EE] in the interrupted context.
 */
INT_DEFINE_BEGIN(hardware_interrupt)
	IVEC=0x500
	IHSRR_IF_HVMODE=1
	IMASK=IRQS_DISABLED
	IKVM_REAL=1
	IKVM_VIRT=1
INT_DEFINE_END(hardware_interrupt)

EXC_REAL_BEGIN(hardware_interrupt, 0x500, 0x100)
	GEN_INT_ENTRY hardware_interrupt, virt=0
EXC_REAL_END(hardware_interrupt, 0x500, 0x100)
EXC_VIRT_BEGIN(hardware_interrupt, 0x4500, 0x100)
	GEN_INT_ENTRY hardware_interrupt, virt=1
EXC_VIRT_END(hardware_interrupt, 0x4500, 0x100)
EXC_COMMON_BEGIN(hardware_interrupt_common)
	GEN_COMMON hardware_interrupt
	FINISH_NAP
	RUNLATCH_ON
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	do_IRQ
	b	interrupt_return

	GEN_KVM hardware_interrupt


/**
 * Interrupt 0x600 - Alignment Interrupt
 * This is a synchronous interrupt in response to data alignment fault.
 */
INT_DEFINE_BEGIN(alignment)
	IVEC=0x600
	IDAR=1
	IDSISR=1
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_REAL=1
#endif
INT_DEFINE_END(alignment)

EXC_REAL_BEGIN(alignment, 0x600, 0x100)
	GEN_INT_ENTRY alignment, virt=0
EXC_REAL_END(alignment, 0x600, 0x100)
EXC_VIRT_BEGIN(alignment, 0x4600, 0x100)
	GEN_INT_ENTRY alignment, virt=1
EXC_VIRT_END(alignment, 0x4600, 0x100)
EXC_COMMON_BEGIN(alignment_common)
	GEN_COMMON alignment
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	alignment_exception
	REST_NVGPRS(r1) /* instruction emulation may change GPRs */
	b	interrupt_return

	GEN_KVM alignment


/**
 * Interrupt 0x700 - Program Interrupt (program check).
 * This is a synchronous interrupt in response to various instruction faults:
 * traps, privilege errors, TM errors, floating point exceptions.
 *
 * Handling:
 * This interrupt may use the "emergency stack" in some cases when being taken
 * from kernel context, which complicates handling.
 */
INT_DEFINE_BEGIN(program_check)
	IVEC=0x700
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_REAL=1
#endif
INT_DEFINE_END(program_check)

EXC_REAL_BEGIN(program_check, 0x700, 0x100)
	GEN_INT_ENTRY program_check, virt=0
EXC_REAL_END(program_check, 0x700, 0x100)
EXC_VIRT_BEGIN(program_check, 0x4700, 0x100)
	GEN_INT_ENTRY program_check, virt=1
EXC_VIRT_END(program_check, 0x4700, 0x100)
EXC_COMMON_BEGIN(program_check_common)
	__GEN_COMMON_ENTRY program_check

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
	__ISTACK(program_check)=0
	__GEN_COMMON_BODY program_check
	b 3f
2:
	__ISTACK(program_check)=1
	__GEN_COMMON_BODY program_check
3:
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	program_check_exception
	REST_NVGPRS(r1) /* instruction emulation may change GPRs */
	b	interrupt_return

	GEN_KVM program_check


/*
 * Interrupt 0x800 - Floating-Point Unavailable Interrupt.
 * This is a synchronous interrupt in response to executing an fp instruction
 * with MSR[FP]=0.
 *
 * Handling:
 * This will load FP registers and enable the FP bit if coming from userspace,
 * otherwise report a bad kernel use of FP.
 */
INT_DEFINE_BEGIN(fp_unavailable)
	IVEC=0x800
	IRECONCILE=0
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_REAL=1
#endif
INT_DEFINE_END(fp_unavailable)

EXC_REAL_BEGIN(fp_unavailable, 0x800, 0x100)
	GEN_INT_ENTRY fp_unavailable, virt=0
EXC_REAL_END(fp_unavailable, 0x800, 0x100)
EXC_VIRT_BEGIN(fp_unavailable, 0x4800, 0x100)
	GEN_INT_ENTRY fp_unavailable, virt=1
EXC_VIRT_END(fp_unavailable, 0x4800, 0x100)
EXC_COMMON_BEGIN(fp_unavailable_common)
	GEN_COMMON fp_unavailable
	bne	1f			/* if from user, just load it up */
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
	b	fast_interrupt_return
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
2:	/* User process was in a transaction */
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	fp_unavailable_tm
	b	interrupt_return
#endif

	GEN_KVM fp_unavailable


/**
 * Interrupt 0x900 - Decrementer Interrupt.
 * This is an asynchronous interrupt in response to a decrementer exception
 * (e.g., DEC has wrapped below zero). It is maskable in hardware by clearing
 * MSR[EE], and soft-maskable with IRQS_DISABLED mask (i.e.,
 * local_irq_disable()).
 *
 * Handling:
 * This calls into Linux timer handler. NVGPRs are not saved (see 0x500).
 *
 * If soft masked, the masked handler will note the pending interrupt for
 * replay, and bump the decrementer to a high value, leaving MSR[EE] enabled
 * in the interrupted context.
 * If PPC_WATCHDOG is configured, the soft masked handler will actually set
 * things back up to run soft_nmi_interrupt as a regular interrupt handler
 * on the emergency stack.
 */
INT_DEFINE_BEGIN(decrementer)
	IVEC=0x900
	IMASK=IRQS_DISABLED
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_REAL=1
#endif
INT_DEFINE_END(decrementer)

EXC_REAL_BEGIN(decrementer, 0x900, 0x80)
	GEN_INT_ENTRY decrementer, virt=0
EXC_REAL_END(decrementer, 0x900, 0x80)
EXC_VIRT_BEGIN(decrementer, 0x4900, 0x80)
	GEN_INT_ENTRY decrementer, virt=1
EXC_VIRT_END(decrementer, 0x4900, 0x80)
EXC_COMMON_BEGIN(decrementer_common)
	GEN_COMMON decrementer
	FINISH_NAP
	RUNLATCH_ON
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	timer_interrupt
	b	interrupt_return

	GEN_KVM decrementer


/**
 * Interrupt 0x980 - Hypervisor Decrementer Interrupt.
 * This is an asynchronous interrupt, similar to 0x900 but for the HDEC
 * register.
 *
 * Handling:
 * Linux does not use this outside KVM where it's used to keep a host timer
 * while the guest is given control of DEC. It should normally be caught by
 * the KVM test and routed there.
 */
INT_DEFINE_BEGIN(hdecrementer)
	IVEC=0x980
	IHSRR=1
	ISTACK=0
	IRECONCILE=0
	IKVM_REAL=1
	IKVM_VIRT=1
INT_DEFINE_END(hdecrementer)

EXC_REAL_BEGIN(hdecrementer, 0x980, 0x80)
	GEN_INT_ENTRY hdecrementer, virt=0
EXC_REAL_END(hdecrementer, 0x980, 0x80)
EXC_VIRT_BEGIN(hdecrementer, 0x4980, 0x80)
	GEN_INT_ENTRY hdecrementer, virt=1
EXC_VIRT_END(hdecrementer, 0x4980, 0x80)
EXC_COMMON_BEGIN(hdecrementer_common)
	__GEN_COMMON_ENTRY hdecrementer
	/*
	 * Hypervisor decrementer interrupts not caught by the KVM test
	 * shouldn't occur but are sometimes left pending on exit from a KVM
	 * guest.  We don't need to do anything to clear them, as they are
	 * edge-triggered.
	 *
	 * Be careful to avoid touching the kernel stack.
	 */
	ld	r10,PACA_EXGEN+EX_CTR(r13)
	mtctr	r10
	mtcrf	0x80,r9
	ld	r9,PACA_EXGEN+EX_R9(r13)
	ld	r10,PACA_EXGEN+EX_R10(r13)
	ld	r11,PACA_EXGEN+EX_R11(r13)
	ld	r12,PACA_EXGEN+EX_R12(r13)
	ld	r13,PACA_EXGEN+EX_R13(r13)
	HRFI_TO_KERNEL

	GEN_KVM hdecrementer


/**
 * Interrupt 0xa00 - Directed Privileged Doorbell Interrupt.
 * This is an asynchronous interrupt in response to a msgsndp doorbell.
 * It is maskable in hardware by clearing MSR[EE], and soft-maskable with
 * IRQS_DISABLED mask (i.e., local_irq_disable()).
 *
 * Handling:
 * Guests may use this for IPIs between threads in a core if the
 * hypervisor supports it. NVGPRS are not saved (see 0x500).
 *
 * If soft masked, the masked handler will note the pending interrupt for
 * replay, leaving MSR[EE] enabled in the interrupted context because the
 * doorbells are edge triggered.
 */
INT_DEFINE_BEGIN(doorbell_super)
	IVEC=0xa00
	IMASK=IRQS_DISABLED
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_REAL=1
#endif
INT_DEFINE_END(doorbell_super)

EXC_REAL_BEGIN(doorbell_super, 0xa00, 0x100)
	GEN_INT_ENTRY doorbell_super, virt=0
EXC_REAL_END(doorbell_super, 0xa00, 0x100)
EXC_VIRT_BEGIN(doorbell_super, 0x4a00, 0x100)
	GEN_INT_ENTRY doorbell_super, virt=1
EXC_VIRT_END(doorbell_super, 0x4a00, 0x100)
EXC_COMMON_BEGIN(doorbell_super_common)
	GEN_COMMON doorbell_super
	FINISH_NAP
	RUNLATCH_ON
	addi	r3,r1,STACK_FRAME_OVERHEAD
#ifdef CONFIG_PPC_DOORBELL
	bl	doorbell_exception
#else
	bl	unknown_exception
#endif
	b	interrupt_return

	GEN_KVM doorbell_super


EXC_REAL_NONE(0xb00, 0x100)
EXC_VIRT_NONE(0x4b00, 0x100)

/**
 * Interrupt 0xc00 - System Call Interrupt (syscall, hcall).
 * This is a synchronous interrupt invoked with the "sc" instruction. The
 * system call is invoked with "sc 0" and does not alter the HV bit, so it
 * is directed to the currently running OS. The hypercall is invoked with
 * "sc 1" and it sets HV=1, so it elevates to hypervisor.
 *
 * In HPT, sc 1 always goes to 0xc00 real mode. In RADIX, sc 1 can go to
 * 0x4c00 virtual mode.
 *
 * Handling:
 * If the KVM test fires then it was due to a hypercall and is accordingly
 * routed to KVM. Otherwise this executes a normal Linux system call.
 *
 * Call convention:
 *
 * syscall and hypercalls register conventions are documented in
 * Documentation/powerpc/syscall64-abi.rst and
 * Documentation/powerpc/papr_hcalls.rst respectively.
 *
 * The intersection of volatile registers that don't contain possible
 * inputs is: cr0, xer, ctr. We may use these as scratch regs upon entry
 * without saving, though xer is not a good idea to use, as hardware may
 * interpret some bits so it may be costly to change them.
 */
INT_DEFINE_BEGIN(system_call)
	IVEC=0xc00
	IKVM_REAL=1
	IKVM_VIRT=1
INT_DEFINE_END(system_call)

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
	KVMTEST system_call /* uses r10, branch to system_call_kvm */
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
TRAMP_REAL_BEGIN(system_call_kvm)
	/*
	 * This is a hcall, so register convention is as above, with these
	 * differences:
	 * r13 = PACA
	 * ctr = orig r13
	 * orig r10 saved in PACA
	 */
	 /*
	  * Save the PPR (on systems that support it) before changing to
	  * HMT_MEDIUM. That allows the KVM code to save that value into the
	  * guest state (it is the guest's PPR value).
	  */
BEGIN_FTR_SECTION
	mfspr	r10,SPRN_PPR
	std	r10,HSTATE_PPR(r13)
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)
	HMT_MEDIUM
	mfctr	r10
	SET_SCRATCH0(r10)
	mfcr	r10
	std	r12,HSTATE_SCRATCH0(r13)
	sldi	r12,r10,32
	ori	r12,r12,0xc00
#ifdef CONFIG_RELOCATABLE
	/*
	 * Requires __LOAD_FAR_HANDLER beause kvmppc_interrupt lives
	 * outside the head section.
	 */
	__LOAD_FAR_HANDLER(r10, kvmppc_interrupt)
	mtctr   r10
	ld	r10,PACA_EXGEN+EX_R10(r13)
	bctr
#else
	ld	r10,PACA_EXGEN+EX_R10(r13)
	b       kvmppc_interrupt
#endif
#endif


/**
 * Interrupt 0xd00 - Trace Interrupt.
 * This is a synchronous interrupt in response to instruction step or
 * breakpoint faults.
 */
INT_DEFINE_BEGIN(single_step)
	IVEC=0xd00
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_REAL=1
#endif
INT_DEFINE_END(single_step)

EXC_REAL_BEGIN(single_step, 0xd00, 0x100)
	GEN_INT_ENTRY single_step, virt=0
EXC_REAL_END(single_step, 0xd00, 0x100)
EXC_VIRT_BEGIN(single_step, 0x4d00, 0x100)
	GEN_INT_ENTRY single_step, virt=1
EXC_VIRT_END(single_step, 0x4d00, 0x100)
EXC_COMMON_BEGIN(single_step_common)
	GEN_COMMON single_step
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	single_step_exception
	b	interrupt_return

	GEN_KVM single_step


/**
 * Interrupt 0xe00 - Hypervisor Data Storage Interrupt (HDSI).
 * This is a synchronous interrupt in response to an MMU fault caused by a
 * guest data access.
 *
 * Handling:
 * This should always get routed to KVM. In radix MMU mode, this is caused
 * by a guest nested radix access that can't be performed due to the
 * partition scope page table. In hash mode, this can be caused by guests
 * running with translation disabled (virtual real mode) or with VPM enabled.
 * KVM will update the page table structures or disallow the access.
 */
INT_DEFINE_BEGIN(h_data_storage)
	IVEC=0xe00
	IHSRR=1
	IDAR=1
	IDSISR=1
	IKVM_SKIP=1
	IKVM_REAL=1
	IKVM_VIRT=1
INT_DEFINE_END(h_data_storage)

EXC_REAL_BEGIN(h_data_storage, 0xe00, 0x20)
	GEN_INT_ENTRY h_data_storage, virt=0, ool=1
EXC_REAL_END(h_data_storage, 0xe00, 0x20)
EXC_VIRT_BEGIN(h_data_storage, 0x4e00, 0x20)
	GEN_INT_ENTRY h_data_storage, virt=1, ool=1
EXC_VIRT_END(h_data_storage, 0x4e00, 0x20)
EXC_COMMON_BEGIN(h_data_storage_common)
	GEN_COMMON h_data_storage
	addi    r3,r1,STACK_FRAME_OVERHEAD
BEGIN_MMU_FTR_SECTION
	ld	r4,_DAR(r1)
	li	r5,SIGSEGV
	bl      bad_page_fault
MMU_FTR_SECTION_ELSE
	bl      unknown_exception
ALT_MMU_FTR_SECTION_END_IFSET(MMU_FTR_TYPE_RADIX)
	b       interrupt_return

	GEN_KVM h_data_storage


/**
 * Interrupt 0xe20 - Hypervisor Instruction Storage Interrupt (HISI).
 * This is a synchronous interrupt in response to an MMU fault caused by a
 * guest instruction fetch, similar to HDSI.
 */
INT_DEFINE_BEGIN(h_instr_storage)
	IVEC=0xe20
	IHSRR=1
	IKVM_REAL=1
	IKVM_VIRT=1
INT_DEFINE_END(h_instr_storage)

EXC_REAL_BEGIN(h_instr_storage, 0xe20, 0x20)
	GEN_INT_ENTRY h_instr_storage, virt=0, ool=1
EXC_REAL_END(h_instr_storage, 0xe20, 0x20)
EXC_VIRT_BEGIN(h_instr_storage, 0x4e20, 0x20)
	GEN_INT_ENTRY h_instr_storage, virt=1, ool=1
EXC_VIRT_END(h_instr_storage, 0x4e20, 0x20)
EXC_COMMON_BEGIN(h_instr_storage_common)
	GEN_COMMON h_instr_storage
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	unknown_exception
	b	interrupt_return

	GEN_KVM h_instr_storage


/**
 * Interrupt 0xe40 - Hypervisor Emulation Assistance Interrupt.
 */
INT_DEFINE_BEGIN(emulation_assist)
	IVEC=0xe40
	IHSRR=1
	IKVM_REAL=1
	IKVM_VIRT=1
INT_DEFINE_END(emulation_assist)

EXC_REAL_BEGIN(emulation_assist, 0xe40, 0x20)
	GEN_INT_ENTRY emulation_assist, virt=0, ool=1
EXC_REAL_END(emulation_assist, 0xe40, 0x20)
EXC_VIRT_BEGIN(emulation_assist, 0x4e40, 0x20)
	GEN_INT_ENTRY emulation_assist, virt=1, ool=1
EXC_VIRT_END(emulation_assist, 0x4e40, 0x20)
EXC_COMMON_BEGIN(emulation_assist_common)
	GEN_COMMON emulation_assist
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	emulation_assist_interrupt
	REST_NVGPRS(r1) /* instruction emulation may change GPRs */
	b	interrupt_return

	GEN_KVM emulation_assist


/**
 * Interrupt 0xe60 - Hypervisor Maintenance Interrupt (HMI).
 * This is an asynchronous interrupt caused by a Hypervisor Maintenance
 * Exception. It is always taken in real mode but uses HSRR registers
 * unlike SRESET and MCE.
 *
 * It is maskable in hardware by clearing MSR[EE], and partially soft-maskable
 * with IRQS_DISABLED mask (i.e., local_irq_disable()).
 *
 * Handling:
 * This is a special case, this is handled similarly to machine checks, with an
 * initial real mode handler that is not soft-masked, which attempts to fix the
 * problem. Then a regular handler which is soft-maskable and reports the
 * problem.
 *
 * The emergency stack is used for the early real mode handler.
 *
 * XXX: unclear why MCE and HMI schemes could not be made common, e.g.,
 * either use soft-masking for the MCE, or use irq_work for the HMI.
 *
 * KVM:
 * Unlike MCE, this calls into KVM without calling the real mode handler
 * first.
 */
INT_DEFINE_BEGIN(hmi_exception_early)
	IVEC=0xe60
	IHSRR=1
	IREALMODE_COMMON=1
	ISTACK=0
	IRECONCILE=0
	IKUAP=0 /* We don't touch AMR here, we never go to virtual mode */
	IKVM_REAL=1
INT_DEFINE_END(hmi_exception_early)

INT_DEFINE_BEGIN(hmi_exception)
	IVEC=0xe60
	IHSRR=1
	IMASK=IRQS_DISABLED
	IKVM_REAL=1
INT_DEFINE_END(hmi_exception)

EXC_REAL_BEGIN(hmi_exception, 0xe60, 0x20)
	GEN_INT_ENTRY hmi_exception_early, virt=0, ool=1
EXC_REAL_END(hmi_exception, 0xe60, 0x20)
EXC_VIRT_NONE(0x4e60, 0x20)

EXC_COMMON_BEGIN(hmi_exception_early_common)
	__GEN_REALMODE_COMMON_ENTRY hmi_exception_early

	mr	r10,r1			/* Save r1 */
	ld	r1,PACAEMERGSP(r13)	/* Use emergency stack for realmode */
	subi	r1,r1,INT_FRAME_SIZE	/* alloc stack frame		*/

	__GEN_COMMON_BODY hmi_exception_early

	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	hmi_exception_realmode
	cmpdi	cr0,r3,0
	bne	1f

	EXCEPTION_RESTORE_REGS hsrr=1
	HRFI_TO_USER_OR_KERNEL

1:
	/*
	 * Go to virtual mode and pull the HMI event information from
	 * firmware.
	 */
	EXCEPTION_RESTORE_REGS hsrr=1
	GEN_INT_ENTRY hmi_exception, virt=0

	GEN_KVM hmi_exception_early

EXC_COMMON_BEGIN(hmi_exception_common)
	GEN_COMMON hmi_exception
	FINISH_NAP
	RUNLATCH_ON
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	handle_hmi_exception
	b	interrupt_return

	GEN_KVM hmi_exception


/**
 * Interrupt 0xe80 - Directed Hypervisor Doorbell Interrupt.
 * This is an asynchronous interrupt in response to a msgsnd doorbell.
 * Similar to the 0xa00 doorbell but for host rather than guest.
 */
INT_DEFINE_BEGIN(h_doorbell)
	IVEC=0xe80
	IHSRR=1
	IMASK=IRQS_DISABLED
	IKVM_REAL=1
	IKVM_VIRT=1
INT_DEFINE_END(h_doorbell)

EXC_REAL_BEGIN(h_doorbell, 0xe80, 0x20)
	GEN_INT_ENTRY h_doorbell, virt=0, ool=1
EXC_REAL_END(h_doorbell, 0xe80, 0x20)
EXC_VIRT_BEGIN(h_doorbell, 0x4e80, 0x20)
	GEN_INT_ENTRY h_doorbell, virt=1, ool=1
EXC_VIRT_END(h_doorbell, 0x4e80, 0x20)
EXC_COMMON_BEGIN(h_doorbell_common)
	GEN_COMMON h_doorbell
	FINISH_NAP
	RUNLATCH_ON
	addi	r3,r1,STACK_FRAME_OVERHEAD
#ifdef CONFIG_PPC_DOORBELL
	bl	doorbell_exception
#else
	bl	unknown_exception
#endif
	b	interrupt_return

	GEN_KVM h_doorbell


/**
 * Interrupt 0xea0 - Hypervisor Virtualization Interrupt.
 * This is an asynchronous interrupt in response to an "external exception".
 * Similar to 0x500 but for host only.
 */
INT_DEFINE_BEGIN(h_virt_irq)
	IVEC=0xea0
	IHSRR=1
	IMASK=IRQS_DISABLED
	IKVM_REAL=1
	IKVM_VIRT=1
INT_DEFINE_END(h_virt_irq)

EXC_REAL_BEGIN(h_virt_irq, 0xea0, 0x20)
	GEN_INT_ENTRY h_virt_irq, virt=0, ool=1
EXC_REAL_END(h_virt_irq, 0xea0, 0x20)
EXC_VIRT_BEGIN(h_virt_irq, 0x4ea0, 0x20)
	GEN_INT_ENTRY h_virt_irq, virt=1, ool=1
EXC_VIRT_END(h_virt_irq, 0x4ea0, 0x20)
EXC_COMMON_BEGIN(h_virt_irq_common)
	GEN_COMMON h_virt_irq
	FINISH_NAP
	RUNLATCH_ON
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	do_IRQ
	b	interrupt_return

	GEN_KVM h_virt_irq


EXC_REAL_NONE(0xec0, 0x20)
EXC_VIRT_NONE(0x4ec0, 0x20)
EXC_REAL_NONE(0xee0, 0x20)
EXC_VIRT_NONE(0x4ee0, 0x20)


/*
 * Interrupt 0xf00 - Performance Monitor Interrupt (PMI, PMU).
 * This is an asynchronous interrupt in response to a PMU exception.
 * It is maskable in hardware by clearing MSR[EE], and soft-maskable with
 * IRQS_PMI_DISABLED mask (NOTE: NOT local_irq_disable()).
 *
 * Handling:
 * This calls into the perf subsystem.
 *
 * Like the watchdog soft-nmi, it appears an NMI interrupt to Linux, in that it
 * runs under local_irq_disable. However it may be soft-masked in
 * powerpc-specific code.
 *
 * If soft masked, the masked handler will note the pending interrupt for
 * replay, and clear MSR[EE] in the interrupted context.
 */
INT_DEFINE_BEGIN(performance_monitor)
	IVEC=0xf00
	IMASK=IRQS_PMI_DISABLED
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_REAL=1
#endif
INT_DEFINE_END(performance_monitor)

EXC_REAL_BEGIN(performance_monitor, 0xf00, 0x20)
	GEN_INT_ENTRY performance_monitor, virt=0, ool=1
EXC_REAL_END(performance_monitor, 0xf00, 0x20)
EXC_VIRT_BEGIN(performance_monitor, 0x4f00, 0x20)
	GEN_INT_ENTRY performance_monitor, virt=1, ool=1
EXC_VIRT_END(performance_monitor, 0x4f00, 0x20)
EXC_COMMON_BEGIN(performance_monitor_common)
	GEN_COMMON performance_monitor
	FINISH_NAP
	RUNLATCH_ON
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	performance_monitor_exception
	b	interrupt_return

	GEN_KVM performance_monitor


/**
 * Interrupt 0xf20 - Vector Unavailable Interrupt.
 * This is a synchronous interrupt in response to
 * executing a vector (or altivec) instruction with MSR[VEC]=0.
 * Similar to FP unavailable.
 */
INT_DEFINE_BEGIN(altivec_unavailable)
	IVEC=0xf20
	IRECONCILE=0
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_REAL=1
#endif
INT_DEFINE_END(altivec_unavailable)

EXC_REAL_BEGIN(altivec_unavailable, 0xf20, 0x20)
	GEN_INT_ENTRY altivec_unavailable, virt=0, ool=1
EXC_REAL_END(altivec_unavailable, 0xf20, 0x20)
EXC_VIRT_BEGIN(altivec_unavailable, 0x4f20, 0x20)
	GEN_INT_ENTRY altivec_unavailable, virt=1, ool=1
EXC_VIRT_END(altivec_unavailable, 0x4f20, 0x20)
EXC_COMMON_BEGIN(altivec_unavailable_common)
	GEN_COMMON altivec_unavailable
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
	b	fast_interrupt_return
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
2:	/* User process was in a transaction */
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	altivec_unavailable_tm
	b	interrupt_return
#endif
1:
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	altivec_unavailable_exception
	b	interrupt_return

	GEN_KVM altivec_unavailable


/**
 * Interrupt 0xf40 - VSX Unavailable Interrupt.
 * This is a synchronous interrupt in response to
 * executing a VSX instruction with MSR[VSX]=0.
 * Similar to FP unavailable.
 */
INT_DEFINE_BEGIN(vsx_unavailable)
	IVEC=0xf40
	IRECONCILE=0
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_REAL=1
#endif
INT_DEFINE_END(vsx_unavailable)

EXC_REAL_BEGIN(vsx_unavailable, 0xf40, 0x20)
	GEN_INT_ENTRY vsx_unavailable, virt=0, ool=1
EXC_REAL_END(vsx_unavailable, 0xf40, 0x20)
EXC_VIRT_BEGIN(vsx_unavailable, 0x4f40, 0x20)
	GEN_INT_ENTRY vsx_unavailable, virt=1, ool=1
EXC_VIRT_END(vsx_unavailable, 0x4f40, 0x20)
EXC_COMMON_BEGIN(vsx_unavailable_common)
	GEN_COMMON vsx_unavailable
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
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	vsx_unavailable_tm
	b	interrupt_return
#endif
1:
END_FTR_SECTION_IFSET(CPU_FTR_VSX)
#endif
	RECONCILE_IRQ_STATE(r10, r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	vsx_unavailable_exception
	b	interrupt_return

	GEN_KVM vsx_unavailable


/**
 * Interrupt 0xf60 - Facility Unavailable Interrupt.
 * This is a synchronous interrupt in response to
 * executing an instruction without access to the facility that can be
 * resolved by the OS (e.g., FSCR, MSR).
 * Similar to FP unavailable.
 */
INT_DEFINE_BEGIN(facility_unavailable)
	IVEC=0xf60
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_REAL=1
#endif
INT_DEFINE_END(facility_unavailable)

EXC_REAL_BEGIN(facility_unavailable, 0xf60, 0x20)
	GEN_INT_ENTRY facility_unavailable, virt=0, ool=1
EXC_REAL_END(facility_unavailable, 0xf60, 0x20)
EXC_VIRT_BEGIN(facility_unavailable, 0x4f60, 0x20)
	GEN_INT_ENTRY facility_unavailable, virt=1, ool=1
EXC_VIRT_END(facility_unavailable, 0x4f60, 0x20)
EXC_COMMON_BEGIN(facility_unavailable_common)
	GEN_COMMON facility_unavailable
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	facility_unavailable_exception
	REST_NVGPRS(r1) /* instruction emulation may change GPRs */
	b	interrupt_return

	GEN_KVM facility_unavailable


/**
 * Interrupt 0xf60 - Hypervisor Facility Unavailable Interrupt.
 * This is a synchronous interrupt in response to
 * executing an instruction without access to the facility that can only
 * be resolved in HV mode (e.g., HFSCR).
 * Similar to FP unavailable.
 */
INT_DEFINE_BEGIN(h_facility_unavailable)
	IVEC=0xf80
	IHSRR=1
	IKVM_REAL=1
	IKVM_VIRT=1
INT_DEFINE_END(h_facility_unavailable)

EXC_REAL_BEGIN(h_facility_unavailable, 0xf80, 0x20)
	GEN_INT_ENTRY h_facility_unavailable, virt=0, ool=1
EXC_REAL_END(h_facility_unavailable, 0xf80, 0x20)
EXC_VIRT_BEGIN(h_facility_unavailable, 0x4f80, 0x20)
	GEN_INT_ENTRY h_facility_unavailable, virt=1, ool=1
EXC_VIRT_END(h_facility_unavailable, 0x4f80, 0x20)
EXC_COMMON_BEGIN(h_facility_unavailable_common)
	GEN_COMMON h_facility_unavailable
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	facility_unavailable_exception
	REST_NVGPRS(r1) /* XXX Shouldn't be necessary in practice */
	b	interrupt_return

	GEN_KVM h_facility_unavailable


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
INT_DEFINE_BEGIN(cbe_system_error)
	IVEC=0x1200
	IHSRR=1
	IKVM_SKIP=1
	IKVM_REAL=1
INT_DEFINE_END(cbe_system_error)

EXC_REAL_BEGIN(cbe_system_error, 0x1200, 0x100)
	GEN_INT_ENTRY cbe_system_error, virt=0
EXC_REAL_END(cbe_system_error, 0x1200, 0x100)
EXC_VIRT_NONE(0x5200, 0x100)
EXC_COMMON_BEGIN(cbe_system_error_common)
	GEN_COMMON cbe_system_error
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	cbe_system_error_exception
	b	interrupt_return

	GEN_KVM cbe_system_error

#else /* CONFIG_CBE_RAS */
EXC_REAL_NONE(0x1200, 0x100)
EXC_VIRT_NONE(0x5200, 0x100)
#endif


INT_DEFINE_BEGIN(instruction_breakpoint)
	IVEC=0x1300
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_SKIP=1
	IKVM_REAL=1
#endif
INT_DEFINE_END(instruction_breakpoint)

EXC_REAL_BEGIN(instruction_breakpoint, 0x1300, 0x100)
	GEN_INT_ENTRY instruction_breakpoint, virt=0
EXC_REAL_END(instruction_breakpoint, 0x1300, 0x100)
EXC_VIRT_BEGIN(instruction_breakpoint, 0x5300, 0x100)
	GEN_INT_ENTRY instruction_breakpoint, virt=1
EXC_VIRT_END(instruction_breakpoint, 0x5300, 0x100)
EXC_COMMON_BEGIN(instruction_breakpoint_common)
	GEN_COMMON instruction_breakpoint
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	instruction_breakpoint_exception
	b	interrupt_return

	GEN_KVM instruction_breakpoint


EXC_REAL_NONE(0x1400, 0x100)
EXC_VIRT_NONE(0x5400, 0x100)

/**
 * Interrupt 0x1500 - Soft Patch Interrupt
 *
 * Handling:
 * This is an implementation specific interrupt which can be used for a
 * range of exceptions.
 *
 * This interrupt handler is unique in that it runs the denormal assist
 * code even for guests (and even in guest context) without going to KVM,
 * for speed. POWER9 does not raise denorm exceptions, so this special case
 * could be phased out in future to reduce special cases.
 */
INT_DEFINE_BEGIN(denorm_exception)
	IVEC=0x1500
	IHSRR=1
	IBRANCH_TO_COMMON=0
	IKVM_REAL=1
INT_DEFINE_END(denorm_exception)

EXC_REAL_BEGIN(denorm_exception, 0x1500, 0x100)
	GEN_INT_ENTRY denorm_exception, virt=0
#ifdef CONFIG_PPC_DENORMALISATION
	andis.	r10,r12,(HSRR1_DENORM)@h /* denorm? */
	bne+	denorm_assist
#endif
	GEN_BRANCH_TO_COMMON denorm_exception, virt=0
EXC_REAL_END(denorm_exception, 0x1500, 0x100)
#ifdef CONFIG_PPC_DENORMALISATION
EXC_VIRT_BEGIN(denorm_exception, 0x5500, 0x100)
	GEN_INT_ENTRY denorm_exception, virt=1
	andis.	r10,r12,(HSRR1_DENORM)@h /* denorm? */
	bne+	denorm_assist
	GEN_BRANCH_TO_COMMON denorm_exception, virt=1
EXC_VIRT_END(denorm_exception, 0x5500, 0x100)
#else
EXC_VIRT_NONE(0x5500, 0x100)
#endif

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
BEGIN_FTR_SECTION
	ld	r10,PACA_EXGEN+EX_PPR(r13)
	mtspr	SPRN_PPR,r10
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)
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

EXC_COMMON_BEGIN(denorm_exception_common)
	GEN_COMMON denorm_exception
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	unknown_exception
	b	interrupt_return

	GEN_KVM denorm_exception


#ifdef CONFIG_CBE_RAS
INT_DEFINE_BEGIN(cbe_maintenance)
	IVEC=0x1600
	IHSRR=1
	IKVM_SKIP=1
	IKVM_REAL=1
INT_DEFINE_END(cbe_maintenance)

EXC_REAL_BEGIN(cbe_maintenance, 0x1600, 0x100)
	GEN_INT_ENTRY cbe_maintenance, virt=0
EXC_REAL_END(cbe_maintenance, 0x1600, 0x100)
EXC_VIRT_NONE(0x5600, 0x100)
EXC_COMMON_BEGIN(cbe_maintenance_common)
	GEN_COMMON cbe_maintenance
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	cbe_maintenance_exception
	b	interrupt_return

	GEN_KVM cbe_maintenance

#else /* CONFIG_CBE_RAS */
EXC_REAL_NONE(0x1600, 0x100)
EXC_VIRT_NONE(0x5600, 0x100)
#endif


INT_DEFINE_BEGIN(altivec_assist)
	IVEC=0x1700
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	IKVM_REAL=1
#endif
INT_DEFINE_END(altivec_assist)

EXC_REAL_BEGIN(altivec_assist, 0x1700, 0x100)
	GEN_INT_ENTRY altivec_assist, virt=0
EXC_REAL_END(altivec_assist, 0x1700, 0x100)
EXC_VIRT_BEGIN(altivec_assist, 0x5700, 0x100)
	GEN_INT_ENTRY altivec_assist, virt=1
EXC_VIRT_END(altivec_assist, 0x5700, 0x100)
EXC_COMMON_BEGIN(altivec_assist_common)
	GEN_COMMON altivec_assist
	addi	r3,r1,STACK_FRAME_OVERHEAD
#ifdef CONFIG_ALTIVEC
	bl	altivec_assist_exception
	REST_NVGPRS(r1) /* instruction emulation may change GPRs */
#else
	bl	unknown_exception
#endif
	b	interrupt_return

	GEN_KVM altivec_assist


#ifdef CONFIG_CBE_RAS
INT_DEFINE_BEGIN(cbe_thermal)
	IVEC=0x1800
	IHSRR=1
	IKVM_SKIP=1
	IKVM_REAL=1
INT_DEFINE_END(cbe_thermal)

EXC_REAL_BEGIN(cbe_thermal, 0x1800, 0x100)
	GEN_INT_ENTRY cbe_thermal, virt=0
EXC_REAL_END(cbe_thermal, 0x1800, 0x100)
EXC_VIRT_NONE(0x5800, 0x100)
EXC_COMMON_BEGIN(cbe_thermal_common)
	GEN_COMMON cbe_thermal
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	cbe_thermal_exception
	b	interrupt_return

	GEN_KVM cbe_thermal

#else /* CONFIG_CBE_RAS */
EXC_REAL_NONE(0x1800, 0x100)
EXC_VIRT_NONE(0x5800, 0x100)
#endif


#ifdef CONFIG_PPC_WATCHDOG

INT_DEFINE_BEGIN(soft_nmi)
	IVEC=0x900
	ISTACK=0
	IRECONCILE=0	/* Soft-NMI may fire under local_irq_disable */
INT_DEFINE_END(soft_nmi)

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
	mfspr	r11,SPRN_SRR0
	mr	r10,r1
	ld	r1,PACAEMERGSP(r13)
	subi	r1,r1,INT_FRAME_SIZE
	__GEN_COMMON_BODY soft_nmi

	/*
	 * Set IRQS_ALL_DISABLED and save PACAIRQHAPPENED (see
	 * system_reset_common)
	 */
	li	r10,IRQS_ALL_DISABLED
	stb	r10,PACAIRQSOFTMASK(r13)
	lbz	r10,PACAIRQHAPPENED(r13)
	std	r10,RESULT(r1)
	ori	r10,r10,PACA_IRQ_HARD_DIS
	stb	r10,PACAIRQHAPPENED(r13)

	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	soft_nmi_interrupt

	/* Clear MSR_RI before setting SRR0 and SRR1. */
	li	r9,0
	mtmsrd	r9,1

	/*
	 * Restore soft mask settings.
	 */
	ld	r10,RESULT(r1)
	stb	r10,PACAIRQHAPPENED(r13)
	ld	r10,SOFTE(r1)
	stb	r10,PACAIRQSOFTMASK(r13)

	kuap_restore_amr r9, r10
	EXCEPTION_RESTORE_REGS hsrr=0
	RFI_TO_KERNEL

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
.macro MASKED_INTERRUPT hsrr=0
	.if \hsrr
masked_Hinterrupt:
	.else
masked_interrupt:
	.endif
	lbz	r11,PACAIRQHAPPENED(r13)
	or	r11,r11,r10
	stb	r11,PACAIRQHAPPENED(r13)
	cmpwi	r10,PACA_IRQ_DEC
	bne	1f
	lis	r10,0x7fff
	ori	r10,r10,0xffff
	mtspr	SPRN_DEC,r10
#ifdef CONFIG_PPC_WATCHDOG
	b	soft_nmi_common
#else
	b	2f
#endif
1:	andi.	r10,r10,PACA_IRQ_MUST_HARD_MASK
	beq	2f
	xori	r12,r12,MSR_EE	/* clear MSR_EE */
	.if \hsrr
	mtspr	SPRN_HSRR1,r12
	.else
	mtspr	SPRN_SRR1,r12
	.endif
	ori	r11,r11,PACA_IRQ_HARD_DIS
	stb	r11,PACAIRQHAPPENED(r13)
2:	/* done */
	ld	r10,PACA_EXGEN+EX_CTR(r13)
	mtctr	r10
	mtcrf	0x80,r9
	std	r1,PACAR1(r13)
	ld	r9,PACA_EXGEN+EX_R9(r13)
	ld	r10,PACA_EXGEN+EX_R10(r13)
	ld	r11,PACA_EXGEN+EX_R11(r13)
	ld	r12,PACA_EXGEN+EX_R12(r13)
	ld	r13,PACA_EXGEN+EX_R13(r13)
	/* May return to masked low address where r13 is not set up */
	.if \hsrr
	HRFI_TO_KERNEL
	.else
	RFI_TO_KERNEL
	.endif
	b	.
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

/* Clobbers r10, r11, ctr */
.macro L1D_DISPLACEMENT_FLUSH
	ld	r10,PACA_RFI_FLUSH_FALLBACK_AREA(r13)
	ld	r11,PACA_L1D_FLUSH_SIZE(r13)
	srdi	r11,r11,(7 + 3) /* 128 byte lines, unrolled 8x */
	mtctr	r11
	DCBT_BOOK3S_STOP_ALL_STREAM_IDS(r11) /* Stop prefetch streams */

	/* order ld/st prior to dcbt stop all streams with flushing */
	sync

	/*
	 * The load addresses are at staggered offsets within cachelines,
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
.endm

TRAMP_REAL_BEGIN(entry_flush_fallback)
	std	r9,PACA_EXRFI+EX_R9(r13)
	std	r10,PACA_EXRFI+EX_R10(r13)
	std	r11,PACA_EXRFI+EX_R11(r13)
	mfctr	r9
	L1D_DISPLACEMENT_FLUSH
	mtctr	r9
	ld	r9,PACA_EXRFI+EX_R9(r13)
	ld	r10,PACA_EXRFI+EX_R10(r13)
	ld	r11,PACA_EXRFI+EX_R11(r13)
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
	L1D_DISPLACEMENT_FLUSH
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
	L1D_DISPLACEMENT_FLUSH
	mtctr	r9
	ld	r9,PACA_EXRFI+EX_R9(r13)
	ld	r10,PACA_EXRFI+EX_R10(r13)
	ld	r11,PACA_EXRFI+EX_R11(r13)
	ld	r1,PACA_EXRFI+EX_R12(r13)
	GET_SCRATCH0(r13);
	hrfid

TRAMP_REAL_BEGIN(rfscv_flush_fallback)
	/* system call volatile */
	mr	r7,r13
	GET_PACA(r13);
	mr	r8,r1
	ld	r1,PACAKSAVE(r13)
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
	li	r9,0
	li	r10,0
	li	r11,0
	mr	r1,r8
	mr	r13,r7
	RFSCV

USE_TEXT_SECTION()

_GLOBAL(do_uaccess_flush)
	UACCESS_FLUSH_FIXUP_SECTION
	nop
	nop
	nop
	blr
	L1D_DISPLACEMENT_FLUSH
	blr
_ASM_NOKPROBE_SYMBOL(do_uaccess_flush)
EXPORT_SYMBOL(do_uaccess_flush)


MASKED_INTERRUPT
MASKED_INTERRUPT hsrr=1

#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
kvmppc_skip_interrupt:
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

kvmppc_skip_Hinterrupt:
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
	 * All code below __end_interrupts is treated as soft-masked. If
	 * any code runs here with MSR[EE]=1, it must then cope with pending
	 * soft interrupt being raised (i.e., by ensuring it is replayed).
	 *
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
	/*
	 * Called by exception entry code if _TLF_NAPPING was set, this clears
	 * the NAPPING flag, and redirects the exception exit to
	 * power4_fixup_nap_return.
	 */
	.globl power4_fixup_nap
EXC_COMMON_BEGIN(power4_fixup_nap)
	andc	r9,r9,r10
	std	r9,TI_LOCAL_FLAGS(r11)
	LOAD_REG_ADDR(r10, power4_idle_nap_return)
	std	r10,_NIP(r1)
	blr

power4_idle_nap_return:
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

	/*
	 * If we are in an "NMI" (e.g., an interrupt when soft-disabled), then
	 * don't call hash_page, just fail the fault. This is required to
	 * prevent re-entrancy problems in the hash code, namely perf
	 * interrupts hitting while something holds H_PAGE_BUSY, and taking a
	 * hash fault. See the comment in hash_preload().
	 */
	ld	r11, PACA_THREAD_INFO(r13)
	lwz	r0,TI_PREEMPT(r11)
	andis.	r0,r0,NMI_MASK@h
	bne	77f

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
	beq	interrupt_return	/* Return from exception on success */

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
	beq+	interrupt_return
	mr	r5,r3
	addi	r3,r1,STACK_FRAME_OVERHEAD
	ld	r4,_DAR(r1)
	bl	bad_page_fault
	b	interrupt_return

/* We have a data breakpoint exception - handle it */
handle_dabr_fault:
	ld      r4,_DAR(r1)
	ld      r5,_DSISR(r1)
	addi    r3,r1,STACK_FRAME_OVERHEAD
	bl      do_break
	/*
	 * do_break() may have changed the NV GPRS while handling a breakpoint.
	 * If so, we need to restore them with their updated values.
	 */
	REST_NVGPRS(r1)
	b       interrupt_return


#ifdef CONFIG_PPC_BOOK3S_64
/* We have a page fault that hash_page could handle but HV refused
 * the PTE insertion
 */
13:	mr	r5,r3
	addi	r3,r1,STACK_FRAME_OVERHEAD
	ld	r4,_DAR(r1)
	bl	low_hash_fault
	b	interrupt_return
#endif

/*
 * We come here as a result of a DSI at a point where we don't want
 * to call hash_page, such as when we are accessing memory (possibly
 * user memory) inside a PMU interrupt that occurred while interrupts
 * were soft-disabled.  We want to invoke the exception handler for
 * the access, or panic if there isn't a handler.
 */
77:	addi	r3,r1,STACK_FRAME_OVERHEAD
	li	r5,SIGSEGV
	bl	bad_page_fault
	b	interrupt_return
