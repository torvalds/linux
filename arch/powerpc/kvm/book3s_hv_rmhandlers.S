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
#include <asm/mmu.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/hvcall.h>
#include <asm/asm-offsets.h>
#include <asm/exception-64s.h>
#include <asm/kvm_book3s_asm.h>
#include <asm/mmu-hash64.h>

/*****************************************************************************
 *                                                                           *
 *        Real Mode handlers that need to be in the linear mapping           *
 *                                                                           *
 ****************************************************************************/

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
 * Call kvmppc_hv_entry in real mode.
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

/******************************************************************************
 *                                                                            *
 *                               Entry code                                   *
 *                                                                            *
 *****************************************************************************/

/*
 * We come in here when wakened from nap mode on a secondary hw thread.
 * Relocation is off and most register values are lost.
 * r13 points to the PACA.
 */
	.globl	kvm_start_guest
kvm_start_guest:
	ld	r1,PACAEMERGSP(r13)
	subi	r1,r1,STACK_FRAME_OVERHEAD
	ld	r2,PACATOC(r13)

	li	r0,KVM_HWTHREAD_IN_KVM
	stb	r0,HSTATE_HWTHREAD_STATE(r13)

	/* NV GPR values from power7_idle() will no longer be valid */
	li	r0,1
	stb	r0,PACA_NAPSTATELOST(r13)

	/* were we napping due to cede? */
	lbz	r0,HSTATE_NAPPING(r13)
	cmpwi	r0,0
	bne	kvm_end_cede

	/*
	 * We weren't napping due to cede, so this must be a secondary
	 * thread being woken up to run a guest, or being woken up due
	 * to a stray IPI.  (Or due to some machine check or hypervisor
	 * maintenance interrupt while the core is in KVM.)
	 */

	/* Check the wake reason in SRR1 to see why we got here */
	mfspr	r3,SPRN_SRR1
	rlwinm	r3,r3,44-31,0x7		/* extract wake reason field */
	cmpwi	r3,4			/* was it an external interrupt? */
	bne	27f			/* if not */
	ld	r5,HSTATE_XICS_PHYS(r13)
	li	r7,XICS_XIRR		/* if it was an external interrupt, */
	lwzcix	r8,r5,r7		/* get and ack the interrupt */
	sync
	clrldi.	r9,r8,40		/* get interrupt source ID. */
	beq	28f			/* none there? */
	cmpwi	r9,XICS_IPI		/* was it an IPI? */
	bne	29f
	li	r0,0xff
	li	r6,XICS_MFRR
	stbcix	r0,r5,r6		/* clear IPI */
	stwcix	r8,r5,r7		/* EOI the interrupt */
	sync				/* order loading of vcpu after that */

	/* get vcpu pointer, NULL if we have no vcpu to run */
	ld	r4,HSTATE_KVM_VCPU(r13)
	cmpdi	r4,0
	/* if we have no vcpu to run, go back to sleep */
	beq	kvm_no_guest
	b	kvmppc_hv_entry

27:	/* XXX should handle hypervisor maintenance interrupts etc. here */
	b	kvm_no_guest
28:	/* SRR1 said external but ICP said nope?? */
	b	kvm_no_guest
29:	/* External non-IPI interrupt to offline secondary thread? help?? */
	stw	r8,HSTATE_SAVED_XIRR(r13)
	b	kvm_no_guest

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

	/* Set partition DABR */
	/* Do this before re-enabling PMU to avoid P7 DABR corruption bug */
	li	r5,3
	ld	r6,VCPU_DABR(r4)
	mtspr	SPRN_DABRX,r5
	mtspr	SPRN_DABR,r6
BEGIN_FTR_SECTION
	isync
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_206)

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
BEGIN_FTR_SECTION
	lwz	r10, VCPU_PMC + 24(r4)
	lwz	r11, VCPU_PMC + 28(r4)
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_201)
	mtspr	SPRN_PMC1, r3
	mtspr	SPRN_PMC2, r5
	mtspr	SPRN_PMC3, r6
	mtspr	SPRN_PMC4, r7
	mtspr	SPRN_PMC5, r8
	mtspr	SPRN_PMC6, r9
BEGIN_FTR_SECTION
	mtspr	SPRN_PMC7, r10
	mtspr	SPRN_PMC8, r11
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_201)
	ld	r3, VCPU_MMCR(r4)
	ld	r5, VCPU_MMCR + 8(r4)
	ld	r6, VCPU_MMCR + 16(r4)
	mtspr	SPRN_MMCR1, r5
	mtspr	SPRN_MMCRA, r6
	mtspr	SPRN_MMCR0, r3
	isync

	/* Load up FP, VMX and VSX registers */
	bl	kvmppc_load_fp

	ld	r14, VCPU_GPR(R14)(r4)
	ld	r15, VCPU_GPR(R15)(r4)
	ld	r16, VCPU_GPR(R16)(r4)
	ld	r17, VCPU_GPR(R17)(r4)
	ld	r18, VCPU_GPR(R18)(r4)
	ld	r19, VCPU_GPR(R19)(r4)
	ld	r20, VCPU_GPR(R20)(r4)
	ld	r21, VCPU_GPR(R21)(r4)
	ld	r22, VCPU_GPR(R22)(r4)
	ld	r23, VCPU_GPR(R23)(r4)
	ld	r24, VCPU_GPR(R24)(r4)
	ld	r25, VCPU_GPR(R25)(r4)
	ld	r26, VCPU_GPR(R26)(r4)
	ld	r27, VCPU_GPR(R27)(r4)
	ld	r28, VCPU_GPR(R28)(r4)
	ld	r29, VCPU_GPR(R29)(r4)
	ld	r30, VCPU_GPR(R30)(r4)
	ld	r31, VCPU_GPR(R31)(r4)

BEGIN_FTR_SECTION
	/* Switch DSCR to guest value */
	ld	r5, VCPU_DSCR(r4)
	mtspr	SPRN_DSCR, r5
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_206)

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
	li	r6, 1
	stb	r6, VCPU_VPA_DIRTY(r4)
25:
	/* Load up DAR and DSISR */
	ld	r5, VCPU_DAR(r4)
	lwz	r6, VCPU_DSISR(r4)
	mtspr	SPRN_DAR, r5
	mtspr	SPRN_DSISR, r6

BEGIN_FTR_SECTION
	/* Restore AMR and UAMOR, set AMOR to all 1s */
	ld	r5,VCPU_AMR(r4)
	ld	r6,VCPU_UAMOR(r4)
	li	r7,-1
	mtspr	SPRN_AMR,r5
	mtspr	SPRN_UAMOR,r6
	mtspr	SPRN_AMOR,r7
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_206)

	/* Clear out SLB */
	li	r6,0
	slbmte	r6,r6
	slbia
	ptesync

BEGIN_FTR_SECTION
	b	30f
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_201)
	/*
	 * POWER7 host -> guest partition switch code.
	 * We don't have to lock against concurrent tlbies,
	 * but we do have to coordinate across hardware threads.
	 */
	/* Increment entry count iff exit count is zero. */
	ld	r5,HSTATE_KVM_VCORE(r13)
	addi	r9,r5,VCORE_ENTRY_EXIT
