/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright SUSE Linux Products GmbH 2010
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

/* Real mode helpers */

#include <asm/asm-compat.h>
#include <asm/feature-fixups.h>

#if defined(CONFIG_PPC_BOOK3S_64)

#define GET_SHADOW_VCPU(reg)    \
	mr	reg, r13

#elif defined(CONFIG_PPC_BOOK3S_32)

#define GET_SHADOW_VCPU(reg)    			\
	tophys(reg, r2);       			\
	lwz     reg, (THREAD + THREAD_KVM_SVCPU)(reg);	\
	tophys(reg, reg)

#endif

/* Disable for nested KVM */
#define USE_QUICK_LAST_INST


/* Get helper functions for subarch specific functionality */

#if defined(CONFIG_PPC_BOOK3S_64)
#include "book3s_64_slb.S"
#elif defined(CONFIG_PPC_BOOK3S_32)
#include "book3s_32_sr.S"
#endif

/******************************************************************************
 *                                                                            *
 *                               Entry code                                   *
 *                                                                            *
 *****************************************************************************/

.global kvmppc_handler_trampoline_enter
kvmppc_handler_trampoline_enter:

	/* Required state:
	 *
	 * MSR = ~IR|DR
	 * R1 = host R1
	 * R2 = host R2
	 * R4 = guest shadow MSR
	 * R5 = normal host MSR
	 * R6 = current host MSR (EE, IR, DR off)
	 * LR = highmem guest exit code
	 * all other volatile GPRS = free
	 * SVCPU[CR] = guest CR
	 * SVCPU[XER] = guest XER
	 * SVCPU[CTR] = guest CTR
	 * SVCPU[LR] = guest LR
	 */

	/* r3 = shadow vcpu */
	GET_SHADOW_VCPU(r3)

	/* Save guest exit handler address and MSR */
	mflr	r0
	PPC_STL	r0, HSTATE_VMHANDLER(r3)
	PPC_STL	r5, HSTATE_HOST_MSR(r3)

	/* Save R1/R2 in the PACA (64-bit) or shadow_vcpu (32-bit) */
	PPC_STL	r1, HSTATE_HOST_R1(r3)
	PPC_STL	r2, HSTATE_HOST_R2(r3)

	/* Activate guest mode, so faults get handled by KVM */
	li	r11, KVM_GUEST_MODE_GUEST
	stb	r11, HSTATE_IN_GUEST(r3)

	/* Switch to guest segment. This is subarch specific. */
	LOAD_GUEST_SEGMENTS

#ifdef CONFIG_PPC_BOOK3S_64
BEGIN_FTR_SECTION
	/* Save host FSCR */
	mfspr	r8, SPRN_FSCR
	std	r8, HSTATE_HOST_FSCR(r13)
	/* Set FSCR during guest execution */
	ld	r9, SVCPU_SHADOW_FSCR(r13)
	mtspr	SPRN_FSCR, r9
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)

	/* Some guests may need to have dcbz set to 32 byte length.
	 *
	 * Usually we ensure that by patching the guest's instructions
	 * to trap on dcbz and emulate it in the hypervisor.
	 *
	 * If we can, we should tell the CPU to use 32 byte dcbz though,
	 * because that's a lot faster.
	 */
	lbz	r0, HSTATE_RESTORE_HID5(r3)
	cmpwi	r0, 0
	beq	no_dcbz32_on

	mfspr   r0,SPRN_HID5
	ori     r0, r0, 0x80		/* XXX HID5_dcbz32 = 0x80 */
	mtspr   SPRN_HID5,r0
no_dcbz32_on:

#endif /* CONFIG_PPC_BOOK3S_64 */

	/* Enter guest */

	PPC_LL	r8, SVCPU_CTR(r3)
	PPC_LL	r9, SVCPU_LR(r3)
	lwz	r10, SVCPU_CR(r3)
	PPC_LL	r11, SVCPU_XER(r3)

	mtctr	r8
	mtlr	r9
	mtcr	r10
	mtxer	r11

	/* Move SRR0 and SRR1 into the respective regs */
	PPC_LL  r9, SVCPU_PC(r3)
	/* First clear RI in our current MSR value */
	li	r0, MSR_RI
	andc	r6, r6, r0

	PPC_LL	r0, SVCPU_R0(r3)
	PPC_LL	r1, SVCPU_R1(r3)
	PPC_LL	r2, SVCPU_R2(r3)
	PPC_LL	r5, SVCPU_R5(r3)
	PPC_LL	r7, SVCPU_R7(r3)
	PPC_LL	r8, SVCPU_R8(r3)
	PPC_LL	r10, SVCPU_R10(r3)
	PPC_LL	r11, SVCPU_R11(r3)
	PPC_LL	r12, SVCPU_R12(r3)
	PPC_LL	r13, SVCPU_R13(r3)

	MTMSR_EERI(r6)
	mtsrr0	r9
	mtsrr1	r4

	PPC_LL	r4, SVCPU_R4(r3)
	PPC_LL	r6, SVCPU_R6(r3)
	PPC_LL	r9, SVCPU_R9(r3)
	PPC_LL	r3, (SVCPU_R3)(r3)

	RFI_TO_GUEST
kvmppc_handler_trampoline_enter_end:



/******************************************************************************
 *                                                                            *
 *                               Exit code                                    *
 *                                                                            *
 *****************************************************************************/

.global kvmppc_interrupt_pr
kvmppc_interrupt_pr:
	/* 64-bit entry. Register usage at this point:
	 *
	 * SPRG_SCRATCH0   = guest R13
	 * R9              = HSTATE_IN_GUEST
	 * R12             = (guest CR << 32) | exit handler id
	 * R13             = PACA
	 * HSTATE.SCRATCH0 = guest R12
	 * HSTATE.SCRATCH2 = guest R9
	 */
#ifdef CONFIG_PPC64
	/* Match 32-bit entry */
	ld	r9,HSTATE_SCRATCH2(r13)
	rotldi	r12, r12, 32		  /* Flip R12 halves for stw */
	stw	r12, HSTATE_SCRATCH1(r13) /* CR is now in the low half */
	srdi	r12, r12, 32		  /* shift trap into low half */
#endif

.global kvmppc_handler_trampoline_exit
kvmppc_handler_trampoline_exit:
	/* Register usage at this point:
	 *
	 * SPRG_SCRATCH0   = guest R13
	 * R12             = exit handler id
	 * R13             = shadow vcpu (32-bit) or PACA (64-bit)
	 * HSTATE.SCRATCH0 = guest R12
	 * HSTATE.SCRATCH1 = guest CR
	 */

	/* Save registers */

	PPC_STL	r0, SVCPU_R0(r13)
	PPC_STL	r1, SVCPU_R1(r13)
	PPC_STL	r2, SVCPU_R2(r13)
	PPC_STL	r3, SVCPU_R3(r13)
	PPC_STL	r4, SVCPU_R4(r13)
	PPC_STL	r5, SVCPU_R5(r13)
	PPC_STL	r6, SVCPU_R6(r13)
	PPC_STL	r7, SVCPU_R7(r13)
	PPC_STL	r8, SVCPU_R8(r13)
	PPC_STL	r9, SVCPU_R9(r13)
	PPC_STL	r10, SVCPU_R10(r13)
	PPC_STL	r11, SVCPU_R11(r13)

	/* Restore R1/R2 so we can handle faults */
	PPC_LL	r1, HSTATE_HOST_R1(r13)
	PPC_LL	r2, HSTATE_HOST_R2(r13)

	/* Save guest PC and MSR */
#ifdef CONFIG_PPC64
BEGIN_FTR_SECTION
	andi.	r0, r12, 0x2
	cmpwi	cr1, r0, 0
	beq	1f
	mfspr	r3,SPRN_HSRR0
	mfspr	r4,SPRN_HSRR1
	andi.	r12,r12,0x3ffd
	b	2f
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE)
#endif
1:	mfsrr0	r3
	mfsrr1	r4
2:
	PPC_STL	r3, SVCPU_PC(r13)
	PPC_STL	r4, SVCPU_SHADOW_SRR1(r13)

	/* Get scratch'ed off registers */
	GET_SCRATCH0(r9)
	PPC_LL	r8, HSTATE_SCRATCH0(r13)
	lwz	r7, HSTATE_SCRATCH1(r13)

	PPC_STL	r9, SVCPU_R13(r13)
	PPC_STL	r8, SVCPU_R12(r13)
	stw	r7, SVCPU_CR(r13)

	/* Save more register state  */

	mfxer	r5
	mfdar	r6
	mfdsisr	r7
	mfctr	r8
	mflr	r9

	PPC_STL	r5, SVCPU_XER(r13)
	PPC_STL	r6, SVCPU_FAULT_DAR(r13)
	stw	r7, SVCPU_FAULT_DSISR(r13)
	PPC_STL	r8, SVCPU_CTR(r13)
	PPC_STL	r9, SVCPU_LR(r13)

	/*
	 * In order for us to easily get the last instruction,
	 * we got the #vmexit at, we exploit the fact that the
	 * virtual layout is still the same here, so we can just
	 * ld from the guest's PC address
	 */

	/* We only load the last instruction when it's safe */
	cmpwi	r12, BOOK3S_INTERRUPT_DATA_STORAGE
	beq	ld_last_inst
	cmpwi	r12, BOOK3S_INTERRUPT_PROGRAM
	beq	ld_last_inst
	cmpwi	r12, BOOK3S_INTERRUPT_SYSCALL
	beq	ld_last_prev_inst
	cmpwi	r12, BOOK3S_INTERRUPT_ALIGNMENT
	beq-	ld_last_inst
#ifdef CONFIG_PPC64
BEGIN_FTR_SECTION
	cmpwi	r12, BOOK3S_INTERRUPT_H_EMUL_ASSIST
	beq-	ld_last_inst
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE)
BEGIN_FTR_SECTION
	cmpwi	r12, BOOK3S_INTERRUPT_FAC_UNAVAIL
	beq-	ld_last_inst
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)
#endif

	b	no_ld_last_inst

ld_last_prev_inst:
	addi	r3, r3, -4

ld_last_inst:
	/* Save off the guest instruction we're at */

	/* In case lwz faults */
	li	r0, KVM_INST_FETCH_FAILED

#ifdef USE_QUICK_LAST_INST

	/* Set guest mode to 'jump over instruction' so if lwz faults
	 * we'll just continue at the next IP. */
	li	r9, KVM_GUEST_MODE_SKIP
	stb	r9, HSTATE_IN_GUEST(r13)

	/*    1) enable paging for data */
	mfmsr	r9
	ori	r11, r9, MSR_DR			/* Enable paging for data */
	mtmsr	r11
	sync
	/*    2) fetch the instruction */
	lwz	r0, 0(r3)
	/*    3) disable paging again */
	mtmsr	r9
	sync

#endif
	stw	r0, SVCPU_LAST_INST(r13)

no_ld_last_inst:

	/* Unset guest mode */
	li	r9, KVM_GUEST_MODE_NONE
	stb	r9, HSTATE_IN_GUEST(r13)

	/* Switch back to host MMU */
	LOAD_HOST_SEGMENTS

#ifdef CONFIG_PPC_BOOK3S_64

	lbz	r5, HSTATE_RESTORE_HID5(r13)
	cmpwi	r5, 0
	beq	no_dcbz32_off

	li	r4, 0
	mfspr   r5,SPRN_HID5
	rldimi  r5,r4,6,56
	mtspr   SPRN_HID5,r5

no_dcbz32_off:

BEGIN_FTR_SECTION
	/* Save guest FSCR on a FAC_UNAVAIL interrupt */
	cmpwi	r12, BOOK3S_INTERRUPT_FAC_UNAVAIL
	bne+	no_fscr_save
	mfspr	r7, SPRN_FSCR
	std	r7, SVCPU_SHADOW_FSCR(r13)
no_fscr_save:
	/* Restore host FSCR */
	ld	r8, HSTATE_HOST_FSCR(r13)
	mtspr	SPRN_FSCR, r8
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_207S)

#endif /* CONFIG_PPC_BOOK3S_64 */

	/*
	 * For some interrupts, we need to call the real Linux
	 * handler, so it can do work for us. This has to happen
	 * as if the interrupt arrived from the kernel though,
	 * so let's fake it here where most state is restored.
	 *
	 * Having set up SRR0/1 with the address where we want
	 * to continue with relocation on (potentially in module
	 * space), we either just go straight there with rfi[d],
	 * or we jump to an interrupt handler if there is an
	 * interrupt to be handled first.  In the latter case,
	 * the rfi[d] at the end of the interrupt handler will
	 * get us back to where we want to continue.
	 */

	/* Register usage at this point:
	 *
	 * R1       = host R1
	 * R2       = host R2
	 * R10      = raw exit handler id
	 * R12      = exit handler id
	 * R13      = shadow vcpu (32-bit) or PACA (64-bit)
	 * SVCPU.*  = guest *
	 *
	 */

	PPC_LL	r6, HSTATE_HOST_MSR(r13)
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	/*
	 * We don't want to change MSR[TS] bits via rfi here.
	 * The actual TM handling logic will be in host with
	 * recovered DR/IR bits after HSTATE_VMHANDLER.
	 * And MSR_TM can be enabled in HOST_MSR so rfid may
	 * not suppress this change and can lead to exception.
	 * Manually set MSR to prevent TS state change here.
	 */
	mfmsr   r7
	rldicl  r7, r7, 64 - MSR_TS_S_LG, 62
	rldimi  r6, r7, MSR_TS_S_LG, 63 - MSR_TS_T_LG
#endif
	PPC_LL	r8, HSTATE_VMHANDLER(r13)

#ifdef CONFIG_PPC64
BEGIN_FTR_SECTION
	beq	cr1, 1f
	mtspr	SPRN_HSRR1, r6
	mtspr	SPRN_HSRR0, r8
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE)
#endif
1:	/* Restore host msr -> SRR1 */
	mtsrr1	r6
	/* Load highmem handler address */
	mtsrr0	r8

	/* RFI into the highmem handler, or jump to interrupt handler */
	cmpwi	r12, BOOK3S_INTERRUPT_EXTERNAL
	beqa	BOOK3S_INTERRUPT_EXTERNAL
	cmpwi	r12, BOOK3S_INTERRUPT_DECREMENTER
	beqa	BOOK3S_INTERRUPT_DECREMENTER
	cmpwi	r12, BOOK3S_INTERRUPT_PERFMON
	beqa	BOOK3S_INTERRUPT_PERFMON
	cmpwi	r12, BOOK3S_INTERRUPT_DOORBELL
	beqa	BOOK3S_INTERRUPT_DOORBELL

	RFI_TO_KERNEL
kvmppc_handler_trampoline_exit_end:
