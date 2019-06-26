/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Transactional memory support routines to reclaim and recheckpoint
 * transactional process state.
 *
 * Copyright 2012 Matt Evans & Michael Neuling, IBM Corporation.
 */

#include <asm/asm-offsets.h>
#include <asm/ppc_asm.h>
#include <asm/ppc-opcode.h>
#include <asm/ptrace.h>
#include <asm/reg.h>
#include <asm/bug.h>
#include <asm/export.h>
#include <asm/feature-fixups.h>

#ifdef CONFIG_VSX
/* See fpu.S, this is borrowed from there */
#define __SAVE_32FPRS_VSRS(n,c,base)		\
BEGIN_FTR_SECTION				\
	b	2f;				\
END_FTR_SECTION_IFSET(CPU_FTR_VSX);		\
	SAVE_32FPRS(n,base);			\
	b	3f;				\
2:	SAVE_32VSRS(n,c,base);			\
3:
#define __REST_32FPRS_VSRS(n,c,base)		\
BEGIN_FTR_SECTION				\
	b	2f;				\
END_FTR_SECTION_IFSET(CPU_FTR_VSX);		\
	REST_32FPRS(n,base);			\
	b	3f;				\
2:	REST_32VSRS(n,c,base);			\
3:
#else
#define __SAVE_32FPRS_VSRS(n,c,base)	SAVE_32FPRS(n, base)
#define __REST_32FPRS_VSRS(n,c,base)	REST_32FPRS(n, base)
#endif
#define SAVE_32FPRS_VSRS(n,c,base) \
	__SAVE_32FPRS_VSRS(n,__REG_##c,__REG_##base)
#define REST_32FPRS_VSRS(n,c,base) \
	__REST_32FPRS_VSRS(n,__REG_##c,__REG_##base)

/* Stack frame offsets for local variables. */
#define TM_FRAME_L0	TM_FRAME_SIZE-16
#define TM_FRAME_L1	TM_FRAME_SIZE-8


/* In order to access the TM SPRs, TM must be enabled.  So, do so: */
_GLOBAL(tm_enable)
	mfmsr	r4
	li	r3, MSR_TM >> 32
	sldi	r3, r3, 32
	and.	r0, r4, r3
	bne	1f
	or	r4, r4, r3
	mtmsrd	r4
1:	blr
EXPORT_SYMBOL_GPL(tm_enable);

_GLOBAL(tm_disable)
	mfmsr	r4
	li	r3, MSR_TM >> 32
	sldi	r3, r3, 32
	andc	r4, r4, r3
	mtmsrd	r4
	blr
EXPORT_SYMBOL_GPL(tm_disable);

_GLOBAL(tm_save_sprs)
	mfspr	r0, SPRN_TFHAR
	std	r0, THREAD_TM_TFHAR(r3)
	mfspr	r0, SPRN_TEXASR
	std	r0, THREAD_TM_TEXASR(r3)
	mfspr	r0, SPRN_TFIAR
	std	r0, THREAD_TM_TFIAR(r3)
	blr

_GLOBAL(tm_restore_sprs)
	ld	r0, THREAD_TM_TFHAR(r3)
	mtspr	SPRN_TFHAR, r0
	ld	r0, THREAD_TM_TEXASR(r3)
	mtspr	SPRN_TEXASR, r0
	ld	r0, THREAD_TM_TFIAR(r3)
	mtspr	SPRN_TFIAR, r0
	blr

	/* Passed an 8-bit failure cause as first argument. */
_GLOBAL(tm_abort)
	TABORT(R3)
	blr
EXPORT_SYMBOL_GPL(tm_abort);

/*
 * void tm_reclaim(struct thread_struct *thread,
 *		   uint8_t cause)
 *
 *	- Performs a full reclaim.  This destroys outstanding
 *	  transactions and updates thread.ckpt_regs, thread.ckfp_state and
 *	  thread.ckvr_state with the original checkpointed state.  Note that
 *	  thread->regs is unchanged.
 *
 * Purpose is to both abort transactions of, and preserve the state of,
 * a transactions at a context switch. We preserve/restore both sets of process
 * state to restore them when the thread's scheduled again.  We continue in
 * userland as though nothing happened, but when the transaction is resumed
 * they will abort back to the checkpointed state we save out here.
 *
 * Call with IRQs off, stacks get all out of sync for some periods in here!
 */
_GLOBAL(tm_reclaim)
	mfcr	r5
	mflr	r0
	stw	r5, 8(r1)
	std	r0, 16(r1)
	std	r2, STK_GOT(r1)
	stdu	r1, -TM_FRAME_SIZE(r1)

	/* We've a struct pt_regs at [r1+STACK_FRAME_OVERHEAD]. */

	std	r3, STK_PARAM(R3)(r1)
	SAVE_NVGPRS(r1)

	/* We need to setup MSR for VSX register save instructions. */
	mfmsr	r14
	mr	r15, r14
	ori	r15, r15, MSR_FP
	li	r16, 0
	ori	r16, r16, MSR_EE /* IRQs hard off */
	andc	r15, r15, r16
	oris	r15, r15, MSR_VEC@h
#ifdef CONFIG_VSX
	BEGIN_FTR_SECTION
	oris	r15,r15, MSR_VSX@h
	END_FTR_SECTION_IFSET(CPU_FTR_VSX)
#endif
	mtmsrd	r15
	std	r14, TM_FRAME_L0(r1)

	/* Do sanity check on MSR to make sure we are suspended */
	li	r7, (MSR_TS_S)@higher
	srdi	r6, r14, 32
	and	r6, r6, r7
1:	tdeqi   r6, 0
	EMIT_BUG_ENTRY 1b,__FILE__,__LINE__,0

	/* Stash the stack pointer away for use after reclaim */
	std	r1, PACAR1(r13)

	/* Clear MSR RI since we are about to change r1, EE is already off. */
	li	r5, 0
	mtmsrd	r5, 1

	/*
	 * BE CAREFUL HERE:
	 * At this point we can't take an SLB miss since we have MSR_RI
	 * off. Load only to/from the stack/paca which are in SLB bolted regions
	 * until we turn MSR RI back on.
	 *
	 * The moment we treclaim, ALL of our GPRs will switch
	 * to user register state.  (FPRs, CCR etc. also!)
	 * Use an sprg and a tm_scratch in the PACA to shuffle.
	 */
	TRECLAIM(R4)				/* Cause in r4 */

	/*
	 * ******************** GPRs ********************
	 * Stash the checkpointed r13 in the scratch SPR and get the real paca.
	 */
	SET_SCRATCH0(r13)
	GET_PACA(r13)

	/*
	 * Stash the checkpointed r1 away in paca->tm_scratch and get the real
	 * stack pointer back into r1.
	 */
	std	r1, PACATMSCRATCH(r13)
	ld	r1, PACAR1(r13)

	std	r11, GPR11(r1)			/* Temporary stash */

	/*
	 * Move the saved user r1 to the kernel stack in case PACATMSCRATCH is
	 * clobbered by an exception once we turn on MSR_RI below.
	 */
	ld	r11, PACATMSCRATCH(r13)
	std	r11, GPR1(r1)

	/*
	 * Store r13 away so we can free up the scratch SPR for the SLB fault
	 * handler (needed once we start accessing the thread_struct).
	 */
	GET_SCRATCH0(r11)
	std	r11, GPR13(r1)

	/* Reset MSR RI so we can take SLB faults again */
	li	r11, MSR_RI
	mtmsrd	r11, 1

	/* Store the PPR in r11 and reset to decent value */
	mfspr	r11, SPRN_PPR
	HMT_MEDIUM

	/* Now get some more GPRS free */
	std	r7, GPR7(r1)			/* Temporary stash */
	std	r12, GPR12(r1)			/* ''   ''    ''   */
	ld	r12, STK_PARAM(R3)(r1)		/* Param 0, thread_struct * */

	std	r11, THREAD_TM_PPR(r12)		/* Store PPR and free r11 */

	addi	r7, r12, PT_CKPT_REGS		/* Thread's ckpt_regs */

	/*
	 * Make r7 look like an exception frame so that we can use the neat
	 * GPRx(n) macros. r7 is NOT a pt_regs ptr!
	 */
	subi	r7, r7, STACK_FRAME_OVERHEAD

	/* Sync the userland GPRs 2-12, 14-31 to thread->regs: */
	SAVE_GPR(0, r7)				/* user r0 */
	SAVE_GPR(2, r7)				/* user r2 */
	SAVE_4GPRS(3, r7)			/* user r3-r6 */
	SAVE_GPR(8, r7)				/* user r8 */
	SAVE_GPR(9, r7)				/* user r9 */
	SAVE_GPR(10, r7)			/* user r10 */
	ld	r3, GPR1(r1)			/* user r1 */
	ld	r4, GPR7(r1)			/* user r7 */
	ld	r5, GPR11(r1)			/* user r11 */
	ld	r6, GPR12(r1)			/* user r12 */
	ld	r8, GPR13(r1)			/* user r13 */
	std	r3, GPR1(r7)
	std	r4, GPR7(r7)
	std	r5, GPR11(r7)
	std	r6, GPR12(r7)
	std	r8, GPR13(r7)

	SAVE_NVGPRS(r7)				/* user r14-r31 */

	/* ******************** NIP ******************** */
	mfspr	r3, SPRN_TFHAR
	std	r3, _NIP(r7)			/* Returns to failhandler */
	/*
	 * The checkpointed NIP is ignored when rescheduling/rechkpting,
	 * but is used in signal return to 'wind back' to the abort handler.
	 */

	/* ******************** CR,LR,CCR,MSR ********** */
	mfctr	r3
	mflr	r4
	mfcr	r5
	mfxer	r6

	std	r3, _CTR(r7)
	std	r4, _LINK(r7)
	std	r5, _CCR(r7)
	std	r6, _XER(r7)


	/* ******************** TAR, DSCR ********** */
	mfspr	r3, SPRN_TAR
	mfspr	r4, SPRN_DSCR

	std	r3, THREAD_TM_TAR(r12)
	std	r4, THREAD_TM_DSCR(r12)

	/*
	 * MSR and flags: We don't change CRs, and we don't need to alter MSR.
	 */


	/*
	 * ******************** FPR/VR/VSRs ************
	 * After reclaiming, capture the checkpointed FPRs/VRs.
	 *
	 * We enabled VEC/FP/VSX in the msr above, so we can execute these
	 * instructions!
	 */
	mr	r3, r12

	/* Altivec (VEC/VMX/VR)*/
	addi	r7, r3, THREAD_CKVRSTATE
	SAVE_32VRS(0, r6, r7)	/* r6 scratch, r7 ckvr_state */
	mfvscr	v0
	li	r6, VRSTATE_VSCR
	stvx	v0, r7, r6

	/* VRSAVE */
	mfspr	r0, SPRN_VRSAVE
	std	r0, THREAD_CKVRSAVE(r3)

	/* Floating Point (FP) */
	addi	r7, r3, THREAD_CKFPSTATE
	SAVE_32FPRS_VSRS(0, R6, R7)	/* r6 scratch, r7 ckfp_state */
	mffs    fr0
	stfd    fr0,FPSTATE_FPSCR(r7)


	/*
	 * TM regs, incl TEXASR -- these live in thread_struct.  Note they've
	 * been updated by the treclaim, to explain to userland the failure
	 * cause (aborted).
	 */
	mfspr	r0, SPRN_TEXASR
	mfspr	r3, SPRN_TFHAR
	mfspr	r4, SPRN_TFIAR
	std	r0, THREAD_TM_TEXASR(r12)
	std	r3, THREAD_TM_TFHAR(r12)
	std	r4, THREAD_TM_TFIAR(r12)

	/* AMR is checkpointed too, but is unsupported by Linux. */

	/* Restore original MSR/IRQ state & clear TM mode */
	ld	r14, TM_FRAME_L0(r1)		/* Orig MSR */

	li	r15, 0
	rldimi  r14, r15, MSR_TS_LG, (63-MSR_TS_LG)-1
	mtmsrd  r14

	REST_NVGPRS(r1)

	addi    r1, r1, TM_FRAME_SIZE
	lwz	r4, 8(r1)
	ld	r0, 16(r1)
	mtcr	r4
	mtlr	r0
	ld	r2, STK_GOT(r1)

	/* Load CPU's default DSCR */
	ld	r0, PACA_DSCR_DEFAULT(r13)
	mtspr	SPRN_DSCR, r0

	blr


	/*
	 * void __tm_recheckpoint(struct thread_struct *thread)
	 *	- Restore the checkpointed register state saved by tm_reclaim
	 *	  when we switch_to a process.
	 *
	 *	Call with IRQs off, stacks get all out of sync for
	 *	some periods in here!
	 */
_GLOBAL(__tm_recheckpoint)
	mfcr	r5
	mflr	r0
	stw	r5, 8(r1)
	std	r0, 16(r1)
	std	r2, STK_GOT(r1)
	stdu	r1, -TM_FRAME_SIZE(r1)

	/*
	 * We've a struct pt_regs at [r1+STACK_FRAME_OVERHEAD].
	 * This is used for backing up the NVGPRs:
	 */
	SAVE_NVGPRS(r1)

	/* Load complete register state from ts_ckpt* registers */

	addi	r7, r3, PT_CKPT_REGS		/* Thread's ckpt_regs */

	/*
	 * Make r7 look like an exception frame so that we can use the neat
	 * GPRx(n) macros. r7 is now NOT a pt_regs ptr!
	 */
	subi	r7, r7, STACK_FRAME_OVERHEAD

	/* We need to setup MSR for FP/VMX/VSX register save instructions. */
	mfmsr	r6
	mr	r5, r6
	ori	r5, r5, MSR_FP
#ifdef CONFIG_ALTIVEC
	oris	r5, r5, MSR_VEC@h
#endif
#ifdef CONFIG_VSX
	BEGIN_FTR_SECTION
	oris	r5,r5, MSR_VSX@h
	END_FTR_SECTION_IFSET(CPU_FTR_VSX)
#endif
	mtmsrd	r5

#ifdef CONFIG_ALTIVEC
	/*
	 * FP and VEC registers: These are recheckpointed from
	 * thread.ckfp_state and thread.ckvr_state respectively. The
	 * thread.fp_state[] version holds the 'live' (transactional)
	 * and will be loaded subsequently by any FPUnavailable trap.
	 */
	addi	r8, r3, THREAD_CKVRSTATE
	li	r5, VRSTATE_VSCR
	lvx	v0, r8, r5
	mtvscr	v0
	REST_32VRS(0, r5, r8)			/* r5 scratch, r8 ptr */
	ld	r5, THREAD_CKVRSAVE(r3)
	mtspr	SPRN_VRSAVE, r5
#endif

	addi	r8, r3, THREAD_CKFPSTATE
	lfd	fr0, FPSTATE_FPSCR(r8)
	MTFSF_L(fr0)
	REST_32FPRS_VSRS(0, R4, R8)

	mtmsr	r6				/* FP/Vec off again! */

restore_gprs:

	/* ******************** CR,LR,CCR,MSR ********** */
	ld	r4, _CTR(r7)
	ld	r5, _LINK(r7)
	ld	r8, _XER(r7)

	mtctr	r4
	mtlr	r5
	mtxer	r8

	/* ******************** TAR ******************** */
	ld	r4, THREAD_TM_TAR(r3)
	mtspr	SPRN_TAR,	r4

	/* Load up the PPR and DSCR in GPRs only at this stage */
	ld	r5, THREAD_TM_DSCR(r3)
	ld	r6, THREAD_TM_PPR(r3)

	REST_GPR(0, r7)				/* GPR0 */
	REST_2GPRS(2, r7)			/* GPR2-3 */
	REST_GPR(4, r7)				/* GPR4 */
	REST_4GPRS(8, r7)			/* GPR8-11 */
	REST_2GPRS(12, r7)			/* GPR12-13 */

	REST_NVGPRS(r7)				/* GPR14-31 */

	/* Load up PPR and DSCR here so we don't run with user values for long */
	mtspr	SPRN_DSCR, r5
	mtspr	SPRN_PPR, r6

	/*
	 * Do final sanity check on TEXASR to make sure FS is set. Do this
	 * here before we load up the userspace r1 so any bugs we hit will get
	 * a call chain.
	 */
	mfspr	r5, SPRN_TEXASR
	srdi	r5, r5, 16
	li	r6, (TEXASR_FS)@h
	and	r6, r6, r5
1:	tdeqi	r6, 0
	EMIT_BUG_ENTRY 1b,__FILE__,__LINE__,0

	/*
	 * Do final sanity check on MSR to make sure we are not transactional
	 * or suspended.
	 */
	mfmsr   r6
	li	r5, (MSR_TS_MASK)@higher
	srdi	r6, r6, 32
	and	r6, r6, r5
1:	tdnei   r6, 0
	EMIT_BUG_ENTRY 1b,__FILE__,__LINE__,0

	/* Restore CR */
	ld	r6, _CCR(r7)
	mtcr    r6

	REST_GPR(6, r7)

	/*
	 * Store r1 and r5 on the stack so that we can access them after we
	 * clear MSR RI.
	 */

	REST_GPR(5, r7)
	std	r5, -8(r1)
	ld	r5, GPR1(r7)
	std	r5, -16(r1)

	REST_GPR(7, r7)

	/* Clear MSR RI since we are about to change r1. EE is already off */
	li	r5, 0
	mtmsrd	r5, 1

	/*
	 * BE CAREFUL HERE:
	 * At this point we can't take an SLB miss since we have MSR_RI
	 * off. Load only to/from the stack/paca which are in SLB bolted regions
	 * until we turn MSR RI back on.
	 */

	SET_SCRATCH0(r1)
	ld	r5, -8(r1)
	ld	r1, -16(r1)

	/* Commit register state as checkpointed state: */
	TRECHKPT

	HMT_MEDIUM

	/*
	 * Our transactional state has now changed.
	 *
	 * Now just get out of here.  Transactional (current) state will be
	 * updated once restore is called on the return path in the _switch-ed
	 * -to process.
	 */

	GET_PACA(r13)
	GET_SCRATCH0(r1)

	/* R1 is restored, so we are recoverable again.  EE is still off */
	li	r4, MSR_RI
	mtmsrd	r4, 1

	REST_NVGPRS(r1)

	addi    r1, r1, TM_FRAME_SIZE
	lwz	r4, 8(r1)
	ld	r0, 16(r1)
	mtcr	r4
	mtlr	r0
	ld	r2, STK_GOT(r1)

	/* Load CPU's default DSCR */
	ld	r0, PACA_DSCR_DEFAULT(r13)
	mtspr	SPRN_DSCR, r0

	blr

	/* ****************************************************************** */
