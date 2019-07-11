/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright SUSE Linux Products GmbH 2010
 * Copyright 2010-2011 Freescale Semiconductor, Inc.
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#include <asm/ppc_asm.h>
#include <asm/kvm_asm.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <asm/asm-offsets.h>
#include <asm/asm-compat.h>

#define KVM_MAGIC_PAGE		(-4096)

#ifdef CONFIG_64BIT
#define LL64(reg, offs, reg2)	ld	reg, (offs)(reg2)
#define STL64(reg, offs, reg2)	std	reg, (offs)(reg2)
#else
#define LL64(reg, offs, reg2)	lwz	reg, (offs + 4)(reg2)
#define STL64(reg, offs, reg2)	stw	reg, (offs + 4)(reg2)
#endif

#define SCRATCH_SAVE							\
	/* Enable critical section. We are critical if			\
	   shared->critical == r1 */					\
	STL64(r1, KVM_MAGIC_PAGE + KVM_MAGIC_CRITICAL, 0);		\
									\
	/* Save state */						\
	PPC_STL	r31, (KVM_MAGIC_PAGE + KVM_MAGIC_SCRATCH1)(0);		\
	PPC_STL	r30, (KVM_MAGIC_PAGE + KVM_MAGIC_SCRATCH2)(0);		\
	mfcr	r31;							\
	stw	r31, (KVM_MAGIC_PAGE + KVM_MAGIC_SCRATCH3)(0);

#define SCRATCH_RESTORE							\
	/* Restore state */						\
	PPC_LL	r31, (KVM_MAGIC_PAGE + KVM_MAGIC_SCRATCH1)(0);		\
	lwz	r30, (KVM_MAGIC_PAGE + KVM_MAGIC_SCRATCH3)(0);		\
	mtcr	r30;							\
	PPC_LL	r30, (KVM_MAGIC_PAGE + KVM_MAGIC_SCRATCH2)(0);		\
									\
	/* Disable critical section. We are critical if			\
	   shared->critical == r1 and r2 is always != r1 */		\
	STL64(r2, KVM_MAGIC_PAGE + KVM_MAGIC_CRITICAL, 0);

.global kvm_template_start
kvm_template_start:

.global kvm_emulate_mtmsrd
kvm_emulate_mtmsrd:

	SCRATCH_SAVE

	/* Put MSR & ~(MSR_EE|MSR_RI) in r31 */
	LL64(r31, KVM_MAGIC_PAGE + KVM_MAGIC_MSR, 0)
	lis	r30, (~(MSR_EE | MSR_RI))@h
	ori	r30, r30, (~(MSR_EE | MSR_RI))@l
	and	r31, r31, r30

	/* OR the register's (MSR_EE|MSR_RI) on MSR */
kvm_emulate_mtmsrd_reg:
	ori	r30, r0, 0
	andi.	r30, r30, (MSR_EE|MSR_RI)
	or	r31, r31, r30

	/* Put MSR back into magic page */
	STL64(r31, KVM_MAGIC_PAGE + KVM_MAGIC_MSR, 0)

	/* Check if we have to fetch an interrupt */
	lwz	r31, (KVM_MAGIC_PAGE + KVM_MAGIC_INT)(0)
	cmpwi	r31, 0
	beq+	no_check

	/* Check if we may trigger an interrupt */
	andi.	r30, r30, MSR_EE
	beq	no_check

	SCRATCH_RESTORE

	/* Nag hypervisor */
kvm_emulate_mtmsrd_orig_ins:
	tlbsync

	b	kvm_emulate_mtmsrd_branch

no_check:

	SCRATCH_RESTORE

	/* Go back to caller */
kvm_emulate_mtmsrd_branch:
	b	.
kvm_emulate_mtmsrd_end:

.global kvm_emulate_mtmsrd_branch_offs
kvm_emulate_mtmsrd_branch_offs:
	.long (kvm_emulate_mtmsrd_branch - kvm_emulate_mtmsrd) / 4

.global kvm_emulate_mtmsrd_reg_offs
kvm_emulate_mtmsrd_reg_offs:
	.long (kvm_emulate_mtmsrd_reg - kvm_emulate_mtmsrd) / 4

.global kvm_emulate_mtmsrd_orig_ins_offs
kvm_emulate_mtmsrd_orig_ins_offs:
	.long (kvm_emulate_mtmsrd_orig_ins - kvm_emulate_mtmsrd) / 4

.global kvm_emulate_mtmsrd_len
kvm_emulate_mtmsrd_len:
	.long (kvm_emulate_mtmsrd_end - kvm_emulate_mtmsrd) / 4


#define MSR_SAFE_BITS (MSR_EE | MSR_RI)
#define MSR_CRITICAL_BITS ~MSR_SAFE_BITS

.global kvm_emulate_mtmsr
kvm_emulate_mtmsr:

	SCRATCH_SAVE

	/* Fetch old MSR in r31 */
	LL64(r31, KVM_MAGIC_PAGE + KVM_MAGIC_MSR, 0)

	/* Find the changed bits between old and new MSR */
kvm_emulate_mtmsr_reg1:
	ori	r30, r0, 0
	xor	r31, r30, r31

	/* Check if we need to really do mtmsr */
	LOAD_REG_IMMEDIATE(r30, MSR_CRITICAL_BITS)
	and.	r31, r31, r30

	/* No critical bits changed? Maybe we can stay in the guest. */
	beq	maybe_stay_in_guest

do_mtmsr:

	SCRATCH_RESTORE

	/* Just fire off the mtmsr if it's critical */
kvm_emulate_mtmsr_orig_ins:
	mtmsr	r0

	b	kvm_emulate_mtmsr_branch

maybe_stay_in_guest:

	/* Get the target register in r30 */
kvm_emulate_mtmsr_reg2:
	ori	r30, r0, 0

	/* Put MSR into magic page because we don't call mtmsr */
	STL64(r30, KVM_MAGIC_PAGE + KVM_MAGIC_MSR, 0)

	/* Check if we have to fetch an interrupt */
	lwz	r31, (KVM_MAGIC_PAGE + KVM_MAGIC_INT)(0)
	cmpwi	r31, 0
	beq+	no_mtmsr

	/* Check if we may trigger an interrupt */
	andi.	r31, r30, MSR_EE
	bne	do_mtmsr

no_mtmsr:

	SCRATCH_RESTORE

	/* Go back to caller */
kvm_emulate_mtmsr_branch:
	b	.
kvm_emulate_mtmsr_end:

.global kvm_emulate_mtmsr_branch_offs
kvm_emulate_mtmsr_branch_offs:
	.long (kvm_emulate_mtmsr_branch - kvm_emulate_mtmsr) / 4

.global kvm_emulate_mtmsr_reg1_offs
kvm_emulate_mtmsr_reg1_offs:
	.long (kvm_emulate_mtmsr_reg1 - kvm_emulate_mtmsr) / 4

.global kvm_emulate_mtmsr_reg2_offs
kvm_emulate_mtmsr_reg2_offs:
	.long (kvm_emulate_mtmsr_reg2 - kvm_emulate_mtmsr) / 4

.global kvm_emulate_mtmsr_orig_ins_offs
kvm_emulate_mtmsr_orig_ins_offs:
	.long (kvm_emulate_mtmsr_orig_ins - kvm_emulate_mtmsr) / 4

.global kvm_emulate_mtmsr_len
kvm_emulate_mtmsr_len:
	.long (kvm_emulate_mtmsr_end - kvm_emulate_mtmsr) / 4

/* also used for wrteei 1 */
.global kvm_emulate_wrtee
kvm_emulate_wrtee:

	SCRATCH_SAVE

	/* Fetch old MSR in r31 */
	LL64(r31, KVM_MAGIC_PAGE + KVM_MAGIC_MSR, 0)

	/* Insert new MSR[EE] */
kvm_emulate_wrtee_reg:
	ori	r30, r0, 0
	rlwimi	r31, r30, 0, MSR_EE

	/*
	 * If MSR[EE] is now set, check for a pending interrupt.
	 * We could skip this if MSR[EE] was already on, but that
	 * should be rare, so don't bother.
	 */
	andi.	r30, r30, MSR_EE

	/* Put MSR into magic page because we don't call wrtee */
	STL64(r31, KVM_MAGIC_PAGE + KVM_MAGIC_MSR, 0)

	beq	no_wrtee

	/* Check if we have to fetch an interrupt */
	lwz	r30, (KVM_MAGIC_PAGE + KVM_MAGIC_INT)(0)
	cmpwi	r30, 0
	bne	do_wrtee

no_wrtee:
	SCRATCH_RESTORE

	/* Go back to caller */
kvm_emulate_wrtee_branch:
	b	.

do_wrtee:
	SCRATCH_RESTORE

	/* Just fire off the wrtee if it's critical */
kvm_emulate_wrtee_orig_ins:
	wrtee	r0

	b	kvm_emulate_wrtee_branch

kvm_emulate_wrtee_end:

.global kvm_emulate_wrtee_branch_offs
kvm_emulate_wrtee_branch_offs:
	.long (kvm_emulate_wrtee_branch - kvm_emulate_wrtee) / 4

.global kvm_emulate_wrtee_reg_offs
kvm_emulate_wrtee_reg_offs:
	.long (kvm_emulate_wrtee_reg - kvm_emulate_wrtee) / 4

.global kvm_emulate_wrtee_orig_ins_offs
kvm_emulate_wrtee_orig_ins_offs:
	.long (kvm_emulate_wrtee_orig_ins - kvm_emulate_wrtee) / 4

.global kvm_emulate_wrtee_len
kvm_emulate_wrtee_len:
	.long (kvm_emulate_wrtee_end - kvm_emulate_wrtee) / 4

.global kvm_emulate_wrteei_0
kvm_emulate_wrteei_0:
	SCRATCH_SAVE

	/* Fetch old MSR in r31 */
	LL64(r31, KVM_MAGIC_PAGE + KVM_MAGIC_MSR, 0)

	/* Remove MSR_EE from old MSR */
	rlwinm	r31, r31, 0, ~MSR_EE

	/* Write new MSR value back */
	STL64(r31, KVM_MAGIC_PAGE + KVM_MAGIC_MSR, 0)

	SCRATCH_RESTORE

	/* Go back to caller */
kvm_emulate_wrteei_0_branch:
	b	.
kvm_emulate_wrteei_0_end:

.global kvm_emulate_wrteei_0_branch_offs
kvm_emulate_wrteei_0_branch_offs:
	.long (kvm_emulate_wrteei_0_branch - kvm_emulate_wrteei_0) / 4

.global kvm_emulate_wrteei_0_len
kvm_emulate_wrteei_0_len:
	.long (kvm_emulate_wrteei_0_end - kvm_emulate_wrteei_0) / 4

.global kvm_emulate_mtsrin
kvm_emulate_mtsrin:

	SCRATCH_SAVE

	LL64(r31, KVM_MAGIC_PAGE + KVM_MAGIC_MSR, 0)
	andi.	r31, r31, MSR_DR | MSR_IR
	beq	kvm_emulate_mtsrin_reg1

	SCRATCH_RESTORE

kvm_emulate_mtsrin_orig_ins:
	nop
	b	kvm_emulate_mtsrin_branch

kvm_emulate_mtsrin_reg1:
	/* rX >> 26 */
	rlwinm  r30,r0,6,26,29

kvm_emulate_mtsrin_reg2:
	stw	r0, (KVM_MAGIC_PAGE + KVM_MAGIC_SR)(r30)

	SCRATCH_RESTORE

	/* Go back to caller */
kvm_emulate_mtsrin_branch:
	b	.
kvm_emulate_mtsrin_end:

.global kvm_emulate_mtsrin_branch_offs
kvm_emulate_mtsrin_branch_offs:
	.long (kvm_emulate_mtsrin_branch - kvm_emulate_mtsrin) / 4

.global kvm_emulate_mtsrin_reg1_offs
kvm_emulate_mtsrin_reg1_offs:
	.long (kvm_emulate_mtsrin_reg1 - kvm_emulate_mtsrin) / 4

.global kvm_emulate_mtsrin_reg2_offs
kvm_emulate_mtsrin_reg2_offs:
	.long (kvm_emulate_mtsrin_reg2 - kvm_emulate_mtsrin) / 4

.global kvm_emulate_mtsrin_orig_ins_offs
kvm_emulate_mtsrin_orig_ins_offs:
	.long (kvm_emulate_mtsrin_orig_ins - kvm_emulate_mtsrin) / 4

.global kvm_emulate_mtsrin_len
kvm_emulate_mtsrin_len:
	.long (kvm_emulate_mtsrin_end - kvm_emulate_mtsrin) / 4

.global kvm_template_end
kvm_template_end:
