/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Derived from book3s_hv_rmhandlers.S, which is:
 *
 * Copyright 2011 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 */

#include <asm/reg.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/export.h>
#include <asm/tm.h>
#include <asm/cputable.h>

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
#define VCPU_GPRS_TM(reg) (((reg) * ULONG_SIZE) + VCPU_GPR_TM)

/*
 * Save transactional state and TM-related registers.
 * Called with:
 * - r3 pointing to the vcpu struct
 * - r4 containing the MSR with current TS bits:
 * 	(For HV KVM, it is VCPU_MSR ; For PR KVM, it is host MSR).
 * - r5 containing a flag indicating that non-volatile registers
 *	must be preserved.
 * If r5 == 0, this can modify all checkpointed registers, but
 * restores r1, r2 before exit.  If r5 != 0, this restores the
 * MSR TM/FP/VEC/VSX bits to their state on entry.
 */
_GLOBAL(__kvmppc_save_tm)
	mflr	r0
	std	r0, PPC_LR_STKOFF(r1)
	stdu    r1, -SWITCH_FRAME_SIZE(r1)

	mr	r9, r3
	cmpdi	cr7, r5, 0

	/* Turn on TM. */
	mfmsr	r8
	mr	r10, r8
	li	r0, 1
	rldimi	r8, r0, MSR_TM_LG, 63-MSR_TM_LG
	ori     r8, r8, MSR_FP
	oris    r8, r8, (MSR_VEC | MSR_VSX)@h
	mtmsrd	r8

	rldicl. r4, r4, 64 - MSR_TS_S_LG, 62
	beq	1f	/* TM not active in guest. */

	std	r1, HSTATE_SCRATCH2(r13)
	std	r3, HSTATE_SCRATCH1(r13)

	/* Save CR on the stack - even if r5 == 0 we need to get cr7 back. */
	mfcr	r6
	SAVE_GPR(6, r1)

	/* Save DSCR so we can restore it to avoid running with user value */
	mfspr	r7, SPRN_DSCR
	SAVE_GPR(7, r1)

	/*
	 * We are going to do treclaim., which will modify all checkpointed
	 * registers.  Save the non-volatile registers on the stack if
	 * preservation of non-volatile state has been requested.
	 */
	beq	cr7, 3f
	SAVE_NVGPRS(r1)

	/* MSR[TS] will be 0 (non-transactional) once we do treclaim. */
	li	r0, 0
	rldimi	r10, r0, MSR_TS_S_LG, 63 - MSR_TS_T_LG
	SAVE_GPR(10, r1)	/* final MSR value */
3:
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
BEGIN_FTR_SECTION
	/* Emulation of the treclaim instruction needs TEXASR before treclaim */
	mfspr	r6, SPRN_TEXASR
	std	r6, VCPU_ORIG_TEXASR(r3)
END_FTR_SECTION_IFSET(CPU_FTR_P9_TM_HV_ASSIST)
#endif

	/* Clear the MSR RI since r1, r13 are all going to be foobar. */
	li	r5, 0
	mtmsrd	r5, 1

	li	r3, TM_CAUSE_KVM_RESCHED

	/* All GPRs are volatile at this point. */
	TRECLAIM(R3)

	/* Temporarily store r13 and r9 so we have some regs to play with */
	SET_SCRATCH0(r13)
	GET_PACA(r13)
	std	r9, PACATMSCRATCH(r13)
	ld	r9, HSTATE_SCRATCH1(r13)

	/* Save away PPR soon so we don't run with user value. */
	std	r0, VCPU_GPRS_TM(0)(r9)
	mfspr	r0, SPRN_PPR
	HMT_MEDIUM

	/* Reload stack pointer. */
	std	r1, VCPU_GPRS_TM(1)(r9)
	ld	r1, HSTATE_SCRATCH2(r13)

	/* Set MSR RI now we have r1 and r13 back. */
	std	r2, VCPU_GPRS_TM(2)(r9)
	li	r2, MSR_RI
	mtmsrd	r2, 1

	/* Reload TOC pointer. */
	ld	r2, PACATOC(r13)

	/* Save all but r0-r2, r9 & r13 */
	reg = 3
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

	/* Restore host DSCR and CR values, after saving guest values */
	mfcr	r6
	mfspr	r7, SPRN_DSCR
	stw	r6, VCPU_CR_TM(r9)
	std	r7, VCPU_DSCR_TM(r9)
	REST_GPR(6, r1)
	REST_GPR(7, r1)
	mtcr	r6
	mtspr	SPRN_DSCR, r7

	/* Save away checkpointed SPRs. */
	std	r0, VCPU_PPR_TM(r9)
	mflr	r5
	mfctr	r7
	mfspr	r8, SPRN_AMR
	mfspr	r10, SPRN_TAR
	mfxer	r11
	std	r5, VCPU_LR_TM(r9)
	std	r7, VCPU_CTR_TM(r9)
	std	r8, VCPU_AMR_TM(r9)
	std	r10, VCPU_TAR_TM(r9)
	std	r11, VCPU_XER_TM(r9)

	/* Save FP/VSX. */
	addi	r3, r9, VCPU_FPRS_TM
	bl	store_fp_state
	addi	r3, r9, VCPU_VRS_TM
	bl	store_vr_state
	mfspr	r6, SPRN_VRSAVE
	stw	r6, VCPU_VRSAVE_TM(r9)

	/* Restore non-volatile registers if requested to */
	beq	cr7, 1f
	REST_NVGPRS(r1)
	REST_GPR(10, r1)