21:	lwarx	r3,0,r9
	cmpwi	r3,0x100		/* any threads starting to exit? */
	bge	secondary_too_late	/* if so we're too late to the party */
	addi	r3,r3,1
	stwcx.	r3,0,r9
	bne	21b

	/* Primary thread switches to guest partition. */
	ld	r9,VCPU_KVM(r4)		/* pointer to struct kvm */
	lwz	r6,VCPU_PTID(r4)
	cmpwi	r6,0
	bne	20f
	ld	r6,KVM_SDR1(r9)
	lwz	r7,KVM_LPID(r9)
	li	r0,LPID_RSVD		/* switch to reserved LPID */
	mtspr	SPRN_LPID,r0
	ptesync
	mtspr	SPRN_SDR1,r6		/* switch to partition page table */
	mtspr	SPRN_LPID,r7
	isync

	/* See if we need to flush the TLB */
	lhz	r6,PACAPACAINDEX(r13)	/* test_bit(cpu, need_tlb_flush) */
	clrldi	r7,r6,64-6		/* extract bit number (6 bits) */
	srdi	r6,r6,6			/* doubleword number */
	sldi	r6,r6,3			/* address offset */
	add	r6,r6,r9
	addi	r6,r6,KVM_NEED_FLUSH	/* dword in kvm->arch.need_tlb_flush */
	li	r0,1
	sld	r0,r0,r7
	ld	r7,0(r6)
	and.	r7,r7,r0
	beq	22f
23:	ldarx	r7,0,r6			/* if set, clear the bit */
	andc	r7,r7,r0
	stdcx.	r7,0,r6
	bne	23b
	li	r6,128			/* and flush the TLB */
	mtctr	r6
	li	r7,0x800		/* IS field = 0b10 */
	ptesync
28:	tlbiel	r7
	addi	r7,r7,0x1000
	bdnz	28b
	ptesync

22:	li	r0,1
	stb	r0,VCORE_IN_GUEST(r5)	/* signal secondaries to continue */
	b	10f

	/* Secondary threads wait for primary to have done partition switch */
20:	lbz	r0,VCORE_IN_GUEST(r5)
	cmpwi	r0,0
	beq	20b

	/* Set LPCR and RMOR. */
10:	ld	r8,KVM_LPCR(r9)
	mtspr	SPRN_LPCR,r8
	ld	r8,KVM_RMOR(r9)
	mtspr	SPRN_RMOR,r8
	isync

	/* Check if HDEC expires soon */
	mfspr	r3,SPRN_HDEC
	cmpwi	r3,10
	li	r12,BOOK3S_INTERRUPT_HV_DECREMENTER
	mr	r9,r4
	blt	hdec_soon

	/* Save purr/spurr */
	mfspr	r5,SPRN_PURR
	mfspr	r6,SPRN_SPURR
	std	r5,HSTATE_PURR(r13)
	std	r6,HSTATE_SPURR(r13)
	ld	r7,VCPU_PURR(r4)
	ld	r8,VCPU_SPURR(r4)
	mtspr	SPRN_PURR,r7
	mtspr	SPRN_SPURR,r8
	b	31f

	/*
	 * PPC970 host -> guest partition switch code.
	 * We have to lock against concurrent tlbies,
	 * using native_tlbie_lock to lock against host tlbies
	 * and kvm->arch.tlbie_lock to lock against guest tlbies.
	 * We also have to invalidate the TLB since its
	 * entries aren't tagged with the LPID.
	 */
30:	ld	r9,VCPU_KVM(r4)		/* pointer to struct kvm */

	/* first take native_tlbie_lock */
	.section ".toc","aw"
toc_tlbie_lock:
	.tc	native_tlbie_lock[TC],native_tlbie_lock
	.previous
	ld	r3,toc_tlbie_lock@toc(2)
	lwz	r8,PACA_LOCK_TOKEN(r13)
24:	lwarx	r0,0,r3
	cmpwi	r0,0
	bne	24b
	stwcx.	r8,0,r3
	bne	24b
	isync

	ld	r7,KVM_LPCR(r9)		/* use kvm->arch.lpcr to store HID4 */
	li	r0,0x18f
	rotldi	r0,r0,HID4_LPID5_SH	/* all lpid bits in HID4 = 1 */
	or	r0,r7,r0
	ptesync
	sync
	mtspr	SPRN_HID4,r0		/* switch to reserved LPID */
	isync
	li	r0,0
	stw	r0,0(r3)		/* drop native_tlbie_lock */

	/* invalidate the whole TLB */
	li	r0,256
	mtctr	r0
	li	r6,0
25:	tlbiel	r6
	addi	r6,r6,0x1000
	bdnz	25b
	ptesync

	/* Take the guest's tlbie_lock */
	addi	r3,r9,KVM_TLBIE_LOCK
24:	lwarx	r0,0,r3
	cmpwi	r0,0
	bne	24b
	stwcx.	r8,0,r3
	bne	24b
	isync
	ld	r6,KVM_SDR1(r9)
	mtspr	SPRN_SDR1,r6		/* switch to partition page table */

	/* Set up HID4 with the guest's LPID etc. */
	sync
	mtspr	SPRN_HID4,r7
	isync

	/* drop the guest's tlbie_lock */
	li	r0,0
	stw	r0,0(r3)

	/* Check if HDEC expires soon */
	mfspr	r3,SPRN_HDEC
	cmpwi	r3,10
	li	r12,BOOK3S_INTERRUPT_HV_DECREMENTER
	mr	r9,r4
	blt	hdec_soon

	/* Enable HDEC interrupts */
	mfspr	r0,SPRN_HID0
	li	r3,1
	rldimi	r0,r3, HID0_HDICE_SH, 64-HID0_HDICE_SH-1
	sync
	mtspr	SPRN_HID0,r0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0

	/* Load up guest SLB entries */
31:	lwz	r5,VCPU_SLB_MAX(r4)
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

	ld	r10, VCPU_PC(r4)
	ld	r11, VCPU_MSR(r4)
kvmppc_cede_reentry:		/* r4 = vcpu, r13 = paca */
	ld	r6, VCPU_SRR0(r4)
	ld	r7, VCPU_SRR1(r4)

	/* r11 = vcpu->arch.msr & ~MSR_HV */
	rldicl	r11, r11, 63 - MSR_HV_LG, 1
	rotldi	r11, r11, 1 + MSR_HV_LG
	ori	r11, r11, MSR_ME

	/* Check if we can deliver an external or decrementer interrupt now */
	ld	r0,VCPU_PENDING_EXC(r4)
	lis	r8,(1 << BOOK3S_IRQPRIO_EXTERNAL_LEVEL)@h
	and	r0,r0,r8
	cmpdi	cr1,r0,0
	andi.	r0,r11,MSR_EE
	beq	cr1,11f
BEGIN_FTR_SECTION
	mfspr	r8,SPRN_LPCR
	ori	r8,r8,LPCR_MER
	mtspr	SPRN_LPCR,r8
	isync
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_206)
	beq	5f
	li	r0,BOOK3S_INTERRUPT_EXTERNAL
