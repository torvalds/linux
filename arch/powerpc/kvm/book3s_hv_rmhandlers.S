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
#include <asm/tm.h>

#define VCPU_GPRS_TM(reg) (((reg) * ULONG_SIZE) + VCPU_GPR_TM)

/* Values in HSTATE_NAPPING(r13) */
#define NAPPING_CEDE	1
#define NAPPING_NOVCPU	2

/*
 * Call kvmppc_hv_entry in real mode.
 * Must be called with interrupts hard-disabled.
 *
 * Input Registers:
 *
 * LR = return address to continue at after eventually re-enabling MMU
 */
_GLOBAL_TOC(kvmppc_hv_entry_trampoline)
	mflr	r0
	std	r0, PPC_LR_STKOFF(r1)
	stdu	r1, -112(r1)
	mfmsr	r10
	LOAD_REG_ADDR(r5, kvmppc_call_hv_entry)
	li	r0,MSR_RI
	andc	r0,r10,r0
	li	r6,MSR_IR | MSR_DR
	andc	r6,r10,r6
	mtmsrd	r0,1		/* clear RI in MSR */
	mtsrr0	r5
	mtsrr1	r6
	RFI

kvmppc_call_hv_entry:
	ld	r4, HSTATE_KVM_VCPU(r13)
	bl	kvmppc_hv_entry

	/* Back from guest - restore host state and return to caller */

BEGIN_FTR_SECTION
	/* Restore host DABR and DABRX */
	ld	r5,HSTATE_DABR(r13)
	li	r6,7
	mtspr	SPRN_DABR,r5
	mtspr	SPRN_DABRX,r6
END_FTR_SECTION_IFCLR(CPU_FTR_ARCH_207S)

	/* Restore SPRG3 */
	ld	r3,PACA_SPRG_VDSO(r13)
	mtspr	SPRN_SPRG_VDSO_WRITE,r3

	/* Reload the host's PMU registers */
	ld	r3, PACALPPACAPTR(r13)	/* is the host using the PMU? */
	lbz	r4, LPPACA_PMCINUSE(r3)
	cmpwi	r4, 0
	beq	23f			/* skip if not */
BEGIN_FTR_SECTION
	ld	r3, HSTATE_MMCR0(r13)
	andi.	r4, r3, MMCR0_PMAO_SYNC | MMCR0_PMAO
	cmpwi	r4, MMCR0_PMAO
	beql	kvmppc_fix_pmao
END_FTR_SECTION_IFSET(CPU_FTR_PMAO_BUG)
	lwz	r3, HSTATE_PMC1(r13)
	lwz	r4, HSTATE_PMC2(r13)
	lwz	r5, HSTATE_PMC3(r13)
	lwz	r6, HSTATE_PMC4(r13)
	lwz	r8, HSTATE_PMC5(r13)
	lwz	r9, HSTATE_PMC6(r13)
	mtspr	SPRN_PMC1, r3
	mtspr	SPRN_PMC2, r4
	mtspr	SPRN_PMC3, r5
	mtspr	SPRN_PMC4, r6
	mtspr	SPRN_PMC5, r8
	mtspr	SPRN_PMC6, r9
	ld	r3, HSTATE_MMCR0(r13)
	ld	r4, HSTATE_MMCR1(r13)
	ld	r5, HSTATE_MMCRA(r13)
	ld	r6, HSTATE_SIAR(r13)
	ld	r7, HSTATE_SDAR(r13)
	mtspr	SPRN_MMCR1, r4
	mtspr	SPRN_MMCRA, r5
	mtspr	SPRN_SIAR, r6
	mtspr	SPRN_SDAR, r7
BEGIN_FTR_SECTION
	ld	r8, HSTATE_MMCR2(r13)
	ld	r9, HSTATE_SIER(r13)
	mtspr	SPRN_MMCR2, r8
	mtspr	SPRN_SIER, r9
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)
	mtspr	SPRN_MMCR0, r3
	isync
23:

	/*
	 * Reload DEC.  HDEC interrupts were disabled when
	 * we reloaded the host's LPCR value.
	 */
	ld	r3, HSTATE_DECEXP(r13)
	mftb	r4
	subf	r4, r4, r3
	mtspr	SPRN_DEC, r4

	/* hwthread_req may have got set by cede or no vcpu, so clear it */
	li	r0, 0
	stb	r0, HSTATE_HWTHREAD_REQ(r13)

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
	ld	r8, 112+PPC_LR_STKOFF(r1)
	addi	r1, r1, 112
	ld	r7, HSTATE_HOST_MSR(r13)

	cmpwi	cr1, r12, BOOK3S_INTERRUPT_MACHINE_CHECK
	cmpwi	r12, BOOK3S_INTERRUPT_EXTERNAL
	beq	11f
	cmpwi	r12, BOOK3S_INTERRUPT_H_DOORBELL
	beq 	15f	/* Invoke the H_DOORBELL handler */
	cmpwi	cr2, r12, BOOK3S_INTERRUPT_HMI
	beq	cr2, 14f			/* HMI check */

	/* RFI into the highmem handler, or branch to interrupt handler */
	mfmsr	r6
	li	r0, MSR_RI
	andc	r6, r6, r0
	mtmsrd	r6, 1			/* Clear RI in MSR */
	mtsrr0	r8
	mtsrr1	r7
	beq	cr1, 13f		/* machine check */
	RFI

	/* On POWER7, we have external interrupts set to use HSRR0/1 */
11:	mtspr	SPRN_HSRR0, r8
	mtspr	SPRN_HSRR1, r7
	ba	0x500

13:	b	machine_check_fwnmi

14:	mtspr	SPRN_HSRR0, r8
	mtspr	SPRN_HSRR1, r7
	b	hmi_exception_after_realmode

15:	mtspr SPRN_HSRR0, r8
	mtspr SPRN_HSRR1, r7
	ba    0xe80

kvmppc_primary_no_guest:
	/* We handle this much like a ceded vcpu */
	/* put the HDEC into the DEC, since HDEC interrupts don't wake us */
	mfspr	r3, SPRN_HDEC
	mtspr	SPRN_DEC, r3
	/*
	 * Make sure the primary has finished the MMU switch.
	 * We should never get here on a secondary thread, but
	 * check it for robustness' sake.
	 */
	ld	r5, HSTATE_KVM_VCORE(r13)
65:	lbz	r0, VCORE_IN_GUEST(r5)
	cmpwi	r0, 0
	beq	65b
	/* Set LPCR. */
	ld	r8,VCORE_LPCR(r5)
	mtspr	SPRN_LPCR,r8
	isync
	/* set our bit in napping_threads */
	ld	r5, HSTATE_KVM_VCORE(r13)
	lbz	r7, HSTATE_PTID(r13)
	li	r0, 1
	sld	r0, r0, r7
	addi	r6, r5, VCORE_NAPPING_THREADS
1:	lwarx	r3, 0, r6
	or	r3, r3, r0
	stwcx.	r3, 0, r6
	bne	1b
	/* order napping_threads update vs testing entry_exit_map */
	isync
	li	r12, 0
	lwz	r7, VCORE_ENTRY_EXIT(r5)
	cmpwi	r7, 0x100
	bge	kvm_novcpu_exit	/* another thread already exiting */
	li	r3, NAPPING_NOVCPU
	stb	r3, HSTATE_NAPPING(r13)

	li	r3, 0		/* Don't wake on privileged (OS) doorbell */
	b	kvm_do_nap

kvm_novcpu_wakeup:
	ld	r1, HSTATE_HOST_R1(r13)
	ld	r5, HSTATE_KVM_VCORE(r13)
	li	r0, 0
	stb	r0, HSTATE_NAPPING(r13)

	/* check the wake reason */
	bl	kvmppc_check_wake_reason

	/* see if any other thread is already exiting */
	lwz	r0, VCORE_ENTRY_EXIT(r5)
	cmpwi	r0, 0x100
	bge	kvm_novcpu_exit

	/* clear our bit in napping_threads */
	lbz	r7, HSTATE_PTID(r13)
	li	r0, 1
	sld	r0, r0, r7
	addi	r6, r5, VCORE_NAPPING_THREADS
4:	lwarx	r7, 0, r6
	andc	r7, r7, r0
	stwcx.	r7, 0, r6
	bne	4b

	/* See if the wake reason means we need to exit */
	cmpdi	r3, 0
	bge	kvm_novcpu_exit

	/* See if our timeslice has expired (HDEC is negative) */
	mfspr	r0, SPRN_HDEC
	li	r12, BOOK3S_INTERRUPT_HV_DECREMENTER
	cmpwi	r0, 0
	blt	kvm_novcpu_exit

	/* Got an IPI but other vcpus aren't yet exiting, must be a latecomer */
	ld	r4, HSTATE_KVM_VCPU(r13)
	cmpdi	r4, 0
	beq	kvmppc_primary_no_guest

#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
	addi	r3, r4, VCPU_TB_RMENTRY
	bl	kvmhv_start_timing
#endif
	b	kvmppc_got_guest

kvm_novcpu_exit:
#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
	ld	r4, HSTATE_KVM_VCPU(r13)
	cmpdi	r4, 0
	beq	13f
	addi	r3, r4, VCPU_TB_RMEXIT
	bl	kvmhv_accumulate_time
#endif
13:	mr	r3, r12
	stw	r12, 112-4(r1)
	bl	kvmhv_commence_exit
	nop
	lwz	r12, 112-4(r1)
	b	kvmhv_switch_to_host

/*
 * We come in here when wakened from nap mode.
 * Relocation is off and most register values are lost.
 * r13 points to the PACA.
 */
	.globl	kvm_start_guest
kvm_start_guest:

	/* Set runlatch bit the minute you wake up from nap */
	mfspr	r0, SPRN_CTRLF
	ori 	r0, r0, 1
	mtspr	SPRN_CTRLT, r0

	ld	r2,PACATOC(r13)

	li	r0,KVM_HWTHREAD_IN_KVM
	stb	r0,HSTATE_HWTHREAD_STATE(r13)

	/* NV GPR values from power7_idle() will no longer be valid */
	li	r0,1
	stb	r0,PACA_NAPSTATELOST(r13)

	/* were we napping due to cede? */
	lbz	r0,HSTATE_NAPPING(r13)
	cmpwi	r0,NAPPING_CEDE
	beq	kvm_end_cede
	cmpwi	r0,NAPPING_NOVCPU
	beq	kvm_novcpu_wakeup

	ld	r1,PACAEMERGSP(r13)
	subi	r1,r1,STACK_FRAME_OVERHEAD

	/*
	 * We weren't napping due to cede, so this must be a secondary
	 * thread being woken up to run a guest, or being woken up due
	 * to a stray IPI.  (Or due to some machine check or hypervisor
	 * maintenance interrupt while the core is in KVM.)
	 */

	/* Check the wake reason in SRR1 to see why we got here */
	bl	kvmppc_check_wake_reason
	cmpdi	r3, 0
	bge	kvm_no_guest

	/* get vcore pointer, NULL if we have nothing to run */
	ld	r5,HSTATE_KVM_VCORE(r13)
	cmpdi	r5,0
	/* if we have no vcore to run, go back to sleep */
	beq	kvm_no_guest

kvm_secondary_got_guest:

	/* Set HSTATE_DSCR(r13) to something sensible */
	ld	r6, PACA_DSCR_DEFAULT(r13)
	std	r6, HSTATE_DSCR(r13)

	/* On thread 0 of a subcore, set HDEC to max */
	lbz	r4, HSTATE_PTID(r13)
	cmpwi	r4, 0
	bne	63f
	lis	r6, 0x7fff
	ori	r6, r6, 0xffff
	mtspr	SPRN_HDEC, r6
	/* and set per-LPAR registers, if doing dynamic micro-threading */
	ld	r6, HSTATE_SPLIT_MODE(r13)
	cmpdi	r6, 0
	beq	63f
	ld	r0, KVM_SPLIT_RPR(r6)
	mtspr	SPRN_RPR, r0
	ld	r0, KVM_SPLIT_PMMAR(r6)
	mtspr	SPRN_PMMAR, r0
	ld	r0, KVM_SPLIT_LDBAR(r6)
	mtspr	SPRN_LDBAR, r0
	isync
63:
	/* Order load of vcpu after load of vcore */
	lwsync
	ld	r4, HSTATE_KVM_VCPU(r13)
	bl	kvmppc_hv_entry

	/* Back from the guest, go back to nap */
	/* Clear our vcpu and vcore pointers so we don't come back in early */
	li	r0, 0
	std	r0, HSTATE_KVM_VCPU(r13)
	/*
	 * Once we clear HSTATE_KVM_VCORE(r13), the code in
	 * kvmppc_run_core() is going to assume that all our vcpu
	 * state is visible in memory.  This lwsync makes sure
	 * that that is true.
	 */
	lwsync
	std	r0, HSTATE_KVM_VCORE(r13)

/*
 * At this point we have finished executing in the guest.
 * We need to wait for hwthread_req to become zero, since
 * we may not turn on the MMU while hwthread_req is non-zero.
 * While waiting we also need to check if we get given a vcpu to run.
 */
kvm_no_guest:
	lbz	r3, HSTATE_HWTHREAD_REQ(r13)
	cmpwi	r3, 0
	bne	53f
	HMT_MEDIUM
	li	r0, KVM_HWTHREAD_IN_KERNEL
	stb	r0, HSTATE_HWTHREAD_STATE(r13)
	/* need to recheck hwthread_req after a barrier, to avoid race */
	sync
	lbz	r3, HSTATE_HWTHREAD_REQ(r13)
	cmpwi	r3, 0
	bne	54f
/*
 * We jump to power7_wakeup_loss, which will return to the caller
 * of power7_nap in the powernv cpu offline loop.  The value we
 * put in r3 becomes the return value for power7_nap.
 */
	li	r3, LPCR_PECE0
	mfspr	r4, SPRN_LPCR
	rlwimi	r4, r3, 0, LPCR_PECE0 | LPCR_PECE1
	mtspr	SPRN_LPCR, r4
	li	r3, 0
	b	power7_wakeup_loss

53:	HMT_LOW
	ld	r5, HSTATE_KVM_VCORE(r13)
	cmpdi	r5, 0
	bne	60f
	ld	r3, HSTATE_SPLIT_MODE(r13)
	cmpdi	r3, 0
	beq	kvm_no_guest
	lbz	r0, KVM_SPLIT_DO_NAP(r3)
	cmpwi	r0, 0
	beq	kvm_no_guest
	HMT_MEDIUM
	b	kvm_unsplit_nap
60:	HMT_MEDIUM
	b	kvm_secondary_got_guest

54:	li	r0, KVM_HWTHREAD_IN_KVM
	stb	r0, HSTATE_HWTHREAD_STATE(r13)
	b	kvm_no_guest

/*
 * Here the primary thread is trying to return the core to
 * whole-core mode, so we need to nap.
 */
kvm_unsplit_nap:
	/*
	 * Ensure that secondary doesn't nap when it has
	 * its vcore pointer set.
	 */
	sync		/* matches smp_mb() before setting split_info.do_nap */
	ld	r0, HSTATE_KVM_VCORE(r13)
	cmpdi	r0, 0
	bne	kvm_no_guest
	/* clear any pending message */
BEGIN_FTR_SECTION
	lis	r6, (PPC_DBELL_SERVER << (63-36))@h
	PPC_MSGCLR(6)
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)
	/* Set kvm_split_mode.napped[tid] = 1 */
	ld	r3, HSTATE_SPLIT_MODE(r13)
	li	r0, 1
	lhz	r4, PACAPACAINDEX(r13)
	clrldi	r4, r4, 61	/* micro-threading => P8 => 8 threads/core */
	addi	r4, r4, KVM_SPLIT_NAPPED
	stbx	r0, r3, r4
	/* Check the do_nap flag again after setting napped[] */
	sync
	lbz	r0, KVM_SPLIT_DO_NAP(r3)
	cmpwi	r0, 0
	beq	57f
	li	r3, (LPCR_PECEDH | LPCR_PECE0) >> 4
	mfspr	r4, SPRN_LPCR
	rlwimi	r4, r3, 4, (LPCR_PECEDP | LPCR_PECEDH | LPCR_PECE0 | LPCR_PECE1)
	mtspr	SPRN_LPCR, r4
	isync
	std	r0, HSTATE_SCRATCH0(r13)
	ptesync
	ld	r0, HSTATE_SCRATCH0(r13)
1:	cmpd	r0, r0
	bne	1b
	nap
	b	.

57:	li	r0, 0
	stbx	r0, r3, r4
	b	kvm_no_guest

/******************************************************************************
 *                                                                            *
 *                               Entry code                                   *
 *                                                                            *
 *****************************************************************************/

.global kvmppc_hv_entry
kvmppc_hv_entry:

	/* Required state:
	 *
	 * R4 = vcpu pointer (or NULL)
	 * MSR = ~IR|DR
	 * R13 = PACA
	 * R1 = host R1
	 * R2 = TOC
	 * all other volatile GPRS = free
	 */
	mflr	r0
	std	r0, PPC_LR_STKOFF(r1)
	stdu	r1, -112(r1)

	/* Save R1 in the PACA */
	std	r1, HSTATE_HOST_R1(r13)

	li	r6, KVM_GUEST_MODE_HOST_HV
	stb	r6, HSTATE_IN_GUEST(r13)

#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
	/* Store initial timestamp */
	cmpdi	r4, 0
	beq	1f
	addi	r3, r4, VCPU_TB_RMENTRY
	bl	kvmhv_start_timing
1:
#endif
	/* Clear out SLB */
	li	r6,0
	slbmte	r6,r6
	slbia
	ptesync

	/*
	 * POWER7/POWER8 host -> guest partition switch code.
	 * We don't have to lock against concurrent tlbies,
	 * but we do have to coordinate across hardware threads.
	 */
	/* Set bit in entry map iff exit map is zero. */
	ld	r5, HSTATE_KVM_VCORE(r13)
	li	r7, 1
	lbz	r6, HSTATE_PTID(r13)
	sld	r7, r7, r6
	addi	r9, r5, VCORE_ENTRY_EXIT
21:	lwarx	r3, 0, r9
	cmpwi	r3, 0x100		/* any threads starting to exit? */
	bge	secondary_too_late	/* if so we're too late to the party */
	or	r3, r3, r7
	stwcx.	r3, 0, r9
	bne	21b

	/* Primary thread switches to guest partition. */
	ld	r9,VCORE_KVM(r5)	/* pointer to struct kvm */
	cmpwi	r6,0
	bne	10f
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
	/* Flush the TLB of any entries for this LPID */
	/* use arch 2.07S as a proxy for POWER8 */
BEGIN_FTR_SECTION
	li	r6,512			/* POWER8 has 512 sets */
FTR_SECTION_ELSE
	li	r6,128			/* POWER7 has 128 sets */
ALT_FTR_SECTION_END_IFSET(CPU_FTR_ARCH_207S)
	mtctr	r6
	li	r7,0x800		/* IS field = 0b10 */
	ptesync
28:	tlbiel	r7
	addi	r7,r7,0x1000
	bdnz	28b
	ptesync

	/* Add timebase offset onto timebase */
22:	ld	r8,VCORE_TB_OFFSET(r5)
	cmpdi	r8,0
	beq	37f
	mftb	r6		/* current host timebase */
	add	r8,r8,r6
	mtspr	SPRN_TBU40,r8	/* update upper 40 bits */
	mftb	r7		/* check if lower 24 bits overflowed */
	clrldi	r6,r6,40
	clrldi	r7,r7,40
	cmpld	r7,r6
	bge	37f
	addis	r8,r8,0x100	/* if so, increment upper 40 bits */
	mtspr	SPRN_TBU40,r8

	/* Load guest PCR value to select appropriate compat mode */
37:	ld	r7, VCORE_PCR(r5)
	cmpdi	r7, 0
	beq	38f
	mtspr	SPRN_PCR, r7
38:

BEGIN_FTR_SECTION
	/* DPDES is shared between threads */
	ld	r8, VCORE_DPDES(r5)
	mtspr	SPRN_DPDES, r8
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)

	li	r0,1
	stb	r0,VCORE_IN_GUEST(r5)	/* signal secondaries to continue */

	/* Do we have a guest vcpu to run? */
10:	cmpdi	r4, 0
	beq	kvmppc_primary_no_guest
kvmppc_got_guest:

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
	/* Increment yield count if they have a VPA */
	ld	r3, VCPU_VPA(r4)
	cmpdi	r3, 0
	beq	25f
	li	r6, LPPACA_YIELDCOUNT
	LWZX_BE	r5, r3, r6
	addi	r5, r5, 1
	STWX_BE	r5, r3, r6
	li	r6, 1
	stb	r6, VCPU_VPA_DIRTY(r4)
25:

	/* Save purr/spurr */
	mfspr	r5,SPRN_PURR
	mfspr	r6,SPRN_SPURR
	std	r5,HSTATE_PURR(r13)
	std	r6,HSTATE_SPURR(r13)
	ld	r7,VCPU_PURR(r4)
	ld	r8,VCPU_SPURR(r4)
	mtspr	SPRN_PURR,r7
	mtspr	SPRN_SPURR,r8

BEGIN_FTR_SECTION
	/* Set partition DABR */
	/* Do this before re-enabling PMU to avoid P7 DABR corruption bug */
	lwz	r5,VCPU_DABRX(r4)
	ld	r6,VCPU_DABR(r4)
	mtspr	SPRN_DABRX,r5
	mtspr	SPRN_DABR,r6
	isync
END_FTR_SECTION_IFCLR(CPU_FTR_ARCH_207S)

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
BEGIN_FTR_SECTION
	bl	kvmppc_restore_tm
END_FTR_SECTION_IFSET(CPU_FTR_TM)
#endif

	/* Load guest PMU registers */
	/* R4 is live here (vcpu pointer) */
	li	r3, 1
	sldi	r3, r3, 31		/* MMCR0_FC (freeze counters) bit */
	mtspr	SPRN_MMCR0, r3		/* freeze all counters, disable ints */
	isync
BEGIN_FTR_SECTION
	ld	r3, VCPU_MMCR(r4)
	andi.	r5, r3, MMCR0_PMAO_SYNC | MMCR0_PMAO
	cmpwi	r5, MMCR0_PMAO
	beql	kvmppc_fix_pmao
END_FTR_SECTION_IFSET(CPU_FTR_PMAO_BUG)
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
	ld	r7, VCPU_SIAR(r4)
	ld	r8, VCPU_SDAR(r4)
	mtspr	SPRN_MMCR1, r5
	mtspr	SPRN_MMCRA, r6
	mtspr	SPRN_SIAR, r7
	mtspr	SPRN_SDAR, r8
BEGIN_FTR_SECTION
	ld	r5, VCPU_MMCR + 24(r4)
	ld	r6, VCPU_SIER(r4)
	lwz	r7, VCPU_PMC + 24(r4)
	lwz	r8, VCPU_PMC + 28(r4)
	ld	r9, VCPU_MMCR + 32(r4)
	mtspr	SPRN_MMCR2, r5
	mtspr	SPRN_SIER, r6
	mtspr	SPRN_SPMC1, r7
	mtspr	SPRN_SPMC2, r8
	mtspr	SPRN_MMCRS, r9
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)
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

	/* Switch DSCR to guest value */
	ld	r5, VCPU_DSCR(r4)
	mtspr	SPRN_DSCR, r5

BEGIN_FTR_SECTION
	/* Skip next section on POWER7 */
	b	8f
END_FTR_SECTION_IFCLR(CPU_FTR_ARCH_207S)
	/* Load up POWER8-specific registers */
	ld	r5, VCPU_IAMR(r4)
	lwz	r6, VCPU_PSPB(r4)
	ld	r7, VCPU_FSCR(r4)
	mtspr	SPRN_IAMR, r5
	mtspr	SPRN_PSPB, r6
	mtspr	SPRN_FSCR, r7
	ld	r5, VCPU_DAWR(r4)
	ld	r6, VCPU_DAWRX(r4)
	ld	r7, VCPU_CIABR(r4)
	ld	r8, VCPU_TAR(r4)
	mtspr	SPRN_DAWR, r5
	mtspr	SPRN_DAWRX, r6
	mtspr	SPRN_CIABR, r7
	mtspr	SPRN_TAR, r8
	ld	r5, VCPU_IC(r4)
	ld	r6, VCPU_VTB(r4)
	mtspr	SPRN_IC, r5
	mtspr	SPRN_VTB, r6
	ld	r8, VCPU_EBBHR(r4)
	mtspr	SPRN_EBBHR, r8
	ld	r5, VCPU_EBBRR(r4)
	ld	r6, VCPU_BESCR(r4)
	ld	r7, VCPU_CSIGR(r4)
	ld	r8, VCPU_TACR(r4)
	mtspr	SPRN_EBBRR, r5
	mtspr	SPRN_BESCR, r6
	mtspr	SPRN_CSIGR, r7
	mtspr	SPRN_TACR, r8
	ld	r5, VCPU_TCSCR(r4)
	ld	r6, VCPU_ACOP(r4)
	lwz	r7, VCPU_GUEST_PID(r4)
	ld	r8, VCPU_WORT(r4)
	mtspr	SPRN_TCSCR, r5
	mtspr	SPRN_ACOP, r6
	mtspr	SPRN_PID, r7
	mtspr	SPRN_WORT, r8
8:

	/*
	 * Set the decrementer to the guest decrementer.
	 */
	ld	r8,VCPU_DEC_EXPIRES(r4)
	/* r8 is a host timebase value here, convert to guest TB */
	ld	r5,HSTATE_KVM_VCORE(r13)
	ld	r6,VCORE_TB_OFFSET(r5)
	add	r8,r8,r6
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

	/* Load up DAR and DSISR */
	ld	r5, VCPU_DAR(r4)
	lwz	r6, VCPU_DSISR(r4)
	mtspr	SPRN_DAR, r5
	mtspr	SPRN_DSISR, r6

	/* Restore AMR and UAMOR, set AMOR to all 1s */
	ld	r5,VCPU_AMR(r4)
	ld	r6,VCPU_UAMOR(r4)
	li	r7,-1
	mtspr	SPRN_AMR,r5
	mtspr	SPRN_UAMOR,r6
	mtspr	SPRN_AMOR,r7

	/* Restore state of CTRL run bit; assume 1 on entry */
	lwz	r5,VCPU_CTRL(r4)
	andi.	r5,r5,1
	bne	4f
	mfspr	r6,SPRN_CTRLF
	clrrdi	r6,r6,1
	mtspr	SPRN_CTRLT,r6
4:
	/* Secondary threads wait for primary to have done partition switch */
	ld	r5, HSTATE_KVM_VCORE(r13)
	lbz	r6, HSTATE_PTID(r13)
	cmpwi	r6, 0
	beq	21f
	lbz	r0, VCORE_IN_GUEST(r5)
	cmpwi	r0, 0
	bne	21f
	HMT_LOW
20:	lwz	r3, VCORE_ENTRY_EXIT(r5)
	cmpwi	r3, 0x100
	bge	no_switch_exit
	lbz	r0, VCORE_IN_GUEST(r5)
	cmpwi	r0, 0
	beq	20b
	HMT_MEDIUM
21:
	/* Set LPCR. */
	ld	r8,VCORE_LPCR(r5)
	mtspr	SPRN_LPCR,r8
	isync

	/* Check if HDEC expires soon */
	mfspr	r3, SPRN_HDEC
	cmpwi	r3, 512		/* 1 microsecond */
	blt	hdec_soon

	ld	r6, VCPU_CTR(r4)
	ld	r7, VCPU_XER(r4)

	mtctr	r6
	mtxer	r7

kvmppc_cede_reentry:		/* r4 = vcpu, r13 = paca */
	ld	r10, VCPU_PC(r4)
	ld	r11, VCPU_MSR(r4)
	ld	r6, VCPU_SRR0(r4)
	ld	r7, VCPU_SRR1(r4)
	mtspr	SPRN_SRR0, r6
	mtspr	SPRN_SRR1, r7

deliver_guest_interrupt:
	/* r11 = vcpu->arch.msr & ~MSR_HV */
	rldicl	r11, r11, 63 - MSR_HV_LG, 1
	rotldi	r11, r11, 1 + MSR_HV_LG
	ori	r11, r11, MSR_ME

	/* Check if we can deliver an external or decrementer interrupt now */
	ld	r0, VCPU_PENDING_EXC(r4)
	rldicl	r0, r0, 64 - BOOK3S_IRQPRIO_EXTERNAL_LEVEL, 63
	cmpdi	cr1, r0, 0
	andi.	r8, r11, MSR_EE
	mfspr	r8, SPRN_LPCR
	/* Insert EXTERNAL_LEVEL bit into LPCR at the MER bit position */
	rldimi	r8, r0, LPCR_MER_SH, 63 - LPCR_MER_SH
	mtspr	SPRN_LPCR, r8
	isync
	beq	5f
	li	r0, BOOK3S_INTERRUPT_EXTERNAL
	bne	cr1, 12f
	mfspr	r0, SPRN_DEC
	cmpwi	r0, 0
	li	r0, BOOK3S_INTERRUPT_DECREMENTER
	bge	5f

12:	mtspr	SPRN_SRR0, r10
	mr	r10,r0
	mtspr	SPRN_SRR1, r11
	mr	r9, r4
	bl	kvmppc_msr_interrupt
5:

/*
 * Required state:
 * R4 = vcpu
 * R10: value for HSRR0
 * R11: value for HSRR1
 * R13 = PACA
 */
fast_guest_return:
	li	r0,0
	stb	r0,VCPU_CEDED(r4)	/* cancel cede */
	mtspr	SPRN_HSRR0,r10
	mtspr	SPRN_HSRR1,r11

	/* Activate guest mode, so faults get handled by KVM */
	li	r9, KVM_GUEST_MODE_GUEST_HV
	stb	r9, HSTATE_IN_GUEST(r13)

#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
	/* Accumulate timing */
	addi	r3, r4, VCPU_TB_GUEST
	bl	kvmhv_accumulate_time
#endif

	/* Enter guest */

BEGIN_FTR_SECTION
	ld	r5, VCPU_CFAR(r4)
	mtspr	SPRN_CFAR, r5
END_FTR_SECTION_IFSET(CPU_FTR_CFAR)
BEGIN_FTR_SECTION
	ld	r0, VCPU_PPR(r4)
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)

	ld	r5, VCPU_LR(r4)
	lwz	r6, VCPU_CR(r4)
	mtlr	r5
	mtcr	r6

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

BEGIN_FTR_SECTION
	mtspr	SPRN_PPR, r0
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)
	ld	r0, VCPU_GPR(R0)(r4)
	ld	r4, VCPU_GPR(R4)(r4)

	hrfid
	b	.

secondary_too_late:
	li	r12, 0
	cmpdi	r4, 0
	beq	11f
	stw	r12, VCPU_TRAP(r4)
#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
	addi	r3, r4, VCPU_TB_RMEXIT
	bl	kvmhv_accumulate_time
#endif
11:	b	kvmhv_switch_to_host

no_switch_exit:
	HMT_MEDIUM
	li	r12, 0
	b	12f
hdec_soon:
	li	r12, BOOK3S_INTERRUPT_HV_DECREMENTER
12:	stw	r12, VCPU_TRAP(r4)
	mr	r9, r4
#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
	addi	r3, r4, VCPU_TB_RMEXIT
	bl	kvmhv_accumulate_time
#endif
	b	guest_exit_cont

/******************************************************************************
 *                                                                            *
 *                               Exit code                                    *
 *                                                                            *
 *****************************************************************************/

/*
 * We come here from the first-level interrupt handlers.
 */
	.globl	kvmppc_interrupt_hv
kvmppc_interrupt_hv:
	/*
	 * Register contents:
	 * R12		= interrupt vector
	 * R13		= PACA
	 * guest CR, R12 saved in shadow VCPU SCRATCH1/0
	 * guest R13 saved in SPRN_SCRATCH0
	 */
	std	r9, HSTATE_SCRATCH2(r13)

	lbz	r9, HSTATE_IN_GUEST(r13)
	cmpwi	r9, KVM_GUEST_MODE_HOST_HV
	beq	kvmppc_bad_host_intr
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	cmpwi	r9, KVM_GUEST_MODE_GUEST
	ld	r9, HSTATE_SCRATCH2(r13)
	beq	kvmppc_interrupt_pr
#endif
	/* We're now back in the host but in guest MMU context */
	li	r9, KVM_GUEST_MODE_HOST_HV
	stb	r9, HSTATE_IN_GUEST(r13)

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
	ld	r0, HSTATE_SCRATCH2(r13)
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
BEGIN_FTR_SECTION
	ld	r4, HSTATE_PPR(r13)
	std	r4, VCPU_PPR(r9)
END_FTR_SECTION_IFSET(CPU_FTR_HAS_PPR)

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

	stw	r12,VCPU_TRAP(r9)

#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
	addi	r3, r9, VCPU_TB_RMINTR
	mr	r4, r9
	bl	kvmhv_accumulate_time
	ld	r5, VCPU_GPR(R5)(r9)
	ld	r6, VCPU_GPR(R6)(r9)
	ld	r7, VCPU_GPR(R7)(r9)
	ld	r8, VCPU_GPR(R8)(r9)
#endif

	/* Save HEIR (HV emulation assist reg) in emul_inst
	   if this is an HEI (HV emulation interrupt, e40) */
	li	r3,KVM_INST_FETCH_FAILED
	stw	r3,VCPU_LAST_INST(r9)
	cmpwi	r12,BOOK3S_INTERRUPT_H_EMUL_ASSIST
	bne	11f
	mfspr	r3,SPRN_HEIR
11:	stw	r3,VCPU_HEIR(r9)

	/* these are volatile across C function calls */
	mfctr	r3
	mfxer	r4
	std	r3, VCPU_CTR(r9)
	std	r4, VCPU_XER(r9)

	/* If this is a page table miss then see if it's theirs or ours */
	cmpwi	r12, BOOK3S_INTERRUPT_H_DATA_STORAGE
	beq	kvmppc_hdsi
	cmpwi	r12, BOOK3S_INTERRUPT_H_INST_STORAGE
	beq	kvmppc_hisi

	/* See if this is a leftover HDEC interrupt */
	cmpwi	r12,BOOK3S_INTERRUPT_HV_DECREMENTER
	bne	2f
	mfspr	r3,SPRN_HDEC
	cmpwi	r3,0
	mr	r4,r9
	bge	fast_guest_return
2:
	/* See if this is an hcall we can handle in real mode */
	cmpwi	r12,BOOK3S_INTERRUPT_SYSCALL
	beq	hcall_try_real_mode

	/* Hypervisor doorbell - exit only if host IPI flag set */
	cmpwi	r12, BOOK3S_INTERRUPT_H_DOORBELL
	bne	3f
	lbz	r0, HSTATE_HOST_IPI(r13)
	cmpwi	r0, 0
	beq	4f
	b	guest_exit_cont
3:
	/* External interrupt ? */
	cmpwi	r12, BOOK3S_INTERRUPT_EXTERNAL
	bne+	guest_exit_cont

	/* External interrupt, first check for host_ipi. If this is
	 * set, we know the host wants us out so let's do it now
	 */
	bl	kvmppc_read_intr
	cmpdi	r3, 0
	bgt	guest_exit_cont

	/* Check if any CPU is heading out to the host, if so head out too */
4:	ld	r5, HSTATE_KVM_VCORE(r13)
	lwz	r0, VCORE_ENTRY_EXIT(r5)
	cmpwi	r0, 0x100
	mr	r4, r9
	blt	deliver_guest_interrupt

guest_exit_cont:		/* r9 = vcpu, r12 = trap, r13 = paca */
	/* Save more register state  */
	mfdar	r6
	mfdsisr	r7
	std	r6, VCPU_DAR(r9)
	stw	r7, VCPU_DSISR(r9)
	/* don't overwrite fault_dar/fault_dsisr if HDSI */
	cmpwi	r12,BOOK3S_INTERRUPT_H_DATA_STORAGE
	beq	mc_cont
	std	r6, VCPU_FAULT_DAR(r9)
	stw	r7, VCPU_FAULT_DSISR(r9)

	/* See if it is a machine check */
	cmpwi	r12, BOOK3S_INTERRUPT_MACHINE_CHECK
	beq	machine_check_realmode
mc_cont:
#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
	addi	r3, r9, VCPU_TB_RMEXIT
	mr	r4, r9
	bl	kvmhv_accumulate_time
#endif

	mr 	r3, r12
	/* Increment exit count, poke other threads to exit */
	bl	kvmhv_commence_exit
	nop
	ld	r9, HSTATE_KVM_VCPU(r13)
	lwz	r12, VCPU_TRAP(r9)

	/* Stop others sending VCPU interrupts to this physical CPU */
	li	r0, -1
	stw	r0, VCPU_CPU(r9)
	stw	r0, VCPU_THREAD_CPU(r9)

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

	/* Save DEC */
	mfspr	r5,SPRN_DEC
	mftb	r6
	extsw	r5,r5
	add	r5,r5,r6
	/* r5 is a guest timebase value here, convert to host TB */
	ld	r3,HSTATE_KVM_VCORE(r13)
	ld	r4,VCORE_TB_OFFSET(r3)
	subf	r5,r4,r5
	std	r5,VCPU_DEC_EXPIRES(r9)

BEGIN_FTR_SECTION
	b	8f
END_FTR_SECTION_IFCLR(CPU_FTR_ARCH_207S)
	/* Save POWER8-specific registers */
	mfspr	r5, SPRN_IAMR
	mfspr	r6, SPRN_PSPB
	mfspr	r7, SPRN_FSCR
	std	r5, VCPU_IAMR(r9)
	stw	r6, VCPU_PSPB(r9)
	std	r7, VCPU_FSCR(r9)
	mfspr	r5, SPRN_IC
	mfspr	r6, SPRN_VTB
	mfspr	r7, SPRN_TAR
	std	r5, VCPU_IC(r9)
	std	r6, VCPU_VTB(r9)
	std	r7, VCPU_TAR(r9)
	mfspr	r8, SPRN_EBBHR
	std	r8, VCPU_EBBHR(r9)
	mfspr	r5, SPRN_EBBRR
	mfspr	r6, SPRN_BESCR
	mfspr	r7, SPRN_CSIGR
	mfspr	r8, SPRN_TACR
	std	r5, VCPU_EBBRR(r9)
	std	r6, VCPU_BESCR(r9)
	std	r7, VCPU_CSIGR(r9)
	std	r8, VCPU_TACR(r9)
	mfspr	r5, SPRN_TCSCR
	mfspr	r6, SPRN_ACOP
	mfspr	r7, SPRN_PID
	mfspr	r8, SPRN_WORT
	std	r5, VCPU_TCSCR(r9)
	std	r6, VCPU_ACOP(r9)
	stw	r7, VCPU_GUEST_PID(r9)
	std	r8, VCPU_WORT(r9)
	/*
	 * Restore various registers to 0, where non-zero values
	 * set by the guest could disrupt the host.
	 */
	li	r0, 0
	mtspr	SPRN_IAMR, r0
	mtspr	SPRN_CIABR, r0
	mtspr	SPRN_DAWRX, r0
	mtspr	SPRN_TCSCR, r0
	mtspr	SPRN_WORT, r0
	/* Set MMCRS to 1<<31 to freeze and disable the SPMC counters */
	li	r0, 1
	sldi	r0, r0, 31
	mtspr	SPRN_MMCRS, r0
8:

	/* Save and reset AMR and UAMOR before turning on the MMU */
	mfspr	r5,SPRN_AMR
	mfspr	r6,SPRN_UAMOR
	std	r5,VCPU_AMR(r9)
	std	r6,VCPU_UAMOR(r9)
	li	r6,0
	mtspr	SPRN_AMR,r6

	/* Switch DSCR back to host value */
	mfspr	r8, SPRN_DSCR
	ld	r7, HSTATE_DSCR(r13)
	std	r8, VCPU_DSCR(r9)
	mtspr	SPRN_DSCR, r7

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
	bl	kvmppc_save_fp

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
BEGIN_FTR_SECTION
	bl	kvmppc_save_tm
END_FTR_SECTION_IFSET(CPU_FTR_TM)
#endif

	/* Increment yield count if they have a VPA */
	ld	r8, VCPU_VPA(r9)	/* do they have a VPA? */
	cmpdi	r8, 0
	beq	25f
	li	r4, LPPACA_YIELDCOUNT
	LWZX_BE	r3, r8, r4
	addi	r3, r3, 1
	STWX_BE	r3, r8, r4
	li	r3, 1
	stb	r3, VCPU_VPA_DIRTY(r9)
25:
	/* Save PMU registers if requested */
	/* r8 and cr0.eq are live here */
BEGIN_FTR_SECTION
	/*
	 * POWER8 seems to have a hardware bug where setting
	 * MMCR0[PMAE] along with MMCR0[PMC1CE] and/or MMCR0[PMCjCE]
	 * when some counters are already negative doesn't seem
	 * to cause a performance monitor alert (and hence interrupt).
	 * The effect of this is that when saving the PMU state,
	 * if there is no PMU alert pending when we read MMCR0
	 * before freezing the counters, but one becomes pending
	 * before we read the counters, we lose it.
	 * To work around this, we need a way to freeze the counters
	 * before reading MMCR0.  Normally, freezing the counters
	 * is done by writing MMCR0 (to set MMCR0[FC]) which
	 * unavoidably writes MMCR0[PMA0] as well.  On POWER8,
	 * we can also freeze the counters using MMCR2, by writing
	 * 1s to all the counter freeze condition bits (there are
	 * 9 bits each for 6 counters).
	 */
	li	r3, -1			/* set all freeze bits */
	clrrdi	r3, r3, 10
	mfspr	r10, SPRN_MMCR2
	mtspr	SPRN_MMCR2, r3
	isync
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)
	li	r3, 1
	sldi	r3, r3, 31		/* MMCR0_FC (freeze counters) bit */
	mfspr	r4, SPRN_MMCR0		/* save MMCR0 */
	mtspr	SPRN_MMCR0, r3		/* freeze all counters, disable ints */
	mfspr	r6, SPRN_MMCRA
	/* Clear MMCRA in order to disable SDAR updates */
	li	r7, 0
	mtspr	SPRN_MMCRA, r7
	isync
	beq	21f			/* if no VPA, save PMU stuff anyway */
	lbz	r7, LPPACA_PMCINUSE(r8)
	cmpwi	r7, 0			/* did they ask for PMU stuff to be saved? */
	bne	21f
	std	r3, VCPU_MMCR(r9)	/* if not, set saved MMCR0 to FC */
	b	22f
21:	mfspr	r5, SPRN_MMCR1
	mfspr	r7, SPRN_SIAR
	mfspr	r8, SPRN_SDAR
	std	r4, VCPU_MMCR(r9)
	std	r5, VCPU_MMCR + 8(r9)
	std	r6, VCPU_MMCR + 16(r9)
BEGIN_FTR_SECTION
	std	r10, VCPU_MMCR + 24(r9)
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)
	std	r7, VCPU_SIAR(r9)
	std	r8, VCPU_SDAR(r9)
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
BEGIN_FTR_SECTION
	mfspr	r5, SPRN_SIER
	mfspr	r6, SPRN_SPMC1
	mfspr	r7, SPRN_SPMC2
	mfspr	r8, SPRN_MMCRS
	std	r5, VCPU_SIER(r9)
	stw	r6, VCPU_PMC + 24(r9)
	stw	r7, VCPU_PMC + 28(r9)
	std	r8, VCPU_MMCR + 32(r9)
	lis	r4, 0x8000
	mtspr	SPRN_MMCRS, r4
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)
22:
	/* Clear out SLB */
	li	r5,0
	slbmte	r5,r5
	slbia
	ptesync

	/*
	 * POWER7/POWER8 guest -> host partition switch code.
	 * We don't have to lock against tlbies but we do
	 * have to coordinate the hardware threads.
	 */
kvmhv_switch_to_host:
	/* Secondary threads wait for primary to do partition switch */
	ld	r5,HSTATE_KVM_VCORE(r13)
	ld	r4,VCORE_KVM(r5)	/* pointer to struct kvm */
	lbz	r3,HSTATE_PTID(r13)
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
	rlwinm	r0,r3,32-8,0xff
	clrldi	r3,r3,56
	cmpw	r3,r0
	bne	15b
	isync

	/* Did we actually switch to the guest at all? */
	lbz	r6, VCORE_IN_GUEST(r5)
	cmpwi	r6, 0
	beq	19f

	/* Primary thread switches back to host partition */
	ld	r6,KVM_HOST_SDR1(r4)
	lwz	r7,KVM_HOST_LPID(r4)
	li	r8,LPID_RSVD		/* switch to reserved LPID */
	mtspr	SPRN_LPID,r8
	ptesync
	mtspr	SPRN_SDR1,r6		/* switch to partition page table */
	mtspr	SPRN_LPID,r7
	isync

BEGIN_FTR_SECTION
	/* DPDES is shared between threads */
	mfspr	r7, SPRN_DPDES
	std	r7, VCORE_DPDES(r5)
	/* clear DPDES so we don't get guest doorbells in the host */
	li	r8, 0
	mtspr	SPRN_DPDES, r8
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)

	/* Subtract timebase offset from timebase */
	ld	r8,VCORE_TB_OFFSET(r5)
	cmpdi	r8,0
	beq	17f
	mftb	r6			/* current guest timebase */
	subf	r8,r8,r6
	mtspr	SPRN_TBU40,r8		/* update upper 40 bits */
	mftb	r7			/* check if lower 24 bits overflowed */
	clrldi	r6,r6,40
	clrldi	r7,r7,40
	cmpld	r7,r6
	bge	17f
	addis	r8,r8,0x100		/* if so, increment upper 40 bits */
	mtspr	SPRN_TBU40,r8

	/* Reset PCR */
17:	ld	r0, VCORE_PCR(r5)
	cmpdi	r0, 0
	beq	18f
	li	r0, 0
	mtspr	SPRN_PCR, r0
18:
	/* Signal secondary CPUs to continue */
	stb	r0,VCORE_IN_GUEST(r5)
19:	lis	r8,0x7fff		/* MAX_INT@h */
	mtspr	SPRN_HDEC,r8

16:	ld	r8,KVM_HOST_LPCR(r4)
	mtspr	SPRN_LPCR,r8
	isync

	/* load host SLB entries */
	ld	r8,PACA_SLBSHADOWPTR(r13)

	.rept	SLB_NUM_BOLTED
	li	r3, SLBSHADOW_SAVEAREA
	LDX_BE	r5, r8, r3
	addi	r3, r3, 8
	LDX_BE	r6, r8, r3
	andis.	r7,r5,SLB_ESID_V@h
	beq	1f
	slbmte	r6,r5
1:	addi	r8,r8,16
	.endr

#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
	/* Finish timing, if we have a vcpu */
	ld	r4, HSTATE_KVM_VCPU(r13)
	cmpdi	r4, 0
	li	r3, 0
	beq	2f
	bl	kvmhv_accumulate_time
2:
#endif
	/* Unset guest mode */
	li	r0, KVM_GUEST_MODE_NONE
	stb	r0, HSTATE_IN_GUEST(r13)

	ld	r0, 112+PPC_LR_STKOFF(r1)
	addi	r1, r1, 112
	mtlr	r0
	blr

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
	li	r0, BOOK3S_INTERRUPT_DATA_SEGMENT
	bne	7f			/* if no SLB entry found */
4:	std	r4, VCPU_FAULT_DAR(r9)
	stw	r6, VCPU_FAULT_DSISR(r9)

	/* Search the hash table. */
	mr	r3, r9			/* vcpu pointer */
	li	r7, 1			/* data fault */
	bl	kvmppc_hpte_hv_fault
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

	/* Synthesize a DSI (or DSegI) for the guest */
	ld	r4, VCPU_FAULT_DAR(r9)
	mr	r6, r3
1:	li	r0, BOOK3S_INTERRUPT_DATA_STORAGE
	mtspr	SPRN_DSISR, r6
7:	mtspr	SPRN_DAR, r4
	mtspr	SPRN_SRR0, r10
	mtspr	SPRN_SRR1, r11
	mr	r10, r0
	bl	kvmppc_msr_interrupt
fast_interrupt_c_return:
6:	ld	r7, VCPU_CTR(r9)
	ld	r8, VCPU_XER(r9)
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
	li	r0, KVM_GUEST_MODE_HOST_HV
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
	li	r0, BOOK3S_INTERRUPT_INST_SEGMENT
	bne	7f			/* if no SLB entry found */
4:
	/* Search the hash table. */
	mr	r3, r9			/* vcpu pointer */
	mr	r4, r10
	mr	r6, r11
	li	r7, 0			/* instruction fault */
	bl	kvmppc_hpte_hv_fault
	ld	r9, HSTATE_KVM_VCPU(r13)
	ld	r10, VCPU_PC(r9)
	ld	r11, VCPU_MSR(r9)
	li	r12, BOOK3S_INTERRUPT_H_INST_STORAGE
	cmpdi	r3, 0			/* retry the instruction */
	beq	fast_interrupt_c_return
	cmpdi	r3, -1			/* handle in kernel mode */
	beq	guest_exit_cont

	/* Synthesize an ISI (or ISegI) for the guest */
	mr	r11, r3
1:	li	r0, BOOK3S_INTERRUPT_INST_STORAGE
7:	mtspr	SPRN_SRR0, r10
	mtspr	SPRN_SRR1, r11
	mr	r10, r0
	bl	kvmppc_msr_interrupt
	b	fast_interrupt_c_return

3:	ld	r6, VCPU_KVM(r9)	/* not relocated, use VRMA */
	ld	r5, KVM_VRMA_SLB_V(r6)
	b	4b

/*
 * Try to handle an hcall in real mode.
 * Returns to the guest if we handle it, or continues on up to
 * the kernel if we can't (i.e. if we don't have a handler for
 * it, or if the handler returns H_TOO_HARD).
 *
 * r5 - r8 contain hcall args,
 * r9 = vcpu, r10 = pc, r11 = msr, r12 = trap, r13 = paca
 */
hcall_try_real_mode:
	ld	r3,VCPU_GPR(R3)(r9)
	andi.	r0,r11,MSR_PR
	/* sc 1 from userspace - reflect to guest syscall */
	bne	sc_1_fast_return
	clrrdi	r3,r3,2
	cmpldi	r3,hcall_real_table_end - hcall_real_table
	bge	guest_exit_cont
	/* See if this hcall is enabled for in-kernel handling */
	ld	r4, VCPU_KVM(r9)
	srdi	r0, r3, 8	/* r0 = (r3 / 4) >> 6 */
	sldi	r0, r0, 3	/* index into kvm->arch.enabled_hcalls[] */
	add	r4, r4, r0
	ld	r0, KVM_ENABLED_HCALLS(r4)
	rlwinm	r4, r3, 32-2, 0x3f	/* r4 = (r3 / 4) & 0x3f */
	srd	r0, r0, r4
	andi.	r0, r0, 1
	beq	guest_exit_cont
	/* Get pointer to handler, if any, and call it */
	LOAD_REG_ADDR(r4, hcall_real_table)
	lwax	r3,r3,r4
	cmpwi	r3,0
	beq	guest_exit_cont
	add	r12,r3,r4
	mtctr	r12
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

sc_1_fast_return:
	mtspr	SPRN_SRR0,r10
	mtspr	SPRN_SRR1,r11
	li	r10, BOOK3S_INTERRUPT_SYSCALL
	bl	kvmppc_msr_interrupt
	mr	r4,r9
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
	.long	DOTSYM(kvmppc_h_remove) - hcall_real_table
	.long	DOTSYM(kvmppc_h_enter) - hcall_real_table
	.long	DOTSYM(kvmppc_h_read) - hcall_real_table
	.long	DOTSYM(kvmppc_h_clear_mod) - hcall_real_table
	.long	DOTSYM(kvmppc_h_clear_ref) - hcall_real_table
	.long	DOTSYM(kvmppc_h_protect) - hcall_real_table
	.long	DOTSYM(kvmppc_h_get_tce) - hcall_real_table
	.long	DOTSYM(kvmppc_h_put_tce) - hcall_real_table
	.long	0		/* 0x24 - H_SET_SPRG0 */
	.long	DOTSYM(kvmppc_h_set_dabr) - hcall_real_table
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
	.long	DOTSYM(kvmppc_rm_h_eoi) - hcall_real_table
	.long	DOTSYM(kvmppc_rm_h_cppr) - hcall_real_table
	.long	DOTSYM(kvmppc_rm_h_ipi) - hcall_real_table
	.long	0		/* 0x70 - H_IPOLL */
	.long	DOTSYM(kvmppc_rm_h_xirr) - hcall_real_table
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
	.long	DOTSYM(kvmppc_h_cede) - hcall_real_table
	.long	DOTSYM(kvmppc_rm_h_confer) - hcall_real_table
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
	.long	DOTSYM(kvmppc_h_bulk_remove) - hcall_real_table
	.long	0		/* 0x128 */
	.long	0		/* 0x12c */
	.long	0		/* 0x130 */
	.long	DOTSYM(kvmppc_h_set_xdabr) - hcall_real_table
	.long	0		/* 0x138 */
	.long	0		/* 0x13c */
	.long	0		/* 0x140 */
	.long	0		/* 0x144 */
	.long	0		/* 0x148 */
	.long	0		/* 0x14c */
	.long	0		/* 0x150 */
	.long	0		/* 0x154 */
	.long	0		/* 0x158 */
	.long	0		/* 0x15c */
	.long	0		/* 0x160 */
	.long	0		/* 0x164 */
	.long	0		/* 0x168 */
	.long	0		/* 0x16c */
	.long	0		/* 0x170 */
	.long	0		/* 0x174 */
	.long	0		/* 0x178 */
	.long	0		/* 0x17c */
	.long	0		/* 0x180 */
	.long	0		/* 0x184 */
	.long	0		/* 0x188 */
	.long	0		/* 0x18c */
	.long	0		/* 0x190 */
	.long	0		/* 0x194 */
	.long	0		/* 0x198 */
	.long	0		/* 0x19c */
	.long	0		/* 0x1a0 */
	.long	0		/* 0x1a4 */
	.long	0		/* 0x1a8 */
	.long	0		/* 0x1ac */
	.long	0		/* 0x1b0 */
	.long	0		/* 0x1b4 */
	.long	0		/* 0x1b8 */
	.long	0		/* 0x1bc */
	.long	0		/* 0x1c0 */
	.long	0		/* 0x1c4 */
	.long	0		/* 0x1c8 */
	.long	0		/* 0x1cc */
	.long	0		/* 0x1d0 */
	.long	0		/* 0x1d4 */
	.long	0		/* 0x1d8 */
	.long	0		/* 0x1dc */
	.long	0		/* 0x1e0 */
	.long	0		/* 0x1e4 */
	.long	0		/* 0x1e8 */
	.long	0		/* 0x1ec */
	.long	0		/* 0x1f0 */
	.long	0		/* 0x1f4 */
	.long	0		/* 0x1f8 */
	.long	0		/* 0x1fc */
	.long	0		/* 0x200 */
	.long	0		/* 0x204 */
	.long	0		/* 0x208 */
	.long	0		/* 0x20c */
	.long	0		/* 0x210 */
	.long	0		/* 0x214 */
	.long	0		/* 0x218 */
	.long	0		/* 0x21c */
	.long	0		/* 0x220 */
	.long	0		/* 0x224 */
	.long	0		/* 0x228 */
	.long	0		/* 0x22c */
	.long	0		/* 0x230 */
	.long	0		/* 0x234 */
	.long	0		/* 0x238 */
	.long	0		/* 0x23c */
	.long	0		/* 0x240 */
	.long	0		/* 0x244 */
	.long	0		/* 0x248 */
	.long	0		/* 0x24c */
	.long	0		/* 0x250 */
	.long	0		/* 0x254 */
	.long	0		/* 0x258 */
	.long	0		/* 0x25c */
	.long	0		/* 0x260 */
	.long	0		/* 0x264 */
	.long	0		/* 0x268 */
	.long	0		/* 0x26c */
	.long	0		/* 0x270 */
	.long	0		/* 0x274 */
	.long	0		/* 0x278 */
	.long	0		/* 0x27c */
	.long	0		/* 0x280 */
	.long	0		/* 0x284 */
	.long	0		/* 0x288 */
	.long	0		/* 0x28c */
	.long	0		/* 0x290 */
	.long	0		/* 0x294 */
	.long	0		/* 0x298 */
	.long	0		/* 0x29c */
	.long	0		/* 0x2a0 */
	.long	0		/* 0x2a4 */
	.long	0		/* 0x2a8 */
	.long	0		/* 0x2ac */
	.long	0		/* 0x2b0 */
	.long	0		/* 0x2b4 */
	.long	0		/* 0x2b8 */
	.long	0		/* 0x2bc */
	.long	0		/* 0x2c0 */
	.long	0		/* 0x2c4 */
	.long	0		/* 0x2c8 */
	.long	0		/* 0x2cc */
	.long	0		/* 0x2d0 */
	.long	0		/* 0x2d4 */
	.long	0		/* 0x2d8 */
	.long	0		/* 0x2dc */
	.long	0		/* 0x2e0 */
	.long	0		/* 0x2e4 */
	.long	0		/* 0x2e8 */
	.long	0		/* 0x2ec */
	.long	0		/* 0x2f0 */
	.long	0		/* 0x2f4 */
	.long	0		/* 0x2f8 */
	.long	0		/* 0x2fc */
	.long	DOTSYM(kvmppc_h_random) - hcall_real_table
	.globl	hcall_real_table_end
hcall_real_table_end:

_GLOBAL(kvmppc_h_set_xdabr)
	andi.	r0, r5, DABRX_USER | DABRX_KERNEL
	beq	6f
	li	r0, DABRX_USER | DABRX_KERNEL | DABRX_BTI
	andc.	r0, r5, r0
	beq	3f
6:	li	r3, H_PARAMETER
	blr

_GLOBAL(kvmppc_h_set_dabr)
	li	r5, DABRX_USER | DABRX_KERNEL
3:
BEGIN_FTR_SECTION
	b	2f
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)
	std	r4,VCPU_DABR(r3)
	stw	r5, VCPU_DABRX(r3)
	mtspr	SPRN_DABRX, r5
	/* Work around P7 bug where DABR can get corrupted on mtspr */
1:	mtspr	SPRN_DABR,r4
	mfspr	r5, SPRN_DABR
	cmpd	r4, r5
	bne	1b
	isync
	li	r3,0
	blr

	/* Emulate H_SET_DABR/X on P8 for the sake of compat mode guests */
2:	rlwimi	r5, r4, 5, DAWRX_DR | DAWRX_DW
	rlwimi	r5, r4, 2, DAWRX_WT
	clrrdi	r4, r4, 3
	std	r4, VCPU_DAWR(r3)
	std	r5, VCPU_DAWRX(r3)
	mtspr	SPRN_DAWR, r4
	mtspr	SPRN_DAWRX, r5
	li	r3, 0
	blr

_GLOBAL(kvmppc_h_cede)		/* r3 = vcpu pointer, r11 = msr, r13 = paca */
	ori	r11,r11,MSR_EE
	std	r11,VCPU_MSR(r3)
	li	r0,1
	stb	r0,VCPU_CEDED(r3)
	sync			/* order setting ceded vs. testing prodded */
	lbz	r5,VCPU_PRODDED(r3)
	cmpwi	r5,0
	bne	kvm_cede_prodded
	li	r12,0		/* set trap to 0 to say hcall is handled */
	stw	r12,VCPU_TRAP(r3)
	li	r0,H_SUCCESS
	std	r0,VCPU_GPR(R3)(r3)

	/*
	 * Set our bit in the bitmask of napping threads unless all the
	 * other threads are already napping, in which case we send this
	 * up to the host.
	 */
	ld	r5,HSTATE_KVM_VCORE(r13)
	lbz	r6,HSTATE_PTID(r13)
	lwz	r8,VCORE_ENTRY_EXIT(r5)
	clrldi	r8,r8,56
	li	r0,1
	sld	r0,r0,r6
	addi	r6,r5,VCORE_NAPPING_THREADS
31:	lwarx	r4,0,r6
	or	r4,r4,r0
	cmpw	r4,r8
	beq	kvm_cede_exit
	stwcx.	r4,0,r6
	bne	31b
	/* order napping_threads update vs testing entry_exit_map */
	isync
	li	r0,NAPPING_CEDE
	stb	r0,HSTATE_NAPPING(r13)
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
	bl	kvmppc_save_fp

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
BEGIN_FTR_SECTION
	ld	r9, HSTATE_KVM_VCPU(r13)
	bl	kvmppc_save_tm
END_FTR_SECTION_IFSET(CPU_FTR_TM)
#endif

	/*
	 * Set DEC to the smaller of DEC and HDEC, so that we wake
	 * no later than the end of our timeslice (HDEC interrupts
	 * don't wake us from nap).
	 */
	mfspr	r3, SPRN_DEC
	mfspr	r4, SPRN_HDEC
	mftb	r5
	cmpw	r3, r4
	ble	67f
	mtspr	SPRN_DEC, r4
67:
	/* save expiry time of guest decrementer */
	extsw	r3, r3
	add	r3, r3, r5
	ld	r4, HSTATE_KVM_VCPU(r13)
	ld	r5, HSTATE_KVM_VCORE(r13)
	ld	r6, VCORE_TB_OFFSET(r5)
	subf	r3, r6, r3	/* convert to host TB value */
	std	r3, VCPU_DEC_EXPIRES(r4)

#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
	ld	r4, HSTATE_KVM_VCPU(r13)
	addi	r3, r4, VCPU_TB_CEDE
	bl	kvmhv_accumulate_time
#endif

	lis	r3, LPCR_PECEDP@h	/* Do wake on privileged doorbell */

	/*
	 * Take a nap until a decrementer or external or doobell interrupt
	 * occurs, with PECE1 and PECE0 set in LPCR.
	 * On POWER8, set PECEDH, and if we are ceding, also set PECEDP.
	 * Also clear the runlatch bit before napping.
	 */
kvm_do_nap:
	mfspr	r0, SPRN_CTRLF
	clrrdi	r0, r0, 1
	mtspr	SPRN_CTRLT, r0

	li	r0,1
	stb	r0,HSTATE_HWTHREAD_REQ(r13)
	mfspr	r5,SPRN_LPCR
	ori	r5,r5,LPCR_PECE0 | LPCR_PECE1
BEGIN_FTR_SECTION
	ori	r5, r5, LPCR_PECEDH
	rlwimi	r5, r3, 0, LPCR_PECEDP
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)
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

33:	mr	r4, r3
	li	r3, 0
	li	r12, 0
	b	34f

kvm_end_cede:
	/* get vcpu pointer */
	ld	r4, HSTATE_KVM_VCPU(r13)

	/* Woken by external or decrementer interrupt */
	ld	r1, HSTATE_HOST_R1(r13)

#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
	addi	r3, r4, VCPU_TB_RMINTR
	bl	kvmhv_accumulate_time
#endif

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
BEGIN_FTR_SECTION
	bl	kvmppc_restore_tm
END_FTR_SECTION_IFSET(CPU_FTR_TM)
#endif

	/* load up FP state */
	bl	kvmppc_load_fp

	/* Restore guest decrementer */
	ld	r3, VCPU_DEC_EXPIRES(r4)
	ld	r5, HSTATE_KVM_VCORE(r13)
	ld	r6, VCORE_TB_OFFSET(r5)
	add	r3, r3, r6	/* convert host TB to guest TB value */
	mftb	r7
	subf	r3, r7, r3
	mtspr	SPRN_DEC, r3

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
 
	/* Check the wake reason in SRR1 to see why we got here */
	bl	kvmppc_check_wake_reason

	/* clear our bit in vcore->napping_threads */
34:	ld	r5,HSTATE_KVM_VCORE(r13)
	lbz	r7,HSTATE_PTID(r13)
	li	r0,1
	sld	r0,r0,r7
	addi	r6,r5,VCORE_NAPPING_THREADS
32:	lwarx	r7,0,r6
	andc	r7,r7,r0
	stwcx.	r7,0,r6
	bne	32b
	li	r0,0
	stb	r0,HSTATE_NAPPING(r13)

	/* See if the wake reason means we need to exit */
	stw	r12, VCPU_TRAP(r4)
	mr	r9, r4
	cmpdi	r3, 0
	bgt	guest_exit_cont

	/* see if any other thread is already exiting */
	lwz	r0,VCORE_ENTRY_EXIT(r5)
	cmpwi	r0,0x100
	bge	guest_exit_cont

	b	kvmppc_cede_reentry	/* if not go back to guest */

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
	ld	r9, HSTATE_KVM_VCPU(r13)
	b	guest_exit_cont

	/* Try to handle a machine check in real mode */
machine_check_realmode:
	mr	r3, r9		/* get vcpu pointer */
	bl	kvmppc_realmode_machine_check
	nop
	ld	r9, HSTATE_KVM_VCPU(r13)
	li	r12, BOOK3S_INTERRUPT_MACHINE_CHECK
	/*
	 * Deliver unhandled/fatal (e.g. UE) MCE errors to guest through
	 * machine check interrupt (set HSRR0 to 0x200). And for handled
	 * errors (no-fatal), just go back to guest execution with current
	 * HSRR0 instead of exiting guest. This new approach will inject
	 * machine check to guest for fatal error causing guest to crash.
	 *
	 * The old code used to return to host for unhandled errors which
	 * was causing guest to hang with soft lockups inside guest and
	 * makes it difficult to recover guest instance.
	 *
	 * if we receive machine check with MSR(RI=0) then deliver it to
	 * guest as machine check causing guest to crash.
	 */
	ld	r11, VCPU_MSR(r9)
	andi.	r10, r11, MSR_RI	/* check for unrecoverable exception */
	beq	1f			/* Deliver a machine check to guest */
	ld	r10, VCPU_PC(r9)
	cmpdi	r3, 0		/* Did we handle MCE ? */
	bne	2f	/* Continue guest execution. */
	/* If not, deliver a machine check.  SRR0/1 are already set */
1:	li	r10, BOOK3S_INTERRUPT_MACHINE_CHECK
	bl	kvmppc_msr_interrupt
2:	b	fast_interrupt_c_return

/*
 * Check the reason we woke from nap, and take appropriate action.
 * Returns (in r3):
 *	0 if nothing needs to be done
 *	1 if something happened that needs to be handled by the host
 *	-1 if there was a guest wakeup (IPI or msgsnd)
 *
 * Also sets r12 to the interrupt vector for any interrupt that needs
 * to be handled now by the host (0x500 for external interrupt), or zero.
 * Modifies r0, r6, r7, r8.
 */
kvmppc_check_wake_reason:
	mfspr	r6, SPRN_SRR1
BEGIN_FTR_SECTION
	rlwinm	r6, r6, 45-31, 0xf	/* extract wake reason field (P8) */
FTR_SECTION_ELSE
	rlwinm	r6, r6, 45-31, 0xe	/* P7 wake reason field is 3 bits */
ALT_FTR_SECTION_END_IFSET(CPU_FTR_ARCH_207S)
	cmpwi	r6, 8			/* was it an external interrupt? */
	li	r12, BOOK3S_INTERRUPT_EXTERNAL
	beq	kvmppc_read_intr	/* if so, see what it was */
	li	r3, 0
	li	r12, 0
	cmpwi	r6, 6			/* was it the decrementer? */
	beq	0f
BEGIN_FTR_SECTION
	cmpwi	r6, 5			/* privileged doorbell? */
	beq	0f
	cmpwi	r6, 3			/* hypervisor doorbell? */
	beq	3f
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)
	li	r3, 1			/* anything else, return 1 */
0:	blr

	/* hypervisor doorbell */
3:	li	r12, BOOK3S_INTERRUPT_H_DOORBELL

	/*
	 * Clear the doorbell as we will invoke the handler
	 * explicitly in the guest exit path.
	 */
	lis	r6, (PPC_DBELL_SERVER << (63-36))@h
	PPC_MSGCLR(6)
	/* see if it's a host IPI */
	li	r3, 1
	lbz	r0, HSTATE_HOST_IPI(r13)
	cmpwi	r0, 0
	bnelr
	/* if not, return -1 */
	li	r3, -1
	blr

/*
 * Determine what sort of external interrupt is pending (if any).
 * Returns:
 *	0 if no interrupt is pending
 *	1 if an interrupt is pending that needs to be handled by the host
 *	-1 if there was a guest wakeup IPI (which has now been cleared)
 * Modifies r0, r6, r7, r8, returns value in r3.
 */
kvmppc_read_intr:
	/* see if a host IPI is pending */
	li	r3, 1
	lbz	r0, HSTATE_HOST_IPI(r13)
	cmpwi	r0, 0
	bne	1f

	/* Now read the interrupt from the ICP */
	ld	r6, HSTATE_XICS_PHYS(r13)
	li	r7, XICS_XIRR
	cmpdi	r6, 0
	beq-	1f
	lwzcix	r0, r6, r7
	/*
	 * Save XIRR for later. Since we get in in reverse endian on LE
	 * systems, save it byte reversed and fetch it back in host endian.
	 */
	li	r3, HSTATE_SAVED_XIRR
	STWX_BE	r0, r3, r13
#ifdef __LITTLE_ENDIAN__
	lwz	r3, HSTATE_SAVED_XIRR(r13)
#else
	mr	r3, r0
#endif
	rlwinm.	r3, r3, 0, 0xffffff
	sync
	beq	1f			/* if nothing pending in the ICP */

	/* We found something in the ICP...
	 *
	 * If it's not an IPI, stash it in the PACA and return to
	 * the host, we don't (yet) handle directing real external
	 * interrupts directly to the guest
	 */
	cmpwi	r3, XICS_IPI		/* if there is, is it an IPI? */
	bne	42f

	/* It's an IPI, clear the MFRR and EOI it */
	li	r3, 0xff
	li	r8, XICS_MFRR
	stbcix	r3, r6, r8		/* clear the IPI */
	stwcix	r0, r6, r7		/* EOI it */
	sync

	/* We need to re-check host IPI now in case it got set in the
	 * meantime. If it's clear, we bounce the interrupt to the
	 * guest
	 */
	lbz	r0, HSTATE_HOST_IPI(r13)
	cmpwi	r0, 0
	bne-	43f

	/* OK, it's an IPI for us */
	li	r12, 0
	li	r3, -1
1:	blr

42:	/* It's not an IPI and it's for the host. We saved a copy of XIRR in
	 * the PACA earlier, it will be picked up by the host ICP driver
	 */
	li	r3, 1
	b	1b

43:	/* We raced with the host, we need to resend that IPI, bummer */
	li	r0, IPI_PRIORITY
	stbcix	r0, r6, r8		/* set the IPI */
	sync
	li	r3, 1
	b	1b

/*
 * Save away FP, VMX and VSX registers.
 * r3 = vcpu pointer
 * N.B. r30 and r31 are volatile across this function,
 * thus it is not callable from C.
 */
kvmppc_save_fp:
	mflr	r30
	mr	r31,r3
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
	addi	r3,r3,VCPU_FPRS
	bl	store_fp_state
#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	addi	r3,r31,VCPU_VRS
	bl	store_vr_state
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif
	mfspr	r6,SPRN_VRSAVE
	stw	r6,VCPU_VRSAVE(r31)
	mtlr	r30
	blr

/*
 * Load up FP, VMX and VSX registers
 * r4 = vcpu pointer
 * N.B. r30 and r31 are volatile across this function,
 * thus it is not callable from C.
 */
kvmppc_load_fp:
	mflr	r30
	mr	r31,r4
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
	addi	r3,r4,VCPU_FPRS
	bl	load_fp_state
#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	addi	r3,r31,VCPU_VRS
	bl	load_vr_state
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif
	lwz	r7,VCPU_VRSAVE(r31)
	mtspr	SPRN_VRSAVE,r7
	mtlr	r30
	mr	r4,r31
	blr

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
/*
 * Save transactional state and TM-related registers.
 * Called with r9 pointing to the vcpu struct.
 * This can modify all checkpointed registers, but
 * restores r1, r2 and r9 (vcpu pointer) before exit.
 */
kvmppc_save_tm:
	mflr	r0
	std	r0, PPC_LR_STKOFF(r1)

	/* Turn on TM. */
	mfmsr	r8
	li	r0, 1
	rldimi	r8, r0, MSR_TM_LG, 63-MSR_TM_LG
	mtmsrd	r8

	ld	r5, VCPU_MSR(r9)
	rldicl. r5, r5, 64 - MSR_TS_S_LG, 62
	beq	1f	/* TM not active in guest. */

	std	r1, HSTATE_HOST_R1(r13)
	li	r3, TM_CAUSE_KVM_RESCHED

	/* Clear the MSR RI since r1, r13 are all going to be foobar. */
	li	r5, 0
	mtmsrd	r5, 1

	/* All GPRs are volatile at this point. */
	TRECLAIM(R3)

	/* Temporarily store r13 and r9 so we have some regs to play with */
	SET_SCRATCH0(r13)
	GET_PACA(r13)
	std	r9, PACATMSCRATCH(r13)
	ld	r9, HSTATE_KVM_VCPU(r13)

	/* Get a few more GPRs free. */
	std	r29, VCPU_GPRS_TM(29)(r9)
	std	r30, VCPU_GPRS_TM(30)(r9)
	std	r31, VCPU_GPRS_TM(31)(r9)

	/* Save away PPR and DSCR soon so don't run with user values. */
	mfspr	r31, SPRN_PPR
	HMT_MEDIUM
	mfspr	r30, SPRN_DSCR
	ld	r29, HSTATE_DSCR(r13)
	mtspr	SPRN_DSCR, r29

	/* Save all but r9, r13 & r29-r31 */
	reg = 0
	.rept	29
	.if (reg != 9) && (reg != 13)
	std	reg, VCPU_GPRS_TM(reg)(r9)
	.endif
	reg = reg + 1
	.endr
	/* ... now save r13 */
	GET_SCRATCH0(r4)
	std	r4, VCPU_GPRS_TM(13)(r9)
	/* ... and save r9 */
	ld	r4, PACATMSCRATCH(r13)
	std	r4, VCPU_GPRS_TM(9)(r9)

	/* Reload stack pointer and TOC. */
	ld	r1, HSTATE_HOST_R1(r13)
	ld	r2, PACATOC(r13)

	/* Set MSR RI now we have r1 and r13 back. */
	li	r5, MSR_RI
	mtmsrd	r5, 1

	/* Save away checkpinted SPRs. */
	std	r31, VCPU_PPR_TM(r9)
	std	r30, VCPU_DSCR_TM(r9)
	mflr	r5
	mfcr	r6
	mfctr	r7
	mfspr	r8, SPRN_AMR
	mfspr	r10, SPRN_TAR
	std	r5, VCPU_LR_TM(r9)
	stw	r6, VCPU_CR_TM(r9)
	std	r7, VCPU_CTR_TM(r9)
	std	r8, VCPU_AMR_TM(r9)
	std	r10, VCPU_TAR_TM(r9)

	/* Restore r12 as trap number. */
	lwz	r12, VCPU_TRAP(r9)

	/* Save FP/VSX. */
	addi	r3, r9, VCPU_FPRS_TM
	bl	store_fp_state
	addi	r3, r9, VCPU_VRS_TM
	bl	store_vr_state
	mfspr	r6, SPRN_VRSAVE
	stw	r6, VCPU_VRSAVE_TM(r9)
1:
	/*
	 * We need to save these SPRs after the treclaim so that the software
	 * error code is recorded correctly in the TEXASR.  Also the user may
	 * change these outside of a transaction, so they must always be
	 * context switched.
	 */
	mfspr	r5, SPRN_TFHAR
	mfspr	r6, SPRN_TFIAR
	mfspr	r7, SPRN_TEXASR
	std	r5, VCPU_TFHAR(r9)
	std	r6, VCPU_TFIAR(r9)
	std	r7, VCPU_TEXASR(r9)

	ld	r0, PPC_LR_STKOFF(r1)
	mtlr	r0
	blr

/*
 * Restore transactional state and TM-related registers.
 * Called with r4 pointing to the vcpu struct.
 * This potentially modifies all checkpointed registers.
 * It restores r1, r2, r4 from the PACA.
 */
kvmppc_restore_tm:
	mflr	r0
	std	r0, PPC_LR_STKOFF(r1)

	/* Turn on TM/FP/VSX/VMX so we can restore them. */
	mfmsr	r5
	li	r6, MSR_TM >> 32
	sldi	r6, r6, 32
	or	r5, r5, r6
	ori	r5, r5, MSR_FP
	oris	r5, r5, (MSR_VEC | MSR_VSX)@h
	mtmsrd	r5

	/*
	 * The user may change these outside of a transaction, so they must
	 * always be context switched.
	 */
	ld	r5, VCPU_TFHAR(r4)
	ld	r6, VCPU_TFIAR(r4)
	ld	r7, VCPU_TEXASR(r4)
	mtspr	SPRN_TFHAR, r5
	mtspr	SPRN_TFIAR, r6
	mtspr	SPRN_TEXASR, r7

	ld	r5, VCPU_MSR(r4)
	rldicl. r5, r5, 64 - MSR_TS_S_LG, 62
	beqlr		/* TM not active in guest */
	std	r1, HSTATE_HOST_R1(r13)

	/* Make sure the failure summary is set, otherwise we'll program check
	 * when we trechkpt.  It's possible that this might have been not set
	 * on a kvmppc_set_one_reg() call but we shouldn't let this crash the
	 * host.
	 */
	oris	r7, r7, (TEXASR_FS)@h
	mtspr	SPRN_TEXASR, r7

	/*
	 * We need to load up the checkpointed state for the guest.
	 * We need to do this early as it will blow away any GPRs, VSRs and
	 * some SPRs.
	 */

	mr	r31, r4
	addi	r3, r31, VCPU_FPRS_TM
	bl	load_fp_state
	addi	r3, r31, VCPU_VRS_TM
	bl	load_vr_state
	mr	r4, r31
	lwz	r7, VCPU_VRSAVE_TM(r4)
	mtspr	SPRN_VRSAVE, r7

	ld	r5, VCPU_LR_TM(r4)
	lwz	r6, VCPU_CR_TM(r4)
	ld	r7, VCPU_CTR_TM(r4)
	ld	r8, VCPU_AMR_TM(r4)
	ld	r9, VCPU_TAR_TM(r4)
	mtlr	r5
	mtcr	r6
	mtctr	r7
	mtspr	SPRN_AMR, r8
	mtspr	SPRN_TAR, r9

	/*
	 * Load up PPR and DSCR values but don't put them in the actual SPRs
	 * till the last moment to avoid running with userspace PPR and DSCR for
	 * too long.
	 */
	ld	r29, VCPU_DSCR_TM(r4)
	ld	r30, VCPU_PPR_TM(r4)

	std	r2, PACATMSCRATCH(r13) /* Save TOC */

	/* Clear the MSR RI since r1, r13 are all going to be foobar. */
	li	r5, 0
	mtmsrd	r5, 1

	/* Load GPRs r0-r28 */
	reg = 0
	.rept	29
	ld	reg, VCPU_GPRS_TM(reg)(r31)
	reg = reg + 1
	.endr

	mtspr	SPRN_DSCR, r29
	mtspr	SPRN_PPR, r30

	/* Load final GPRs */
	ld	29, VCPU_GPRS_TM(29)(r31)
	ld	30, VCPU_GPRS_TM(30)(r31)
	ld	31, VCPU_GPRS_TM(31)(r31)

	/* TM checkpointed state is now setup.  All GPRs are now volatile. */
	TRECHKPT

	/* Now let's get back the state we need. */
	HMT_MEDIUM
	GET_PACA(r13)
	ld	r29, HSTATE_DSCR(r13)
	mtspr	SPRN_DSCR, r29
	ld	r4, HSTATE_KVM_VCPU(r13)
	ld	r1, HSTATE_HOST_R1(r13)
	ld	r2, PACATMSCRATCH(r13)

	/* Set the MSR RI since we have our registers back. */
	li	r5, MSR_RI
	mtmsrd	r5, 1

	ld	r0, PPC_LR_STKOFF(r1)
	mtlr	r0
	blr
#endif

/*
 * We come here if we get any exception or interrupt while we are
 * executing host real mode code while in guest MMU context.
 * For now just spin, but we should do something better.
 */
kvmppc_bad_host_intr:
	b	.

/*
 * This mimics the MSR transition on IRQ delivery.  The new guest MSR is taken
 * from VCPU_INTR_MSR and is modified based on the required TM state changes.
 *   r11 has the guest MSR value (in/out)
 *   r9 has a vcpu pointer (in)
 *   r0 is used as a scratch register
 */
kvmppc_msr_interrupt:
	rldicl	r0, r11, 64 - MSR_TS_S_LG, 62
	cmpwi	r0, 2 /* Check if we are in transactional state..  */
	ld	r11, VCPU_INTR_MSR(r9)
	bne	1f
	/* ... if transactional, change to suspended */
	li	r0, 1
1:	rldimi	r11, r0, MSR_TS_S_LG, 63 - MSR_TS_T_LG
	blr

/*
 * This works around a hardware bug on POWER8E processors, where
 * writing a 1 to the MMCR0[PMAO] bit doesn't generate a
 * performance monitor interrupt.  Instead, when we need to have
 * an interrupt pending, we have to arrange for a counter to overflow.
 */
kvmppc_fix_pmao:
	li	r3, 0
	mtspr	SPRN_MMCR2, r3
	lis	r3, (MMCR0_PMXE | MMCR0_FCECE)@h
	ori	r3, r3, MMCR0_PMCjCE | MMCR0_C56RUN
	mtspr	SPRN_MMCR0, r3
	lis	r3, 0x7fff
	ori	r3, r3, 0xffff
	mtspr	SPRN_PMC6, r3
	isync
	blr

#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
/*
 * Start timing an activity
 * r3 = pointer to time accumulation struct, r4 = vcpu
 */
kvmhv_start_timing:
	ld	r5, HSTATE_KVM_VCORE(r13)
	lbz	r6, VCORE_IN_GUEST(r5)
	cmpwi	r6, 0
	beq	5f				/* if in guest, need to */
	ld	r6, VCORE_TB_OFFSET(r5)		/* subtract timebase offset */
5:	mftb	r5
	subf	r5, r6, r5
	std	r3, VCPU_CUR_ACTIVITY(r4)
	std	r5, VCPU_ACTIVITY_START(r4)
	blr

/*
 * Accumulate time to one activity and start another.
 * r3 = pointer to new time accumulation struct, r4 = vcpu
 */
kvmhv_accumulate_time:
	ld	r5, HSTATE_KVM_VCORE(r13)
	lbz	r8, VCORE_IN_GUEST(r5)
	cmpwi	r8, 0
	beq	4f				/* if in guest, need to */
	ld	r8, VCORE_TB_OFFSET(r5)		/* subtract timebase offset */
4:	ld	r5, VCPU_CUR_ACTIVITY(r4)
	ld	r6, VCPU_ACTIVITY_START(r4)
	std	r3, VCPU_CUR_ACTIVITY(r4)
	mftb	r7
	subf	r7, r8, r7
	std	r7, VCPU_ACTIVITY_START(r4)
	cmpdi	r5, 0
	beqlr
	subf	r3, r6, r7
	ld	r8, TAS_SEQCOUNT(r5)
	cmpdi	r8, 0
	addi	r8, r8, 1
	std	r8, TAS_SEQCOUNT(r5)
	lwsync
	ld	r7, TAS_TOTAL(r5)
	add	r7, r7, r3
	std	r7, TAS_TOTAL(r5)
	ld	r6, TAS_MIN(r5)
	ld	r7, TAS_MAX(r5)
	beq	3f
	cmpd	r3, r6
	bge	1f
3:	std	r3, TAS_MIN(r5)
1:	cmpd	r3, r7
	ble	2f
	std	r3, TAS_MAX(r5)
2:	lwsync
	addi	r8, r8, 1
	std	r8, TAS_SEQCOUNT(r5)
	blr
#endif