1:
	/*
	 * We need to save these SPRs after the treclaim so that the software
	 * error code is recorded correctly in the TEXASR.  Also the user may
	 * change these outside of a transaction, so they must always be
	 * context switched.
	 */
	mfspr	r7, SPRN_TEXASR
	std	r7, VCPU_TEXASR(r9)
	mfspr	r5, SPRN_TFHAR
	mfspr	r6, SPRN_TFIAR
	std	r5, VCPU_TFHAR(r9)
	std	r6, VCPU_TFIAR(r9)

	/* Restore MSR state if requested */
	beq	cr7, 2f
	mtmsrd	r10, 0
2:
	addi	r1, r1, SWITCH_FRAME_SIZE
	ld	r0, PPC_LR_STKOFF(r1)
	mtlr	r0
	blr

/*
 * _kvmppc_save_tm_pr() is a wrapper around __kvmppc_save_tm(), so that it can
 * be invoked from C function by PR KVM only.
 */
_GLOBAL(_kvmppc_save_tm_pr)
	mflr	r0
	std	r0, PPC_LR_STKOFF(r1)
	stdu    r1, -PPC_MIN_STKFRM(r1)

	mfspr   r8, SPRN_TAR
	std	r8, PPC_MIN_STKFRM-8(r1)

	li	r5, 1		/* preserve non-volatile registers */
	bl	__kvmppc_save_tm

	ld	r8, PPC_MIN_STKFRM-8(r1)
	mtspr   SPRN_TAR, r8

	addi    r1, r1, PPC_MIN_STKFRM
	ld	r0, PPC_LR_STKOFF(r1)
	mtlr	r0
	blr

EXPORT_SYMBOL_GPL(_kvmppc_save_tm_pr);

/*
 * Restore transactional state and TM-related registers.
 * Called with:
 *  - r3 pointing to the vcpu struct.
 *  - r4 is the guest MSR with desired TS bits:
 * 	For HV KVM, it is VCPU_MSR
 * 	For PR KVM, it is provided by caller
 * - r5 containing a flag indicating that non-volatile registers
 *	must be preserved.
 * If r5 == 0, this potentially modifies all checkpointed registers, but
 * restores r1, r2 from the PACA before exit.
 * If r5 != 0, this restores the MSR TM/FP/VEC/VSX bits to their state on entry.
 */