12:	mr	r6,r10
	mr	r10,r0
	mr	r7,r11
	li	r11,(MSR_ME << 1) | 1	/* synthesize MSR_SF | MSR_ME */
	rotldi	r11,r11,63
	b	5f
11:	beq	5f
	mfspr	r0,SPRN_DEC
	cmpwi	r0,0
	li	r0,BOOK3S_INTERRUPT_DECREMENTER
	blt	12b

	/* Move SRR0 and SRR1 into the respective regs */
5:	mtspr	SPRN_SRR0, r6
	mtspr	SPRN_SRR1, r7

fast_guest_return:
	li	r0,0
	stb	r0,VCPU_CEDED(r4)	/* cancel cede */
	mtspr	SPRN_HSRR0,r10
	mtspr	SPRN_HSRR1,r11

	/* Activate guest mode, so faults get handled by KVM */
	li	r9, KVM_GUEST_MODE_GUEST
	stb	r9, HSTATE_IN_GUEST(r13)

	/* Enter guest */

BEGIN_FTR_SECTION
	ld	r5, VCPU_CFAR(r4)
	mtspr	SPRN_CFAR, r5
END_FTR_SECTION_IFSET(CPU_FTR_CFAR)

	ld	r5, VCPU_LR(r4)
	lwz	r6, VCPU_CR(r4)
	mtlr	r5
	mtcr	r6

	ld	r0, VCPU_GPR(R0)(r4)
	ld	r1, VCPU_GPR(R1)(r4)
	ld	r2, VCPU_GPR(R2)(r4)
	ld	r3, VCPU_GPR(R3)(r4)
	ld	r5, VCPU_GPR(R5)(r4)
	ld	r6, VCPU_GPR(R6)(r4)
	ld	r7, VCPU_GPR(R7)(r4)
	ld	r8, VCPU_GPR(R8)(r4)
	ld	r9, VCPU_GPR(R9)(r4)
	ld	r10, VCPU_GPR(R10)(r4)
	ld	r11, VCPU_GPR(R11)(r4)
	ld	r12, VCPU_GPR(R12)(r4)
	ld	r13, VCPU_GPR(R13)(r4)

	ld	r4, VCPU_GPR(R4)(r4)

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

	std	r0, VCPU_GPR(R0)(r9)
	std	r1, VCPU_GPR(R1)(r9)
	std	r2, VCPU_GPR(R2)(r9)
	std	r3, VCPU_GPR(R3)(r9)
	std	r4, VCPU_GPR(R4)(r9)
	std	r5, VCPU_GPR(R5)(r9)
	std	r6, VCPU_GPR(R6)(r9)
	std	r7, VCPU_GPR(R7)(r9)
	std	r8, VCPU_GPR(R8)(r9)
	ld	r0, HSTATE_HOST_R2(r13)
	std	r0, VCPU_GPR(R9)(r9)
	std	r10, VCPU_GPR(R10)(r9)
	std	r11, VCPU_GPR(R11)(r9)
	ld	r3, HSTATE_SCRATCH0(r13)
	lwz	r4, HSTATE_SCRATCH1(r13)
	std	r3, VCPU_GPR(R12)(r9)
	stw	r4, VCPU_CR(r9)
BEGIN_FTR_SECTION
	ld	r3, HSTATE_CFAR(r13)
	std	r3, VCPU_CFAR(r9)
END_FTR_SECTION_IFSET(CPU_FTR_CFAR)

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
	std	r3, VCPU_GPR(R13)(r9)
	std	r4, VCPU_LR(r9)

	/* Unset guest mode */
	li	r0, KVM_GUEST_MODE_NONE
	stb	r0, HSTATE_IN_GUEST(r13)

	stw	r12,VCPU_TRAP(r9)

	/* Save HEIR (HV emulation assist reg) in last_inst
	   if this is an HEI (HV emulation interrupt, e40) */
	li	r3,KVM_INST_FETCH_FAILED
BEGIN_FTR_SECTION
	cmpwi	r12,BOOK3S_INTERRUPT_H_EMUL_ASSIST
	bne	11f
	mfspr	r3,SPRN_HEIR
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_206)
11:	stw	r3,VCPU_LAST_INST(r9)

	/* these are volatile across C function calls */
	mfctr	r3
	mfxer	r4
	std	r3, VCPU_CTR(r9)
	stw	r4, VCPU_XER(r9)

BEGIN_FTR_SECTION
	/* If this is a page table miss then see if it's theirs or ours */
	cmpwi	r12, BOOK3S_INTERRUPT_H_DATA_STORAGE
	beq	kvmppc_hdsi
	cmpwi	r12, BOOK3S_INTERRUPT_H_INST_STORAGE
	beq	kvmppc_hisi
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_206)

	/* See if this is a leftover HDEC interrupt */
	cmpwi	r12,BOOK3S_INTERRUPT_HV_DECREMENTER
	bne	2f
	mfspr	r3,SPRN_HDEC
	cmpwi	r3,0
	bge	ignore_hdec
2:
	/* See if this is an hcall we can handle in real mode */
	cmpwi	r12,BOOK3S_INTERRUPT_SYSCALL
	beq	hcall_try_real_mode

	/* Only handle external interrupts here on arch 206 and later */
BEGIN_FTR_SECTION
	b	ext_interrupt_to_host
END_FTR_SECTION_IFCLR(CPU_FTR_ARCH_206)

	/* External interrupt ? */
	cmpwi	r12, BOOK3S_INTERRUPT_EXTERNAL
	bne+	ext_interrupt_to_host

	/* External interrupt, first check for host_ipi. If this is
	 * set, we know the host wants us out so let's do it now
	 */
do_ext_interrupt:
	lbz	r0, HSTATE_HOST_IPI(r13)
	cmpwi	r0, 0
	bne	ext_interrupt_to_host

	/* Now read the interrupt from the ICP */
	ld	r5, HSTATE_XICS_PHYS(r13)
	li	r7, XICS_XIRR
	cmpdi	r5, 0
	beq-	ext_interrupt_to_host
	lwzcix	r3, r5, r7
	rlwinm.	r0, r3, 0, 0xffffff
	sync
	beq	3f		/* if nothing pending in the ICP */

	/* We found something in the ICP...
	 *
	 * If it's not an IPI, stash it in the PACA and return to
	 * the host, we don't (yet) handle directing real external
	 * interrupts directly to the guest
	 */
	cmpwi	r0, XICS_IPI
	bne	ext_stash_for_host

	/* It's an IPI, clear the MFRR and EOI it */
	li	r0, 0xff
	li	r6, XICS_MFRR
	stbcix	r0, r5, r6		/* clear the IPI */
	stwcix	r3, r5, r7		/* EOI it */
	sync

	/* We need to re-check host IPI now in case it got set in the
	 * meantime. If it's clear, we bounce the interrupt to the
	 * guest
	 */
	lbz	r0, HSTATE_HOST_IPI(r13)
	cmpwi	r0, 0
	bne-	1f

	/* Allright, looks like an IPI for the guest, we need to set MER */
3:
	/* Check if any CPU is heading out to the host, if so head out too */
	ld	r5, HSTATE_KVM_VCORE(r13)
	lwz	r0, VCORE_ENTRY_EXIT(r5)
	cmpwi	r0, 0x100
	bge	ext_interrupt_to_host

	/* See if there is a pending interrupt for the guest */
	mfspr	r8, SPRN_LPCR
	ld	r0, VCPU_PENDING_EXC(r9)
	/* Insert EXTERNAL_LEVEL bit into LPCR at the MER bit position */
	rldicl.	r0, r0, 64 - BOOK3S_IRQPRIO_EXTERNAL_LEVEL, 63
	rldimi	r8, r0, LPCR_MER_SH, 63 - LPCR_MER_SH
	beq	2f

	/* And if the guest EE is set, we can deliver immediately, else
	 * we return to the guest with MER set
	 */
	andi.	r0, r11, MSR_EE
	beq	2f
	mtspr	SPRN_SRR0, r10
	mtspr	SPRN_SRR1, r11
	li	r10, BOOK3S_INTERRUPT_EXTERNAL
	li	r11, (MSR_ME << 1) | 1	/* synthesize MSR_SF | MSR_ME */
	rotldi	r11, r11, 63
2:	mr	r4, r9
	mtspr	SPRN_LPCR, r8
	b	fast_guest_return

	/* We raced with the host, we need to resend that IPI, bummer */
1:	li	r0, IPI_PRIORITY
	stbcix	r0, r5, r6		/* set the IPI */
	sync
	b	ext_interrupt_to_host

ext_stash_for_host:
	/* It's not an IPI and it's for the host, stash it in the PACA
	 * before exit, it will be picked up by the host ICP driver
	 */
	stw	r3, HSTATE_SAVED_XIRR(r13)
ext_interrupt_to_host:

guest_exit_cont:		/* r9 = vcpu, r12 = trap, r13 = paca */
	/* Save DEC */
	mfspr	r5,SPRN_DEC
	mftb	r6
	extsw	r5,r5
	add	r5,r5,r6
	std	r5,VCPU_DEC_EXPIRES(r9)

	/* Save more register state  */
	mfdar	r6
	mfdsisr	r7
	std	r6, VCPU_DAR(r9)
	stw	r7, VCPU_DSISR(r9)
BEGIN_FTR_SECTION
	/* don't overwrite fault_dar/fault_dsisr if HDSI */
	cmpwi	r12,BOOK3S_INTERRUPT_H_DATA_STORAGE
	beq	6f
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_206)
	std	r6, VCPU_FAULT_DAR(r9)
	stw	r7, VCPU_FAULT_DSISR(r9)

	/* See if it is a machine check */
	cmpwi	r12, BOOK3S_INTERRUPT_MACHINE_CHECK
	beq	machine_check_realmode
mc_cont:

	/* Save guest CTRL register, set runlatch to 1 */
6:	mfspr	r6,SPRN_CTRLF
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
BEGIN_FTR_SECTION
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
END_FTR_SECTION_IFCLR(CPU_FTR_ARCH_201)

	/* Clear out SLB */
	li	r5,0
	slbmte	r5,r5
	slbia
	ptesync

hdec_soon:			/* r9 = vcpu, r12 = trap, r13 = paca */
BEGIN_FTR_SECTION
	b	32f
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_201)
	/*
	 * POWER7 guest -> host partition switch code.
	 * We don't have to lock against tlbies but we do
	 * have to coordinate the hardware threads.
	 */
	/* Increment the threads-exiting-guest count in the 0xff00
	   bits of vcore->entry_exit_count */
	lwsync
	ld	r5,HSTATE_KVM_VCORE(r13)
	addi	r6,r5,VCORE_ENTRY_EXIT
41:	lwarx	r3,0,r6
	addi	r0,r3,0x100
	stwcx.	r0,0,r6
	bne	41b
	lwsync

	/*
	 * At this point we have an interrupt that we have to pass
	 * up to the kernel or qemu; we can't handle it in real mode.
	 * Thus we have to do a partition switch, so we have to
	 * collect the other threads, if we are the first thread
	 * to take an interrupt.  To do this, we set the HDEC to 0,
	 * which causes an HDEC interrupt in all threads within 2ns
	 * because the HDEC register is shared between all 4 threads.
	 * However, we don't need to bother if this is an HDEC
	 * interrupt, since the other threads will already be on their
	 * way here in that case.
	 */
	cmpwi	r3,0x100	/* Are we the first here? */
	bge	43f
	cmpwi	r3,1		/* Are any other threads in the guest? */
	ble	43f
	cmpwi	r12,BOOK3S_INTERRUPT_HV_DECREMENTER
	beq	40f
	li	r0,0
	mtspr	SPRN_HDEC,r0
40:
	/*
	 * Send an IPI to any napping threads, since an HDEC interrupt
	 * doesn't wake CPUs up from nap.
	 */
	lwz	r3,VCORE_NAPPING_THREADS(r5)
	lwz	r4,VCPU_PTID(r9)
	li	r0,1
	sld	r0,r0,r4
	andc.	r3,r3,r0		/* no sense IPI'ing ourselves */
	beq	43f
	mulli	r4,r4,PACA_SIZE		/* get paca for thread 0 */
	subf	r6,r4,r13
42:	andi.	r0,r3,1
	beq	44f
	ld	r8,HSTATE_XICS_PHYS(r6)	/* get thread's XICS reg addr */
	li	r0,IPI_PRIORITY
	li	r7,XICS_MFRR
	stbcix	r0,r7,r8		/* trigger the IPI */
44:	srdi.	r3,r3,1
	addi	r6,r6,PACA_SIZE
	bne	42b

	/* Secondary threads wait for primary to do partition switch */
43:	ld	r4,VCPU_KVM(r9)		/* pointer to struct kvm */
	ld	r5,HSTATE_KVM_VCORE(r13)
	lwz	r3,VCPU_PTID(r9)
	cmpwi	r3,0
	beq	15f
	HMT_LOW
13:	lbz	r3,VCORE_IN_GUEST(r5)
	cmpwi	r3,0
	bne	13b
	HMT_MEDIUM
	b	16f

	/* Primary thread waits for all the secondaries to exit guest */
15:	lwz	r3,VCORE_ENTRY_EXIT(r5)
	srwi	r0,r3,8
	clrldi	r3,r3,56
	cmpw	r3,r0
	bne	15b
	isync

	/* Primary thread switches back to host partition */
	ld	r6,KVM_HOST_SDR1(r4)
	lwz	r7,KVM_HOST_LPID(r4)
	li	r8,LPID_RSVD		/* switch to reserved LPID */
	mtspr	SPRN_LPID,r8
	ptesync
	mtspr	SPRN_SDR1,r6		/* switch to partition page table */
	mtspr	SPRN_LPID,r7
	isync
	li	r0,0
	stb	r0,VCORE_IN_GUEST(r5)
	lis	r8,0x7fff		/* MAX_INT@h */
	mtspr	SPRN_HDEC,r8

16:	ld	r8,KVM_HOST_LPCR(r4)
	mtspr	SPRN_LPCR,r8
	isync
	b	33f

	/*
	 * PPC970 guest -> host partition switch code.
	 * We have to lock against concurrent tlbies, and
	 * we have to flush the whole TLB.
	 */
32:	ld	r4,VCPU_KVM(r9)		/* pointer to struct kvm */

	/* Take the guest's tlbie_lock */
	lwz	r8,PACA_LOCK_TOKEN(r13)
	addi	r3,r4,KVM_TLBIE_LOCK
24:	lwarx	r0,0,r3
	cmpwi	r0,0
	bne	24b
	stwcx.	r8,0,r3
	bne	24b
	isync

	ld	r7,KVM_HOST_LPCR(r4)	/* use kvm->arch.host_lpcr for HID4 */
	li	r0,0x18f
	rotldi	r0,r0,HID4_LPID5_SH	/* all lpid bits in HID4 = 1 */
	or	r0,r7,r0
	ptesync
	sync
	mtspr	SPRN_HID4,r0		/* switch to reserved LPID */
	isync
	li	r0,0
	stw	r0,0(r3)		/* drop guest tlbie_lock */

	/* invalidate the whole TLB */
	li	r0,256
	mtctr	r0
	li	r6,0
25:	tlbiel	r6
	addi	r6,r6,0x1000
	bdnz	25b
	ptesync

	/* take native_tlbie_lock */
	ld	r3,toc_tlbie_lock@toc(2)
24:	lwarx	r0,0,r3
	cmpwi	r0,0
	bne	24b
	stwcx.	r8,0,r3
	bne	24b
	isync

	ld	r6,KVM_HOST_SDR1(r4)
	mtspr	SPRN_SDR1,r6		/* switch to host page table */

	/* Set up host HID4 value */
	sync
	mtspr	SPRN_HID4,r7
	isync
	li	r0,0
	stw	r0,0(r3)		/* drop native_tlbie_lock */

	lis	r8,0x7fff		/* MAX_INT@h */
	mtspr	SPRN_HDEC,r8

	/* Disable HDEC interrupts */
	mfspr	r0,SPRN_HID0
	li	r3,0
	rldimi	r0,r3, HID0_HDICE_SH, 64-HID0_HDICE_SH-1
	sync
	mtspr	SPRN_HID0,r0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0
	mfspr	r0,SPRN_HID0

	/* load host SLB entries */
33:	ld	r8,PACA_SLBSHADOWPTR(r13)

	.rept	SLB_NUM_BOLTED
	ld	r5,SLBSHADOW_SAVEAREA(r8)
	ld	r6,SLBSHADOW_SAVEAREA+8(r8)
	andis.	r7,r5,SLB_ESID_V@h
	beq	1f
	slbmte	r6,r5
1:	addi	r8,r8,16
	.endr

	/* Save and reset AMR and UAMOR before turning on the MMU */
BEGIN_FTR_SECTION
	mfspr	r5,SPRN_AMR
	mfspr	r6,SPRN_UAMOR
	std	r5,VCPU_AMR(r9)
	std	r6,VCPU_UAMOR(r9)
	li	r6,0
	mtspr	SPRN_AMR,r6
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_206)

	/* Switch DSCR back to host value */
BEGIN_FTR_SECTION
	mfspr	r8, SPRN_DSCR
	ld	r7, HSTATE_DSCR(r13)
	std	r8, VCPU_DSCR(r7)
	mtspr	SPRN_DSCR, r7
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_206)

	/* Save non-volatile GPRs */
	std	r14, VCPU_GPR(R14)(r9)
	std	r15, VCPU_GPR(R15)(r9)
	std	r16, VCPU_GPR(R16)(r9)
	std	r17, VCPU_GPR(R17)(r9)
	std	r18, VCPU_GPR(R18)(r9)
	std	r19, VCPU_GPR(R19)(r9)
	std	r20, VCPU_GPR(R20)(r9)
	std	r21, VCPU_GPR(R21)(r9)
	std	r22, VCPU_GPR(R22)(r9)
	std	r23, VCPU_GPR(R23)(r9)
	std	r24, VCPU_GPR(R24)(r9)
	std	r25, VCPU_GPR(R25)(r9)
	std	r26, VCPU_GPR(R26)(r9)
	std	r27, VCPU_GPR(R27)(r9)
	std	r28, VCPU_GPR(R28)(r9)
	std	r29, VCPU_GPR(R29)(r9)
	std	r30, VCPU_GPR(R30)(r9)
	std	r31, VCPU_GPR(R31)(r9)

	/* Save SPRGs */
	mfspr	r3, SPRN_SPRG0
	mfspr	r4, SPRN_SPRG1
	mfspr	r5, SPRN_SPRG2
	mfspr	r6, SPRN_SPRG3
	std	r3, VCPU_SPRG0(r9)
	std	r4, VCPU_SPRG1(r9)
	std	r5, VCPU_SPRG2(r9)
	std	r6, VCPU_SPRG3(r9)

	/* save FP state */
	mr	r3, r9
	bl	.kvmppc_save_fp

	/* Increment yield count if they have a VPA */
	ld	r8, VCPU_VPA(r9)	/* do they have a VPA? */
	cmpdi	r8, 0
	beq	25f
	lwz	r3, LPPACA_YIELDCOUNT(r8)
	addi	r3, r3, 1
	stw	r3, LPPACA_YIELDCOUNT(r8)
	li	r3, 1
	stb	r3, VCPU_VPA_DIRTY(r9)
25:
	/* Save PMU registers if requested */
	/* r8 and cr0.eq are live here */
	li	r3, 1
	sldi	r3, r3, 31		/* MMCR0_FC (freeze counters) bit */
	mfspr	r4, SPRN_MMCR0		/* save MMCR0 */
	mtspr	SPRN_MMCR0, r3		/* freeze all counters, disable ints */
	mfspr	r6, SPRN_MMCRA
BEGIN_FTR_SECTION
	/* On P7, clear MMCRA in order to disable SDAR updates */
	li	r7, 0
	mtspr	SPRN_MMCRA, r7
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_206)
	isync
	beq	21f			/* if no VPA, save PMU stuff anyway */
	lbz	r7, LPPACA_PMCINUSE(r8)
	cmpwi	r7, 0			/* did they ask for PMU stuff to be saved? */
	bne	21f
	std	r3, VCPU_MMCR(r9)	/* if not, set saved MMCR0 to FC */
	b	22f
21:	mfspr	r5, SPRN_MMCR1
	std	r4, VCPU_MMCR(r9)
	std	r5, VCPU_MMCR + 8(r9)
	std	r6, VCPU_MMCR + 16(r9)
	mfspr	r3, SPRN_PMC1
	mfspr	r4, SPRN_PMC2
	mfspr	r5, SPRN_PMC3
	mfspr	r6, SPRN_PMC4
	mfspr	r7, SPRN_PMC5
	mfspr	r8, SPRN_PMC6
BEGIN_FTR_SECTION
	mfspr	r10, SPRN_PMC7
	mfspr	r11, SPRN_PMC8
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_201)
	stw	r3, VCPU_PMC(r9)
	stw	r4, VCPU_PMC + 4(r9)
	stw	r5, VCPU_PMC + 8(r9)
	stw	r6, VCPU_PMC + 12(r9)
	stw	r7, VCPU_PMC + 16(r9)
	stw	r8, VCPU_PMC + 20(r9)
BEGIN_FTR_SECTION
	stw	r10, VCPU_PMC + 24(r9)
	stw	r11, VCPU_PMC + 28(r9)
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_201)
22:

	/* Secondary threads go off to take a nap on POWER7 */
BEGIN_FTR_SECTION
	lwz	r0,VCPU_PTID(r9)
	cmpwi	r0,0
	bne	secondary_nap
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_206)

	/* Restore host DABR and DABRX */
	ld	r5,HSTATE_DABR(r13)
	li	r6,7
	mtspr	SPRN_DABR,r5
	mtspr	SPRN_DABRX,r6

	/* Restore SPRG3 */
	ld	r3,PACA_SPRG3(r13)
	mtspr	SPRN_SPRG3,r3

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
BEGIN_FTR_SECTION
	lwz	r10, HSTATE_PMC + 24(r13)
	lwz	r11, HSTATE_PMC + 28(r13)
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_201)
	mtspr	SPRN_PMC1, r3
	mtspr	SPRN_PMC2, r4
	mtspr	SPRN_PMC3, r5
	mtspr	SPRN_PMC4, r6
	mtspr	SPRN_PMC5, r8
	mtspr	SPRN_PMC6, r9
BEGIN_FTR_SECTION
	mtspr	SPRN_PMC7, r10
	mtspr	SPRN_PMC8, r11
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_201)
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
	 * We do that by jumping to absolute address 0x500 for
	 * external interrupts, or the machine_check_fwnmi label
	 * for machine checks (since firmware might have patched
	 * the vector area at 0x200).  The [h]rfid at the end of the
	 * handler will return to the book3s_hv_interrupts.S code.
	 * For other interrupts we do the rfid to get back
	 * to the book3s_hv_interrupts.S code here.
	 */
	ld	r8, HSTATE_VMHANDLER(r13)
	ld	r7, HSTATE_HOST_MSR(r13)

	cmpwi	cr1, r12, BOOK3S_INTERRUPT_MACHINE_CHECK
	cmpwi	r12, BOOK3S_INTERRUPT_EXTERNAL
BEGIN_FTR_SECTION
	beq	11f
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_206)

	/* RFI into the highmem handler, or branch to interrupt handler */
	mfmsr	r6
	li	r0, MSR_RI
	andc	r6, r6, r0
	mtmsrd	r6, 1			/* Clear RI in MSR */
	mtsrr0	r8
	mtsrr1	r7
	beqa	0x500			/* external interrupt (PPC970) */
	beq	cr1, 13f		/* machine check */
	RFI

	/* On POWER7, we have external interrupts set to use HSRR0/1 */
11:	mtspr	SPRN_HSRR0, r8
	mtspr	SPRN_HSRR1, r7
	ba	0x500

13:	b	machine_check_fwnmi

/*
 * Check whether an HDSI is an HPTE not found fault or something else.
 * If it is an HPTE not found fault that is due to the guest accessing
 * a page that they have mapped but which we have paged out, then
 * we continue on with the guest exit path.  In all other cases,
 * reflect the HDSI to the guest as a DSI.
 */
kvmppc_hdsi:
	mfspr	r4, SPRN_HDAR
	mfspr	r6, SPRN_HDSISR
	/* HPTE not found fault or protection fault? */
	andis.	r0, r6, (DSISR_NOHPTE | DSISR_PROTFAULT)@h
	beq	1f			/* if not, send it to the guest */
	andi.	r0, r11, MSR_DR		/* data relocation enabled? */
	beq	3f
	clrrdi	r0, r4, 28
	PPC_SLBFEE_DOT(R5, R0)		/* if so, look up SLB */
	bne	1f			/* if no SLB entry found */
4:	std	r4, VCPU_FAULT_DAR(r9)
	stw	r6, VCPU_FAULT_DSISR(r9)

	/* Search the hash table. */
	mr	r3, r9			/* vcpu pointer */
	li	r7, 1			/* data fault */
	bl	.kvmppc_hpte_hv_fault
	ld	r9, HSTATE_KVM_VCPU(r13)
	ld	r10, VCPU_PC(r9)
	ld	r11, VCPU_MSR(r9)
	li	r12, BOOK3S_INTERRUPT_H_DATA_STORAGE
	cmpdi	r3, 0			/* retry the instruction */
	beq	6f
	cmpdi	r3, -1			/* handle in kernel mode */
	beq	guest_exit_cont
	cmpdi	r3, -2			/* MMIO emulation; need instr word */
	beq	2f

	/* Synthesize a DSI for the guest */
	ld	r4, VCPU_FAULT_DAR(r9)
	mr	r6, r3
1:	mtspr	SPRN_DAR, r4
	mtspr	SPRN_DSISR, r6
	mtspr	SPRN_SRR0, r10
	mtspr	SPRN_SRR1, r11
	li	r10, BOOK3S_INTERRUPT_DATA_STORAGE
	li	r11, (MSR_ME << 1) | 1	/* synthesize MSR_SF | MSR_ME */
	rotldi	r11, r11, 63
fast_interrupt_c_return:
6:	ld	r7, VCPU_CTR(r9)
	lwz	r8, VCPU_XER(r9)
	mtctr	r7
	mtxer	r8
	mr	r4, r9
	b	fast_guest_return

3:	ld	r5, VCPU_KVM(r9)	/* not relocated, use VRMA */
	ld	r5, KVM_VRMA_SLB_V(r5)
	b	4b

	/* If this is for emulated MMIO, load the instruction word */
2:	li	r8, KVM_INST_FETCH_FAILED	/* In case lwz faults */

	/* Set guest mode to 'jump over instruction' so if lwz faults
	 * we'll just continue at the next IP. */
	li	r0, KVM_GUEST_MODE_SKIP
	stb	r0, HSTATE_IN_GUEST(r13)

	/* Do the access with MSR:DR enabled */
	mfmsr	r3
	ori	r4, r3, MSR_DR		/* Enable paging for data */
	mtmsrd	r4
	lwz	r8, 0(r10)
	mtmsrd	r3

	/* Store the result */
	stw	r8, VCPU_LAST_INST(r9)

	/* Unset guest mode. */
	li	r0, KVM_GUEST_MODE_NONE
	stb	r0, HSTATE_IN_GUEST(r13)
	b	guest_exit_cont

/*
 * Similarly for an HISI, reflect it to the guest as an ISI unless
 * it is an HPTE not found fault for a page that we have paged out.
 */
kvmppc_hisi:
	andis.	r0, r11, SRR1_ISI_NOPT@h
	beq	1f
	andi.	r0, r11, MSR_IR		/* instruction relocation enabled? */
	beq	3f
	clrrdi	r0, r10, 28
	PPC_SLBFEE_DOT(R5, R0)		/* if so, look up SLB */
	bne	1f			/* if no SLB entry found */
4:
	/* Search the hash table. */
	mr	r3, r9			/* vcpu pointer */
	mr	r4, r10
	mr	r6, r11
	li	r7, 0			/* instruction fault */
	bl	.kvmppc_hpte_hv_fault
	ld	r9, HSTATE_KVM_VCPU(r13)
	ld	r10, VCPU_PC(r9)
	ld	r11, VCPU_MSR(r9)
	li	r12, BOOK3S_INTERRUPT_H_INST_STORAGE
	cmpdi	r3, 0			/* retry the instruction */
	beq	fast_interrupt_c_return
	cmpdi	r3, -1			/* handle in kernel mode */
	beq	guest_exit_cont

	/* Synthesize an ISI for the guest */
	mr	r11, r3
1:	mtspr	SPRN_SRR0, r10
	mtspr	SPRN_SRR1, r11
	li	r10, BOOK3S_INTERRUPT_INST_STORAGE
	li	r11, (MSR_ME << 1) | 1	/* synthesize MSR_SF | MSR_ME */
	rotldi	r11, r11, 63
	b	fast_interrupt_c_return

3:	ld	r6, VCPU_KVM(r9)	/* not relocated, use VRMA */
	ld	r5, KVM_VRMA_SLB_V(r6)
	b	4b

/*
 * Try to handle an hcall in real mode.
 * Returns to the guest if we handle it, or continues on up to
 * the kernel if we can't (i.e. if we don't have a handler for
 * it, or if the handler returns H_TOO_HARD).
 */
	.globl	hcall_try_real_mode
hcall_try_real_mode:
	ld	r3,VCPU_GPR(R3)(r9)
	andi.	r0,r11,MSR_PR
	bne	guest_exit_cont
	clrrdi	r3,r3,2
	cmpldi	r3,hcall_real_table_end - hcall_real_table
	bge	guest_exit_cont
	LOAD_REG_ADDR(r4, hcall_real_table)
	lwzx	r3,r3,r4
	cmpwi	r3,0
	beq	guest_exit_cont
	add	r3,r3,r4
	mtctr	r3
	mr	r3,r9		/* get vcpu pointer */
	ld	r4,VCPU_GPR(R4)(r9)
	bctrl
	cmpdi	r3,H_TOO_HARD
	beq	hcall_real_fallback
	ld	r4,HSTATE_KVM_VCPU(r13)
	std	r3,VCPU_GPR(R3)(r4)
	ld	r10,VCPU_PC(r4)
	ld	r11,VCPU_MSR(r4)
	b	fast_guest_return

	/* We've attempted a real mode hcall, but it's punted it back
	 * to userspace.  We need to restore some clobbered volatiles
	 * before resuming the pass-it-to-qemu path */
hcall_real_fallback:
	li	r12,BOOK3S_INTERRUPT_SYSCALL
	ld	r9, HSTATE_KVM_VCPU(r13)

	b	guest_exit_cont

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
	.long	.kvmppc_h_put_tce - hcall_real_table
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
#ifdef CONFIG_KVM_XICS
	.long	.kvmppc_rm_h_eoi - hcall_real_table
	.long	.kvmppc_rm_h_cppr - hcall_real_table
	.long	.kvmppc_rm_h_ipi - hcall_real_table
	.long	0		/* 0x70 - H_IPOLL */
	.long	.kvmppc_rm_h_xirr - hcall_real_table
#else
	.long	0		/* 0x64 - H_EOI */
	.long	0		/* 0x68 - H_CPPR */
	.long	0		/* 0x6c - H_IPI */
	.long	0		/* 0x70 - H_IPOLL */
	.long	0		/* 0x74 - H_XIRR */
#endif
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
	.long	.kvmppc_h_cede - hcall_real_table
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

_GLOBAL(kvmppc_h_set_dabr)
	std	r4,VCPU_DABR(r3)
	/* Work around P7 bug where DABR can get corrupted on mtspr */
1:	mtspr	SPRN_DABR,r4
	mfspr	r5, SPRN_DABR
	cmpd	r4, r5
	bne	1b
	isync
	li	r3,0
	blr

_GLOBAL(kvmppc_h_cede)
	ori	r11,r11,MSR_EE
	std	r11,VCPU_MSR(r3)
	li	r0,1
	stb	r0,VCPU_CEDED(r3)
	sync			/* order setting ceded vs. testing prodded */
	lbz	r5,VCPU_PRODDED(r3)
	cmpwi	r5,0
	bne	kvm_cede_prodded
	li	r0,0		/* set trap to 0 to say hcall is handled */
	stw	r0,VCPU_TRAP(r3)
	li	r0,H_SUCCESS
	std	r0,VCPU_GPR(R3)(r3)
BEGIN_FTR_SECTION
	b	kvm_cede_exit	/* just send it up to host on 970 */
END_FTR_SECTION_IFCLR(CPU_FTR_ARCH_206)

	/*
	 * Set our bit in the bitmask of napping threads unless all the
	 * other threads are already napping, in which case we send this
	 * up to the host.
	 */
	ld	r5,HSTATE_KVM_VCORE(r13)
	lwz	r6,VCPU_PTID(r3)
	lwz	r8,VCORE_ENTRY_EXIT(r5)
	clrldi	r8,r8,56
	li	r0,1
	sld	r0,r0,r6
	addi	r6,r5,VCORE_NAPPING_THREADS
31:	lwarx	r4,0,r6
	or	r4,r4,r0
	PPC_POPCNTW(R7,R4)
	cmpw	r7,r8
	bge	kvm_cede_exit
	stwcx.	r4,0,r6
	bne	31b
	li	r0,1
	stb	r0,HSTATE_NAPPING(r13)
	/* order napping_threads update vs testing entry_exit_count */
	lwsync
	mr	r4,r3
	lwz	r7,VCORE_ENTRY_EXIT(r5)
	cmpwi	r7,0x100
	bge	33f		/* another thread already exiting */

/*
 * Although not specifically required by the architecture, POWER7
 * preserves the following registers in nap mode, even if an SMT mode
 * switch occurs: SLB entries, PURR, SPURR, AMOR, UAMOR, AMR, SPRG0-3,
 * DAR, DSISR, DABR, DABRX, DSCR, PMCx, MMCRx, SIAR, SDAR.
 */
	/* Save non-volatile GPRs */
	std	r14, VCPU_GPR(R14)(r3)
	std	r15, VCPU_GPR(R15)(r3)
	std	r16, VCPU_GPR(R16)(r3)
	std	r17, VCPU_GPR(R17)(r3)
	std	r18, VCPU_GPR(R18)(r3)
	std	r19, VCPU_GPR(R19)(r3)
	std	r20, VCPU_GPR(R20)(r3)
	std	r21, VCPU_GPR(R21)(r3)
	std	r22, VCPU_GPR(R22)(r3)
	std	r23, VCPU_GPR(R23)(r3)
	std	r24, VCPU_GPR(R24)(r3)
	std	r25, VCPU_GPR(R25)(r3)
	std	r26, VCPU_GPR(R26)(r3)
	std	r27, VCPU_GPR(R27)(r3)
	std	r28, VCPU_GPR(R28)(r3)
	std	r29, VCPU_GPR(R29)(r3)
	std	r30, VCPU_GPR(R30)(r3)
	std	r31, VCPU_GPR(R31)(r3)

	/* save FP state */
	bl	.kvmppc_save_fp

	/*
	 * Take a nap until a decrementer or external interrupt occurs,
	 * with PECE1 (wake on decr) and PECE0 (wake on external) set in LPCR
	 */
	li	r0,1
	stb	r0,HSTATE_HWTHREAD_REQ(r13)
	mfspr	r5,SPRN_LPCR
	ori	r5,r5,LPCR_PECE0 | LPCR_PECE1
	mtspr	SPRN_LPCR,r5
	isync
	li	r0, 0
	std	r0, HSTATE_SCRATCH0(r13)
	ptesync
	ld	r0, HSTATE_SCRATCH0(r13)
1:	cmpd	r0, r0
	bne	1b
	nap
	b	.

kvm_end_cede:
	/* get vcpu pointer */
	ld	r4, HSTATE_KVM_VCPU(r13)

	/* Woken by external or decrementer interrupt */
	ld	r1, HSTATE_HOST_R1(r13)

	/* load up FP state */
	bl	kvmppc_load_fp

	/* Load NV GPRS */
	ld	r14, VCPU_GPR(R14)(r4)
	ld	r15, VCPU_GPR(R15)(r4)
	ld	r16, VCPU_GPR(R16)(r4)
	ld	r17, VCPU_GPR(R17)(r4)
	ld	r18, VCPU_GPR(R18)(r4)
	ld	r19, VCPU_GPR(R19)(r4)
	ld	r20, VCPU_GPR(R20)(r4)
	ld	r21, VCPU_GPR(R21)(r4)
	ld	r22, VCPU_GPR(R22)(r4)
	ld	r23, VCPU_GPR(R23)(r4)
	ld	r24, VCPU_GPR(R24)(r4)
	ld	r25, VCPU_GPR(R25)(r4)
	ld	r26, VCPU_GPR(R26)(r4)
	ld	r27, VCPU_GPR(R27)(r4)
	ld	r28, VCPU_GPR(R28)(r4)
	ld	r29, VCPU_GPR(R29)(r4)
	ld	r30, VCPU_GPR(R30)(r4)
	ld	r31, VCPU_GPR(R31)(r4)

	/* clear our bit in vcore->napping_threads */
33:	ld	r5,HSTATE_KVM_VCORE(r13)
	lwz	r3,VCPU_PTID(r4)
	li	r0,1
	sld	r0,r0,r3
	addi	r6,r5,VCORE_NAPPING_THREADS
32:	lwarx	r7,0,r6
	andc	r7,r7,r0
	stwcx.	r7,0,r6
	bne	32b
	li	r0,0
	stb	r0,HSTATE_NAPPING(r13)

	/* Check the wake reason in SRR1 to see why we got here */
	mfspr	r3, SPRN_SRR1
	rlwinm	r3, r3, 44-31, 0x7	/* extract wake reason field */
	cmpwi	r3, 4			/* was it an external interrupt? */
	li	r12, BOOK3S_INTERRUPT_EXTERNAL
	mr	r9, r4
	ld	r10, VCPU_PC(r9)
	ld	r11, VCPU_MSR(r9)
	beq	do_ext_interrupt	/* if so */

	/* see if any other thread is already exiting */
	lwz	r0,VCORE_ENTRY_EXIT(r5)
	cmpwi	r0,0x100
	blt	kvmppc_cede_reentry	/* if not go back to guest */

	/* some threads are exiting, so go to the guest exit path */
	b	hcall_real_fallback

	/* cede when already previously prodded case */
kvm_cede_prodded:
	li	r0,0
	stb	r0,VCPU_PRODDED(r3)
	sync			/* order testing prodded vs. clearing ceded */
	stb	r0,VCPU_CEDED(r3)
	li	r3,H_SUCCESS
	blr

	/* we've ceded but we want to give control to the host */
kvm_cede_exit:
	b	hcall_real_fallback

	/* Try to handle a machine check in real mode */
machine_check_realmode:
	mr	r3, r9		/* get vcpu pointer */
	bl	.kvmppc_realmode_machine_check
	nop
	cmpdi	r3, 0		/* continue exiting from guest? */
	ld	r9, HSTATE_KVM_VCPU(r13)
	li	r12, BOOK3S_INTERRUPT_MACHINE_CHECK
	beq	mc_cont
	/* If not, deliver a machine check.  SRR0/1 are already set */
	li	r10, BOOK3S_INTERRUPT_MACHINE_CHECK
	li	r11, (MSR_ME << 1) | 1	/* synthesize MSR_SF | MSR_ME */
	rotldi	r11, r11, 63
	b	fast_interrupt_c_return

secondary_too_late:
	ld	r5,HSTATE_KVM_VCORE(r13)
	HMT_LOW
13:	lbz	r3,VCORE_IN_GUEST(r5)
	cmpwi	r3,0
	bne	13b
	HMT_MEDIUM
	ld	r11,PACA_SLBSHADOWPTR(r13)

	.rept	SLB_NUM_BOLTED
	ld	r5,SLBSHADOW_SAVEAREA(r11)
	ld	r6,SLBSHADOW_SAVEAREA+8(r11)
	andis.	r7,r5,SLB_ESID_V@h
	beq	1f
	slbmte	r6,r5
1:	addi	r11,r11,16
	.endr

secondary_nap:
	/* Clear our vcpu pointer so we don't come back in early */
	li	r0, 0
	std	r0, HSTATE_KVM_VCPU(r13)
	lwsync
	/* Clear any pending IPI - assume we're a secondary thread */
	ld	r5, HSTATE_XICS_PHYS(r13)
	li	r7, XICS_XIRR
	lwzcix	r3, r5, r7		/* ack any pending interrupt */
	rlwinm.	r0, r3, 0, 0xffffff	/* any pending? */
	beq	37f
	sync
	li	r0, 0xff
	li	r6, XICS_MFRR
	stbcix	r0, r5, r6		/* clear the IPI */
	stwcix	r3, r5, r7		/* EOI it */
37:	sync

	/* increment the nap count and then go to nap mode */
	ld	r4, HSTATE_KVM_VCORE(r13)
	addi	r4, r4, VCORE_NAP_COUNT
	lwsync				/* make previous updates visible */
51:	lwarx	r3, 0, r4
	addi	r3, r3, 1
	stwcx.	r3, 0, r4
	bne	51b

kvm_no_guest:
	li	r0, KVM_HWTHREAD_IN_NAP
	stb	r0, HSTATE_HWTHREAD_STATE(r13)

	li	r3, LPCR_PECE0
	mfspr	r4, SPRN_LPCR
	rlwimi	r4, r3, 0, LPCR_PECE0 | LPCR_PECE1
	mtspr	SPRN_LPCR, r4
	isync
	std	r0, HSTATE_SCRATCH0(r13)
	ptesync
	ld	r0, HSTATE_SCRATCH0(r13)
1:	cmpd	r0, r0
	bne	1b
	nap
	b	.

/*
 * Save away FP, VMX and VSX registers.
 * r3 = vcpu pointer
 */
_GLOBAL(kvmppc_save_fp)
	mfmsr	r5
	ori	r8,r5,MSR_FP
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
	STXVD2X(reg,R6,R3)
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
	mtmsrd	r5
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
	LXVD2X(reg,R7,R4)
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
