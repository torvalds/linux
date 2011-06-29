/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright 2011 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 *
 * Derived from book3s_rmhandlers.S and other files, which are:
 *
 * Copyright SUSE Linux Products GmbH 2009
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#include <asm/ppc_asm.h>
#include <asm/kvm_asm.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <asm/asm-offsets.h>
#include <asm/exception-64s.h>

/*****************************************************************************
 *                                                                           *
 *        Real Mode handlers that need to be in the linear mapping           *
 *                                                                           *
 ****************************************************************************/

#define SHADOW_VCPU_OFF		PACA_KVM_SVCPU

	.globl	kvmppc_skip_interrupt
kvmppc_skip_interrupt:
	mfspr	r13,SPRN_SRR0
	addi	r13,r13,4
	mtspr	SPRN_SRR0,r13
	GET_SCRATCH0(r13)
	rfid
	b	.

	.globl	kvmppc_skip_Hinterrupt
kvmppc_skip_Hinterrupt:
	mfspr	r13,SPRN_HSRR0
	addi	r13,r13,4
	mtspr	SPRN_HSRR0,r13
	GET_SCRATCH0(r13)
	hrfid
	b	.

/*
 * Call kvmppc_handler_trampoline_enter in real mode.
 * Must be called with interrupts hard-disabled.
 *
 * Input Registers:
 *
 * LR = return address to continue at after eventually re-enabling MMU
 */
_GLOBAL(kvmppc_hv_entry_trampoline)
	mfmsr	r10
	LOAD_REG_ADDR(r5, kvmppc_hv_entry)
	li	r0,MSR_RI
	andc	r0,r10,r0
	li	r6,MSR_IR | MSR_DR
	andc	r6,r10,r6
	mtmsrd	r0,1		/* clear RI in MSR */
	mtsrr0	r5
	mtsrr1	r6
	RFI

#define ULONG_SIZE 		8
#define VCPU_GPR(n)		(VCPU_GPRS + (n * ULONG_SIZE))

/******************************************************************************
 *                                                                            *
 *                               Entry code                                   *
 *                                                                            *
 *****************************************************************************/

.global kvmppc_hv_entry
kvmppc_hv_entry:

	/* Required state:
	 *
	 * R4 = vcpu pointer
	 * MSR = ~IR|DR
	 * R13 = PACA
	 * R1 = host R1
	 * all other volatile GPRS = free
	 */
	mflr	r0
	std	r0, HSTATE_VMHANDLER(r13)

	ld	r14, VCPU_GPR(r14)(r4)
	ld	r15, VCPU_GPR(r15)(r4)
	ld	r16, VCPU_GPR(r16)(r4)
	ld	r17, VCPU_GPR(r17)(r4)
	ld	r18, VCPU_GPR(r18)(r4)
	ld	r19, VCPU_GPR(r19)(r4)
	ld	r20, VCPU_GPR(r20)(r4)
	ld	r21, VCPU_GPR(r21)(r4)
	ld	r22, VCPU_GPR(r22)(r4)
	ld	r23, VCPU_GPR(r23)(r4)
	ld	r24, VCPU_GPR(r24)(r4)
	ld	r25, VCPU_GPR(r25)(r4)
	ld	r26, VCPU_GPR(r26)(r4)
	ld	r27, VCPU_GPR(r27)(r4)
	ld	r28, VCPU_GPR(r28)(r4)
	ld	r29, VCPU_GPR(r29)(r4)
	ld	r30, VCPU_GPR(r30)(r4)
	ld	r31, VCPU_GPR(r31)(r4)

	/* Load guest PMU registers */
	/* R4 is live here (vcpu pointer) */
	li	r3, 1
	sldi	r3, r3, 31		/* MMCR0_FC (freeze counters) bit */
	mtspr	SPRN_MMCR0, r3		/* freeze all counters, disable ints */
	isync
	lwz	r3, VCPU_PMC(r4)	/* always load up guest PMU registers */
	lwz	r5, VCPU_PMC + 4(r4)	/* to prevent information leak */
	lwz	r6, VCPU_PMC + 8(r4)
	lwz	r7, VCPU_PMC + 12(r4)
	lwz	r8, VCPU_PMC + 16(r4)
	lwz	r9, VCPU_PMC + 20(r4)
	mtspr	SPRN_PMC1, r3
	mtspr	SPRN_PMC2, r5
	mtspr	SPRN_PMC3, r6
	mtspr	SPRN_PMC4, r7
	mtspr	SPRN_PMC5, r8
	mtspr	SPRN_PMC6, r9
	ld	r3, VCPU_MMCR(r4)
	ld	r5, VCPU_MMCR + 8(r4)
	ld	r6, VCPU_MMCR + 16(r4)
	mtspr	SPRN_MMCR1, r5
	mtspr	SPRN_MMCRA, r6
	mtspr	SPRN_MMCR0, r3
	isync

	/* Load up FP, VMX and VSX registers */
	bl	kvmppc_load_fp

	/* Switch DSCR to guest value */
	ld	r5, VCPU_DSCR(r4)
	mtspr	SPRN_DSCR, r5

	/*
	 * Set the decrementer to the guest decrementer.
	 */
	ld	r8,VCPU_DEC_EXPIRES(r4)
	mftb	r7
	subf	r3,r7,r8
	mtspr	SPRN_DEC,r3
	stw	r3,VCPU_DEC(r4)

	ld	r5, VCPU_SPRG0(r4)
	ld	r6, VCPU_SPRG1(r4)
	ld	r7, VCPU_SPRG2(r4)
	ld	r8, VCPU_SPRG3(r4)
	mtspr	SPRN_SPRG0, r5
	mtspr	SPRN_SPRG1, r6
	mtspr	SPRN_SPRG2, r7
	mtspr	SPRN_SPRG3, r8

	/* Save R1 in the PACA */
	std	r1, HSTATE_HOST_R1(r13)

	/* Increment yield count if they have a VPA */
	ld	r3, VCPU_VPA(r4)
	cmpdi	r3, 0
	beq	25f
	lwz	r5, LPPACA_YIELDCOUNT(r3)
	addi	r5, r5, 1
	stw	r5, LPPACA_YIELDCOUNT(r3)
25:
	/* Load up DAR and DSISR */
	ld	r5, VCPU_DAR(r4)
	lwz	r6, VCPU_DSISR(r4)
	mtspr	SPRN_DAR, r5
	mtspr	SPRN_DSISR, r6

	/* Set partition DABR */
	li	r5,3
	ld	r6,VCPU_DABR(r4)
	mtspr	SPRN_DABRX,r5
	mtspr	SPRN_DABR,r6

	/* Restore AMR and UAMOR, set AMOR to all 1s */
	ld	r5,VCPU_AMR(r4)
	ld	r6,VCPU_UAMOR(r4)
	li	r7,-1
	mtspr	SPRN_AMR,r5
	mtspr	SPRN_UAMOR,r6
	mtspr	SPRN_AMOR,r7

	/* Clear out SLB */
	li	r6,0
	slbmte	r6,r6
	slbia
	ptesync

	/* Switch to guest partition. */
	ld	r9,VCPU_KVM(r4)		/* pointer to struct kvm */
	ld	r6,KVM_SDR1(r9)
	lwz	r7,KVM_LPID(r9)
	li	r0,LPID_RSVD		/* switch to reserved LPID */
	mtspr	SPRN_LPID,r0
	ptesync
	mtspr	SPRN_SDR1,r6		/* switch to partition page table */
	mtspr	SPRN_LPID,r7
	isync
	ld	r8,VCPU_LPCR(r4)
	mtspr	SPRN_LPCR,r8
	isync

	/* Check if HDEC expires soon */
	mfspr	r3,SPRN_HDEC
	cmpwi	r3,10
	li	r12,BOOK3S_INTERRUPT_HV_DECREMENTER
	mr	r9,r4
	blt	hdec_soon

	/*
	 * Invalidate the TLB if we could possibly have stale TLB
	 * entries for this partition on this core due to the use
	 * of tlbiel.
	 */
	ld	r9,VCPU_KVM(r4)		/* pointer to struct kvm */
	lwz	r5,VCPU_VCPUID(r4)
	lhz	r6,PACAPACAINDEX(r13)
	lhz	r8,VCPU_LAST_CPU(r4)
	sldi	r7,r6,1			/* see if this is the same vcpu */
	add	r7,r7,r9		/* as last ran on this pcpu */
	lhz	r0,KVM_LAST_VCPU(r7)
	cmpw	r6,r8			/* on the same cpu core as last time? */
	bne	3f
	cmpw	r0,r5			/* same vcpu as this core last ran? */
	beq	1f
3:	sth	r6,VCPU_LAST_CPU(r4)	/* if not, invalidate partition TLB */
	sth	r5,KVM_LAST_VCPU(r7)
	li	r6,128
	mtctr	r6
	li	r7,0x800		/* IS field = 0b10 */
	ptesync
2:	tlbiel	r7
	addi	r7,r7,0x1000
	bdnz	2b
	ptesync
1:

	/* Save purr/spurr */
	mfspr	r5,SPRN_PURR
	mfspr	r6,SPRN_SPURR
	std	r5,HSTATE_PURR(r13)
	std	r6,HSTATE_SPURR(r13)
	ld	r7,VCPU_PURR(r4)
	ld	r8,VCPU_SPURR(r4)
	mtspr	SPRN_PURR,r7
	mtspr	SPRN_SPURR,r8

	/* Load up guest SLB entries */
	lwz	r5,VCPU_SLB_MAX(r4)
	cmpwi	r5,0
	beq	9f
	mtctr	r5
	addi	r6,r4,VCPU_SLB
1:	ld	r8,VCPU_SLB_E(r6)
	ld	r9,VCPU_SLB_V(r6)
	slbmte	r9,r8
	addi	r6,r6,VCPU_SLB_SIZE
	bdnz	1b
9:

	/* Restore state of CTRL run bit; assume 1 on entry */
	lwz	r5,VCPU_CTRL(r4)
	andi.	r5,r5,1
	bne	4f
	mfspr	r6,SPRN_CTRLF
	clrrdi	r6,r6,1
	mtspr	SPRN_CTRLT,r6
4:
	ld	r6, VCPU_CTR(r4)
	lwz	r7, VCPU_XER(r4)

	mtctr	r6
	mtxer	r7

	/* Move SRR0 and SRR1 into the respective regs */
	ld	r6, VCPU_SRR0(r4)
	ld	r7, VCPU_SRR1(r4)
	mtspr	SPRN_SRR0, r6
	mtspr	SPRN_SRR1, r7

	ld	r10, VCPU_PC(r4)

	ld	r11, VCPU_MSR(r4)	/* r10 = vcpu->arch.msr & ~MSR_HV */
	rldicl	r11, r11, 63 - MSR_HV_LG, 1
	rotldi	r11, r11, 1 + MSR_HV_LG
	ori	r11, r11, MSR_ME

fast_guest_return:
	mtspr	SPRN_HSRR0,r10
	mtspr	SPRN_HSRR1,r11

	/* Activate guest mode, so faults get handled by KVM */
	li	r9, KVM_GUEST_MODE_GUEST
	stb	r9, HSTATE_IN_GUEST(r13)

	/* Enter guest */

	ld	r5, VCPU_LR(r4)
	lwz	r6, VCPU_CR(r4)
	mtlr	r5
	mtcr	r6

	ld	r0, VCPU_GPR(r0)(r4)
	ld	r1, VCPU_GPR(r1)(r4)
	ld	r2, VCPU_GPR(r2)(r4)
	ld	r3, VCPU_GPR(r3)(r4)
	ld	r5, VCPU_GPR(r5)(r4)
	ld	r6, VCPU_GPR(r6)(r4)
	ld	r7, VCPU_GPR(r7)(r4)
	ld	r8, VCPU_GPR(r8)(r4)
	ld	r9, VCPU_GPR(r9)(r4)
	ld	r10, VCPU_GPR(r10)(r4)
	ld	r11, VCPU_GPR(r11)(r4)
	ld	r12, VCPU_GPR(r12)(r4)
	ld	r13, VCPU_GPR(r13)(r4)

	ld	r4, VCPU_GPR(r4)(r4)

	hrfid
	b	.

/******************************************************************************
 *                                                                            *
 *                               Exit code                                    *
 *                                                                            *
 *****************************************************************************/

/*
 * We come here from the first-level interrupt handlers.
 */
	.globl	kvmppc_interrupt
kvmppc_interrupt:
	/*
	 * Register contents:
	 * R12		= interrupt vector
	 * R13		= PACA
	 * guest CR, R12 saved in shadow VCPU SCRATCH1/0
	 * guest R13 saved in SPRN_SCRATCH0
	 */
	/* abuse host_r2 as third scratch area; we get r2 from PACATOC(r13) */
	std	r9, HSTATE_HOST_R2(r13)
	ld	r9, HSTATE_KVM_VCPU(r13)

	/* Save registers */

	std	r0, VCPU_GPR(r0)(r9)
	std	r1, VCPU_GPR(r1)(r9)
	std	r2, VCPU_GPR(r2)(r9)
	std	r3, VCPU_GPR(r3)(r9)
	std	r4, VCPU_GPR(r4)(r9)
	std	r5, VCPU_GPR(r5)(r9)
	std	r6, VCPU_GPR(r6)(r9)
	std	r7, VCPU_GPR(r7)(r9)
	std	r8, VCPU_GPR(r8)(r9)
	ld	r0, HSTATE_HOST_R2(r13)
	std	r0, VCPU_GPR(r9)(r9)
	std	r10, VCPU_GPR(r10)(r9)
	std	r11, VCPU_GPR(r11)(r9)
	ld	r3, HSTATE_SCRATCH0(r13)
	lwz	r4, HSTATE_SCRATCH1(r13)
	std	r3, VCPU_GPR(r12)(r9)
	stw	r4, VCPU_CR(r9)

	/* Restore R1/R2 so we can handle faults */
	ld	r1, HSTATE_HOST_R1(r13)
	ld	r2, PACATOC(r13)

	mfspr	r10, SPRN_SRR0
	mfspr	r11, SPRN_SRR1
	std	r10, VCPU_SRR0(r9)
	std	r11, VCPU_SRR1(r9)
	andi.	r0, r12, 2		/* need to read HSRR0/1? */
	beq	1f
	mfspr	r10, SPRN_HSRR0
	mfspr	r11, SPRN_HSRR1
	clrrdi	r12, r12, 2
1:	std	r10, VCPU_PC(r9)
	std	r11, VCPU_MSR(r9)

	GET_SCRATCH0(r3)
	mflr	r4
	std	r3, VCPU_GPR(r13)(r9)
	std	r4, VCPU_LR(r9)

	/* Unset guest mode */
	li	r0, KVM_GUEST_MODE_NONE
	stb	r0, HSTATE_IN_GUEST(r13)

	stw	r12,VCPU_TRAP(r9)

	/* See if this is a leftover HDEC interrupt */
	cmpwi	r12,BOOK3S_INTERRUPT_HV_DECREMENTER
	bne	2f
	mfspr	r3,SPRN_HDEC
	cmpwi	r3,0
	bge	ignore_hdec
2:
	/* See if this is something we can handle in real mode */
	cmpwi	r12,BOOK3S_INTERRUPT_SYSCALL
	beq	hcall_try_real_mode
hcall_real_cont:

	/* Check for mediated interrupts (could be done earlier really ...) */
	cmpwi	r12,BOOK3S_INTERRUPT_EXTERNAL
	bne+	1f
	ld	r5,VCPU_LPCR(r9)
	andi.	r0,r11,MSR_EE
	beq	1f
	andi.	r0,r5,LPCR_MER
	bne	bounce_ext_interrupt
1:

	/* Save DEC */
	mfspr	r5,SPRN_DEC
	mftb	r6
	extsw	r5,r5
	add	r5,r5,r6
	std	r5,VCPU_DEC_EXPIRES(r9)

	/* Save HEIR (HV emulation assist reg) in last_inst
	   if this is an HEI (HV emulation interrupt, e40) */
	li	r3,-1
	cmpwi	r12,BOOK3S_INTERRUPT_H_EMUL_ASSIST
	bne	11f
	mfspr	r3,SPRN_HEIR
11:	stw	r3,VCPU_LAST_INST(r9)

	/* Save more register state  */
	mfxer	r5
	mfdar	r6
	mfdsisr	r7
	mfctr	r8

	stw	r5, VCPU_XER(r9)
	std	r6, VCPU_DAR(r9)
	stw	r7, VCPU_DSISR(r9)
	std	r8, VCPU_CTR(r9)
	/* grab HDAR & HDSISR if HV data storage interrupt (HDSI) */
	cmpwi	r12,BOOK3S_INTERRUPT_H_DATA_STORAGE
	beq	6f
7:	std	r6, VCPU_FAULT_DAR(r9)
	stw	r7, VCPU_FAULT_DSISR(r9)

	/* Save guest CTRL register, set runlatch to 1 */
	mfspr	r6,SPRN_CTRLF
	stw	r6,VCPU_CTRL(r9)
	andi.	r0,r6,1
	bne	4f
	ori	r6,r6,1
	mtspr	SPRN_CTRLT,r6
4:
	/* Read the guest SLB and save it away */
	lwz	r0,VCPU_SLB_NR(r9)	/* number of entries in SLB */
	mtctr	r0
	li	r6,0
	addi	r7,r9,VCPU_SLB
	li	r5,0
1:	slbmfee	r8,r6
	andis.	r0,r8,SLB_ESID_V@h
	beq	2f
	add	r8,r8,r6		/* put index in */
	slbmfev	r3,r6
	std	r8,VCPU_SLB_E(r7)
	std	r3,VCPU_SLB_V(r7)
	addi	r7,r7,VCPU_SLB_SIZE
	addi	r5,r5,1
2:	addi	r6,r6,1
	bdnz	1b
	stw	r5,VCPU_SLB_MAX(r9)

	/*
	 * Save the guest PURR/SPURR
	 */
	mfspr	r5,SPRN_PURR
	mfspr	r6,SPRN_SPURR
	ld	r7,VCPU_PURR(r9)
	ld	r8,VCPU_SPURR(r9)
	std	r5,VCPU_PURR(r9)
	std	r6,VCPU_SPURR(r9)
	subf	r5,r7,r5
	subf	r6,r8,r6

	/*
	 * Restore host PURR/SPURR and add guest times
	 * so that the time in the guest gets accounted.
	 */
	ld	r3,HSTATE_PURR(r13)
	ld	r4,HSTATE_SPURR(r13)
	add	r3,r3,r5
	add	r4,r4,r6
	mtspr	SPRN_PURR,r3
	mtspr	SPRN_SPURR,r4

	/* Clear out SLB */
	li	r5,0
	slbmte	r5,r5
	slbia
	ptesync

hdec_soon:
	/* Switch back to host partition */
	ld	r4,VCPU_KVM(r9)		/* pointer to struct kvm */
	ld	r6,KVM_HOST_SDR1(r4)
	lwz	r7,KVM_HOST_LPID(r4)
	li	r8,LPID_RSVD		/* switch to reserved LPID */
	mtspr	SPRN_LPID,r8
	ptesync
	mtspr	SPRN_SDR1,r6		/* switch to partition page table */
	mtspr	SPRN_LPID,r7
	isync
	lis	r8,0x7fff		/* MAX_INT@h */
	mtspr	SPRN_HDEC,r8

	ld	r8,KVM_HOST_LPCR(r4)
	mtspr	SPRN_LPCR,r8
	isync

	/* load host SLB entries */
	ld	r8,PACA_SLBSHADOWPTR(r13)

	.rept	SLB_NUM_BOLTED
	ld	r5,SLBSHADOW_SAVEAREA(r8)
	ld	r6,SLBSHADOW_SAVEAREA+8(r8)
	andis.	r7,r5,SLB_ESID_V@h
	beq	1f
	slbmte	r6,r5
1:	addi	r8,r8,16
	.endr

	/* Save and reset AMR and UAMOR before turning on the MMU */
	mfspr	r5,SPRN_AMR
	mfspr	r6,SPRN_UAMOR
	std	r5,VCPU_AMR(r9)
	std	r6,VCPU_UAMOR(r9)
	li	r6,0
	mtspr	SPRN_AMR,r6

	/* Restore host DABR and DABRX */
	ld	r5,HSTATE_DABR(r13)
	li	r6,7
	mtspr	SPRN_DABR,r5
	mtspr	SPRN_DABRX,r6

	/* Switch DSCR back to host value */
	mfspr	r8, SPRN_DSCR
	ld	r7, HSTATE_DSCR(r13)
	std	r8, VCPU_DSCR(r7)
	mtspr	SPRN_DSCR, r7

	/* Save non-volatile GPRs */
	std	r14, VCPU_GPR(r14)(r9)
	std	r15, VCPU_GPR(r15)(r9)
	std	r16, VCPU_GPR(r16)(r9)
	std	r17, VCPU_GPR(r17)(r9)
	std	r18, VCPU_GPR(r18)(r9)
	std	r19, VCPU_GPR(r19)(r9)
	std	r20, VCPU_GPR(r20)(r9)
	std	r21, VCPU_GPR(r21)(r9)
	std	r22, VCPU_GPR(r22)(r9)
	std	r23, VCPU_GPR(r23)(r9)
	std	r24, VCPU_GPR(r24)(r9)
	std	r25, VCPU_GPR(r25)(r9)
	std	r26, VCPU_GPR(r26)(r9)
	std	r27, VCPU_GPR(r27)(r9)
	std	r28, VCPU_GPR(r28)(r9)
	std	r29, VCPU_GPR(r29)(r9)
	std	r30, VCPU_GPR(r30)(r9)
	std	r31, VCPU_GPR(r31)(r9)

	/* Save SPRGs */
	mfspr	r3, SPRN_SPRG0
	mfspr	r4, SPRN_SPRG1
	mfspr	r5, SPRN_SPRG2
	mfspr	r6, SPRN_SPRG3
	std	r3, VCPU_SPRG0(r9)
	std	r4, VCPU_SPRG1(r9)
	std	r5, VCPU_SPRG2(r9)
	std	r6, VCPU_SPRG3(r9)

	/* Increment yield count if they have a VPA */
	ld	r8, VCPU_VPA(r9)	/* do they have a VPA? */
	cmpdi	r8, 0
	beq	25f
	lwz	r3, LPPACA_YIELDCOUNT(r8)
	addi	r3, r3, 1
	stw	r3, LPPACA_YIELDCOUNT(r8)
25:
	/* Save PMU registers if requested */
	/* r8 and cr0.eq are live here */
	li	r3, 1
	sldi	r3, r3, 31		/* MMCR0_FC (freeze counters) bit */
	mfspr	r4, SPRN_MMCR0		/* save MMCR0 */
	mtspr	SPRN_MMCR0, r3		/* freeze all counters, disable ints */
	isync
	beq	21f			/* if no VPA, save PMU stuff anyway */
	lbz	r7, LPPACA_PMCINUSE(r8)
	cmpwi	r7, 0			/* did they ask for PMU stuff to be saved? */
	bne	21f
	std	r3, VCPU_MMCR(r9)	/* if not, set saved MMCR0 to FC */
	b	22f
21:	mfspr	r5, SPRN_MMCR1
	mfspr	r6, SPRN_MMCRA
	std	r4, VCPU_MMCR(r9)
	std	r5, VCPU_MMCR + 8(r9)
	std	r6, VCPU_MMCR + 16(r9)
	mfspr	r3, SPRN_PMC1
	mfspr	r4, SPRN_PMC2
	mfspr	r5, SPRN_PMC3
	mfspr	r6, SPRN_PMC4
	mfspr	r7, SPRN_PMC5
	mfspr	r8, SPRN_PMC6
	stw	r3, VCPU_PMC(r9)
	stw	r4, VCPU_PMC + 4(r9)
	stw	r5, VCPU_PMC + 8(r9)
	stw	r6, VCPU_PMC + 12(r9)
	stw	r7, VCPU_PMC + 16(r9)
	stw	r8, VCPU_PMC + 20(r9)
22:
	/* save FP state */
	mr	r3, r9
	bl	.kvmppc_save_fp

	/*
	 * Reload DEC.  HDEC interrupts were disabled when
	 * we reloaded the host's LPCR value.
	 */
	ld	r3, HSTATE_DECEXP(r13)
	mftb	r4
	subf	r4, r4, r3
	mtspr	SPRN_DEC, r4

	/* Reload the host's PMU registers */
	ld	r3, PACALPPACAPTR(r13)	/* is the host using the PMU? */
	lbz	r4, LPPACA_PMCINUSE(r3)
	cmpwi	r4, 0
	beq	23f			/* skip if not */
	lwz	r3, HSTATE_PMC(r13)
	lwz	r4, HSTATE_PMC + 4(r13)
	lwz	r5, HSTATE_PMC + 8(r13)
	lwz	r6, HSTATE_PMC + 12(r13)
	lwz	r8, HSTATE_PMC + 16(r13)
	lwz	r9, HSTATE_PMC + 20(r13)
	mtspr	SPRN_PMC1, r3
	mtspr	SPRN_PMC2, r4
	mtspr	SPRN_PMC3, r5
	mtspr	SPRN_PMC4, r6
	mtspr	SPRN_PMC5, r8
	mtspr	SPRN_PMC6, r9
	ld	r3, HSTATE_MMCR(r13)
	ld	r4, HSTATE_MMCR + 8(r13)
	ld	r5, HSTATE_MMCR + 16(r13)
	mtspr	SPRN_MMCR1, r4
	mtspr	SPRN_MMCRA, r5
	mtspr	SPRN_MMCR0, r3
	isync
23:
	/*
	 * For external and machine check interrupts, we need
	 * to call the Linux handler to process the interrupt.
	 * We do that by jumping to the interrupt vector address
	 * which we have in r12.  The [h]rfid at the end of the
	 * handler will return to the book3s_hv_interrupts.S code.
	 * For other interrupts we do the rfid to get back
	 * to the book3s_interrupts.S code here.
	 */
	ld	r8, HSTATE_VMHANDLER(r13)
	ld	r7, HSTATE_HOST_MSR(r13)

	cmpwi	r12, BOOK3S_INTERRUPT_EXTERNAL
	beq	11f
	cmpwi	r12, BOOK3S_INTERRUPT_MACHINE_CHECK

	/* RFI into the highmem handler, or branch to interrupt handler */
	mfmsr	r6
	mtctr	r12
	li	r0, MSR_RI
	andc	r6, r6, r0
	mtmsrd	r6, 1			/* Clear RI in MSR */
	mtsrr0	r8
	mtsrr1	r7
	beqctr
	RFI

11:	mtspr	SPRN_HSRR0, r8
	mtspr	SPRN_HSRR1, r7
	ba	0x500

6:	mfspr	r6,SPRN_HDAR
	mfspr	r7,SPRN_HDSISR
	b	7b

/*
 * Try to handle an hcall in real mode.
 * Returns to the guest if we handle it, or continues on up to
 * the kernel if we can't (i.e. if we don't have a handler for
 * it, or if the handler returns H_TOO_HARD).
 */
	.globl	hcall_try_real_mode
hcall_try_real_mode:
	ld	r3,VCPU_GPR(r3)(r9)
	andi.	r0,r11,MSR_PR
	bne	hcall_real_cont
	clrrdi	r3,r3,2
	cmpldi	r3,hcall_real_table_end - hcall_real_table
	bge	hcall_real_cont
	LOAD_REG_ADDR(r4, hcall_real_table)
	lwzx	r3,r3,r4
	cmpwi	r3,0
	beq	hcall_real_cont
	add	r3,r3,r4
	mtctr	r3
	mr	r3,r9		/* get vcpu pointer */
	ld	r4,VCPU_GPR(r4)(r9)
	bctrl
	cmpdi	r3,H_TOO_HARD
	beq	hcall_real_fallback
	ld	r4,HSTATE_KVM_VCPU(r13)
	std	r3,VCPU_GPR(r3)(r4)
	ld	r10,VCPU_PC(r4)
	ld	r11,VCPU_MSR(r4)
	b	fast_guest_return

	/* We've attempted a real mode hcall, but it's punted it back
	 * to userspace.  We need to restore some clobbered volatiles
	 * before resuming the pass-it-to-qemu path */
hcall_real_fallback:
	li	r12,BOOK3S_INTERRUPT_SYSCALL
	ld	r9, HSTATE_KVM_VCPU(r13)
	ld	r11, VCPU_MSR(r9)

	b	hcall_real_cont

	.globl	hcall_real_table
hcall_real_table:
	.long	0		/* 0 - unused */
	.long	.kvmppc_h_remove - hcall_real_table
	.long	.kvmppc_h_enter - hcall_real_table
	.long	.kvmppc_h_read - hcall_real_table
	.long	0		/* 0x10 - H_CLEAR_MOD */
	.long	0		/* 0x14 - H_CLEAR_REF */
	.long	.kvmppc_h_protect - hcall_real_table
	.long	0		/* 0x1c - H_GET_TCE */
	.long	0		/* 0x20 - H_SET_TCE */
	.long	0		/* 0x24 - H_SET_SPRG0 */
	.long	.kvmppc_h_set_dabr - hcall_real_table
	.long	0		/* 0x2c */
	.long	0		/* 0x30 */
	.long	0		/* 0x34 */
	.long	0		/* 0x38 */
	.long	0		/* 0x3c */
	.long	0		/* 0x40 */
	.long	0		/* 0x44 */
	.long	0		/* 0x48 */
	.long	0		/* 0x4c */
	.long	0		/* 0x50 */
	.long	0		/* 0x54 */
	.long	0		/* 0x58 */
	.long	0		/* 0x5c */
	.long	0		/* 0x60 */
	.long	0		/* 0x64 */
	.long	0		/* 0x68 */
	.long	0		/* 0x6c */
	.long	0		/* 0x70 */
	.long	0		/* 0x74 */
	.long	0		/* 0x78 */
	.long	0		/* 0x7c */
	.long	0		/* 0x80 */
	.long	0		/* 0x84 */
	.long	0		/* 0x88 */
	.long	0		/* 0x8c */
	.long	0		/* 0x90 */
	.long	0		/* 0x94 */
	.long	0		/* 0x98 */
	.long	0		/* 0x9c */
	.long	0		/* 0xa0 */
	.long	0		/* 0xa4 */
	.long	0		/* 0xa8 */
	.long	0		/* 0xac */
	.long	0		/* 0xb0 */
	.long	0		/* 0xb4 */
	.long	0		/* 0xb8 */
	.long	0		/* 0xbc */
	.long	0		/* 0xc0 */
	.long	0		/* 0xc4 */
	.long	0		/* 0xc8 */
	.long	0		/* 0xcc */
	.long	0		/* 0xd0 */
	.long	0		/* 0xd4 */
	.long	0		/* 0xd8 */
	.long	0		/* 0xdc */
	.long	0		/* 0xe0 */
	.long	0		/* 0xe4 */
	.long	0		/* 0xe8 */
	.long	0		/* 0xec */
	.long	0		/* 0xf0 */
	.long	0		/* 0xf4 */
	.long	0		/* 0xf8 */
	.long	0		/* 0xfc */
	.long	0		/* 0x100 */
	.long	0		/* 0x104 */
	.long	0		/* 0x108 */
	.long	0		/* 0x10c */
	.long	0		/* 0x110 */
	.long	0		/* 0x114 */
	.long	0		/* 0x118 */
	.long	0		/* 0x11c */
	.long	0		/* 0x120 */
	.long	.kvmppc_h_bulk_remove - hcall_real_table
hcall_real_table_end:

ignore_hdec:
	mr	r4,r9
	b	fast_guest_return

bounce_ext_interrupt:
	mr	r4,r9
	mtspr	SPRN_SRR0,r10
	mtspr	SPRN_SRR1,r11
	li	r10,BOOK3S_INTERRUPT_EXTERNAL
	LOAD_REG_IMMEDIATE(r11,MSR_SF | MSR_ME);
	b	fast_guest_return

_GLOBAL(kvmppc_h_set_dabr)
	std	r4,VCPU_DABR(r3)
	mtspr	SPRN_DABR,r4
	li	r3,0
	blr

/*
 * Save away FP, VMX and VSX registers.
 * r3 = vcpu pointer
 */
_GLOBAL(kvmppc_save_fp)
	mfmsr	r9
	ori	r8,r9,MSR_FP
#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	oris	r8,r8,MSR_VEC@h
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif
#ifdef CONFIG_VSX
BEGIN_FTR_SECTION
	oris	r8,r8,MSR_VSX@h
END_FTR_SECTION_IFSET(CPU_FTR_VSX)
#endif
	mtmsrd	r8
	isync
#ifdef CONFIG_VSX
BEGIN_FTR_SECTION
	reg = 0
	.rept	32
	li	r6,reg*16+VCPU_VSRS
	stxvd2x	reg,r6,r3
	reg = reg + 1
	.endr
FTR_SECTION_ELSE
#endif
	reg = 0
	.rept	32
	stfd	reg,reg*8+VCPU_FPRS(r3)
	reg = reg + 1
	.endr
#ifdef CONFIG_VSX
ALT_FTR_SECTION_END_IFSET(CPU_FTR_VSX)
#endif
	mffs	fr0
	stfd	fr0,VCPU_FPSCR(r3)

#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	reg = 0
	.rept	32
	li	r6,reg*16+VCPU_VRS
	stvx	reg,r6,r3
	reg = reg + 1
	.endr
	mfvscr	vr0
	li	r6,VCPU_VSCR
	stvx	vr0,r6,r3
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif
	mfspr	r6,SPRN_VRSAVE
	stw	r6,VCPU_VRSAVE(r3)
	mtmsrd	r9
	isync
	blr

/*
 * Load up FP, VMX and VSX registers
 * r4 = vcpu pointer
 */
	.globl	kvmppc_load_fp
kvmppc_load_fp:
	mfmsr	r9
	ori	r8,r9,MSR_FP
#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	oris	r8,r8,MSR_VEC@h
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif
#ifdef CONFIG_VSX
BEGIN_FTR_SECTION
	oris	r8,r8,MSR_VSX@h
END_FTR_SECTION_IFSET(CPU_FTR_VSX)
#endif
	mtmsrd	r8
	isync
	lfd	fr0,VCPU_FPSCR(r4)
	MTFSF_L(fr0)
#ifdef CONFIG_VSX
BEGIN_FTR_SECTION
	reg = 0
	.rept	32
	li	r7,reg*16+VCPU_VSRS
	lxvd2x	reg,r7,r4
	reg = reg + 1
	.endr
FTR_SECTION_ELSE
#endif
	reg = 0
	.rept	32
	lfd	reg,reg*8+VCPU_FPRS(r4)
	reg = reg + 1
	.endr
#ifdef CONFIG_VSX
ALT_FTR_SECTION_END_IFSET(CPU_FTR_VSX)
#endif

#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	li	r7,VCPU_VSCR
	lvx	vr0,r7,r4
	mtvscr	vr0
	reg = 0
	.rept	32
	li	r7,reg*16+VCPU_VRS
	lvx	reg,r7,r4
	reg = reg + 1
	.endr
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif
	lwz	r7,VCPU_VRSAVE(r4)
	mtspr	SPRN_VRSAVE,r7
	blr