_GLOBAL(__kvmppc_restore_tm)
	mflr	r0
	std	r0, PPC_LR_STKOFF(r1)

	cmpdi	cr7, r5, 0

	/* Turn on TM/FP/VSX/VMX so we can restore them. */
	mfmsr	r5
	mr	r10, r5
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
	ld	r5, VCPU_TFHAR(r3)
	ld	r6, VCPU_TFIAR(r3)
	ld	r7, VCPU_TEXASR(r3)
	mtspr	SPRN_TFHAR, r5
	mtspr	SPRN_TFIAR, r6
	mtspr	SPRN_TEXASR, r7

	mr	r5, r4
	rldicl. r5, r5, 64 - MSR_TS_S_LG, 62
	beq	9f		/* TM not active in guest */

	/* Make sure the failure summary is set, otherwise we'll program check
	 * when we trechkpt.  It's possible that this might have been not set
	 * on a kvmppc_set_one_reg() call but we shouldn't let this crash the
	 * host.
	 */
	oris	r7, r7, (TEXASR_FS)@h
	mtspr	SPRN_TEXASR, r7

	/*
	 * Make a stack frame and save non-volatile registers if requested.
	 */
	stdu	r1, -SWITCH_FRAME_SIZE(r1)
	std	r1, HSTATE_SCRATCH2(r13)

	mfcr	r6
	mfspr	r7, SPRN_DSCR
	SAVE_GPR(2, r1)
	SAVE_GPR(6, r1)
	SAVE_GPR(7, r1)

	beq	cr7, 4f
	SAVE_NVGPRS(r1)

	/* MSR[TS] will be 1 (suspended) once we do trechkpt */
	li	r0, 1
	rldimi	r10, r0, MSR_TS_S_LG, 63 - MSR_TS_T_LG
	SAVE_GPR(10, r1)	/* final MSR value */
4:
	/*
	 * We need to load up the checkpointed state for the guest.
	 * We need to do this early as it will blow away any GPRs, VSRs and
	 * some SPRs.
	 */

	mr	r31, r3
	addi	r3, r31, VCPU_FPRS_TM
	bl	load_fp_state
	addi	r3, r31, VCPU_VRS_TM
	bl	load_vr_state
	mr	r3, r31
	lwz	r7, VCPU_VRSAVE_TM(r3)
	mtspr	SPRN_VRSAVE, r7

	ld	r5, VCPU_LR_TM(r3)
	lwz	r6, VCPU_CR_TM(r3)
	ld	r7, VCPU_CTR_TM(r3)
	ld	r8, VCPU_AMR_TM(r3)
	ld	r9, VCPU_TAR_TM(r3)
	ld	r10, VCPU_XER_TM(r3)
	mtlr	r5
	mtcr	r6
	mtctr	r7
	mtspr	SPRN_AMR, r8
	mtspr	SPRN_TAR, r9
	mtxer	r10

	/*
	 * Load up PPR and DSCR values but don't put them in the actual SPRs
	 * till the last moment to avoid running with userspace PPR and DSCR for
	 * too long.
	 */
	ld	r29, VCPU_DSCR_TM(r3)
	ld	r30, VCPU_PPR_TM(r3)

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
	ld	r1, HSTATE_SCRATCH2(r13)
	REST_GPR(7, r1)
	mtspr	SPRN_DSCR, r7

	/* Set the MSR RI since we have our registers back. */
	li	r5, MSR_RI
	mtmsrd	r5, 1

	/* Restore TOC pointer and CR */
	REST_GPR(2, r1)
	REST_GPR(6, r1)
	mtcr	r6

	/* Restore non-volatile registers if requested to. */
	beq	cr7, 5f
	REST_GPR(10, r1)
	REST_NVGPRS(r1)

5:	addi	r1, r1, SWITCH_FRAME_SIZE
	ld	r0, PPC_LR_STKOFF(r1)
	mtlr	r0

9:	/* Restore MSR bits if requested */
	beqlr	cr7
	mtmsrd	r10, 0
	blr

/*
 * _kvmppc_restore_tm_pr() is a wrapper around __kvmppc_restore_tm(), so that it
 * can be invoked from C function by PR KVM only.
 */
_GLOBAL(_kvmppc_restore_tm_pr)
	mflr	r0
	std	r0, PPC_LR_STKOFF(r1)
	stdu    r1, -PPC_MIN_STKFRM(r1)

	/* save TAR so that it can be recovered later */
	mfspr   r8, SPRN_TAR
	std	r8, PPC_MIN_STKFRM-8(r1)

	li	r5, 1
	bl	__kvmppc_restore_tm

	ld	r8, PPC_MIN_STKFRM-8(r1)
	mtspr   SPRN_TAR, r8

	addi    r1, r1, PPC_MIN_STKFRM
	ld	r0, PPC_LR_STKOFF(r1)
	mtlr	r0
	blr

EXPORT_SYMBOL_GPL(_kvmppc_restore_tm_pr);
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */
