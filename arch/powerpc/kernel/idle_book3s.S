/*
 *  This file contains idle entry/exit functions for POWER7,
 *  POWER8 and POWER9 CPUs.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/threads.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ppc-opcode.h>
#include <asm/hw_irq.h>
#include <asm/kvm_book3s_asm.h>
#include <asm/opal.h>
#include <asm/cpuidle.h>
#include <asm/exception-64s.h>
#include <asm/book3s/64/mmu-hash.h>
#include <asm/mmu.h>

#undef DEBUG

/*
 * Use unused space in the interrupt stack to save and restore
 * registers for winkle support.
 */
#define _MMCR0	GPR0
#define _SDR1	GPR3
#define _PTCR	GPR3
#define _RPR	GPR4
#define _SPURR	GPR5
#define _PURR	GPR6
#define _TSCR	GPR7
#define _DSCR	GPR8
#define _AMOR	GPR9
#define _WORT	GPR10
#define _WORC	GPR11
#define _LPCR	GPR12

#define PSSCR_EC_ESL_MASK_SHIFTED          (PSSCR_EC | PSSCR_ESL) >> 16

	.text

/*
 * Used by threads before entering deep idle states. Saves SPRs
 * in interrupt stack frame
 */
save_sprs_to_stack:
	/*
	 * Note all register i.e per-core, per-subcore or per-thread is saved
	 * here since any thread in the core might wake up first
	 */
BEGIN_FTR_SECTION
	/*
	 * Note - SDR1 is dropped in Power ISA v3. Hence not restoring
	 * SDR1 here
	 */
	mfspr	r3,SPRN_PTCR
	std	r3,_PTCR(r1)
	mfspr	r3,SPRN_LPCR
	std	r3,_LPCR(r1)
FTR_SECTION_ELSE
	mfspr	r3,SPRN_SDR1
	std	r3,_SDR1(r1)
ALT_FTR_SECTION_END_IFSET(CPU_FTR_ARCH_300)
	mfspr	r3,SPRN_RPR
	std	r3,_RPR(r1)
	mfspr	r3,SPRN_SPURR
	std	r3,_SPURR(r1)
	mfspr	r3,SPRN_PURR
	std	r3,_PURR(r1)
	mfspr	r3,SPRN_TSCR
	std	r3,_TSCR(r1)
	mfspr	r3,SPRN_DSCR
	std	r3,_DSCR(r1)
	mfspr	r3,SPRN_AMOR
	std	r3,_AMOR(r1)
	mfspr	r3,SPRN_WORT
	std	r3,_WORT(r1)
	mfspr	r3,SPRN_WORC
	std	r3,_WORC(r1)
/*
 * On POWER9, there are idle states such as stop4, invoked via cpuidle,
 * that lose hypervisor resources. In such cases, we need to save
 * additional SPRs before entering those idle states so that they can
 * be restored to their older values on wakeup from the idle state.
 *
 * On POWER8, the only such deep idle state is winkle which is used
 * only in the context of CPU-Hotplug, where these additional SPRs are
 * reinitiazed to a sane value. Hence there is no need to save/restore
 * these SPRs.
 */
BEGIN_FTR_SECTION
	blr
END_FTR_SECTION_IFCLR(CPU_FTR_ARCH_300)

power9_save_additional_sprs:
	mfspr	r3, SPRN_PID
	mfspr	r4, SPRN_LDBAR
	std	r3, STOP_PID(r13)
	std	r4, STOP_LDBAR(r13)

	mfspr	r3, SPRN_FSCR
	mfspr	r4, SPRN_HFSCR
	std	r3, STOP_FSCR(r13)
	std	r4, STOP_HFSCR(r13)

	mfspr	r3, SPRN_MMCRA
	mfspr	r4, SPRN_MMCR0
	std	r3, STOP_MMCRA(r13)
	std	r4, _MMCR0(r1)

	mfspr	r3, SPRN_MMCR1
	mfspr	r4, SPRN_MMCR2
	std	r3, STOP_MMCR1(r13)
	std	r4, STOP_MMCR2(r13)
	blr

power9_restore_additional_sprs:
	ld	r3,_LPCR(r1)
	ld	r4, STOP_PID(r13)
	mtspr	SPRN_LPCR,r3
	mtspr	SPRN_PID, r4

	ld	r3, STOP_LDBAR(r13)
	ld	r4, STOP_FSCR(r13)
	mtspr	SPRN_LDBAR, r3
	mtspr	SPRN_FSCR, r4

	ld	r3, STOP_HFSCR(r13)
	ld	r4, STOP_MMCRA(r13)
	mtspr	SPRN_HFSCR, r3
	mtspr	SPRN_MMCRA, r4

	ld	r3, _MMCR0(r1)
	ld	r4, STOP_MMCR1(r13)
	mtspr	SPRN_MMCR0, r3
	mtspr	SPRN_MMCR1, r4

	ld	r3, STOP_MMCR2(r13)
	mtspr	SPRN_MMCR2, r3
	blr

/*
 * Used by threads when the lock bit of core_idle_state is set.
 * Threads will spin in HMT_LOW until the lock bit is cleared.
 * r14 - pointer to core_idle_state
 * r15 - used to load contents of core_idle_state
 * r9  - used as a temporary variable
 */

core_idle_lock_held:
	HMT_LOW
3:	lwz	r15,0(r14)
	andis.	r15,r15,PNV_CORE_IDLE_LOCK_BIT@h
	bne	3b
	HMT_MEDIUM
	lwarx	r15,0,r14
	andis.	r9,r15,PNV_CORE_IDLE_LOCK_BIT@h
	bne-	core_idle_lock_held
	blr

/*
 * Pass requested state in r3:
 *	r3 - PNV_THREAD_NAP/SLEEP/WINKLE in POWER8
 *	   - Requested PSSCR value in POWER9
 *
 * Address of idle handler to branch to in realmode in r4
 */
pnv_powersave_common:
	/* Use r3 to pass state nap/sleep/winkle */
	/* NAP is a state loss, we create a regs frame on the
	 * stack, fill it up with the state we care about and
	 * stick a pointer to it in PACAR1. We really only
	 * need to save PC, some CR bits and the NV GPRs,
	 * but for now an interrupt frame will do.
	 */
	mtctr	r4

	mflr	r0
	std	r0,16(r1)
	stdu	r1,-INT_FRAME_SIZE(r1)
	std	r0,_LINK(r1)
	std	r0,_NIP(r1)

	/* We haven't lost state ... yet */
	li	r0,0
	stb	r0,PACA_NAPSTATELOST(r13)

	/* Continue saving state */
	SAVE_GPR(2, r1)
	SAVE_NVGPRS(r1)
	mfcr	r5
	std	r5,_CCR(r1)
	std	r1,PACAR1(r13)

BEGIN_FTR_SECTION
	/*
	 * POWER9 does not require real mode to stop, and presently does not
	 * set hwthread_state for KVM (threads don't share MMU context), so
	 * we can remain in virtual mode for this.
	 */
	bctr
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_300)
	/*
	 * POWER8
	 * Go to real mode to do the nap, as required by the architecture.
	 * Also, we need to be in real mode before setting hwthread_state,
	 * because as soon as we do that, another thread can switch
	 * the MMU context to the guest.
	 */
	LOAD_REG_IMMEDIATE(r7, MSR_IDLE)
	mtmsrd	r7,0
	bctr

/*
 * This is the sequence required to execute idle instructions, as
 * specified in ISA v2.07 (and earlier). MSR[IR] and MSR[DR] must be 0.
 */
#define IDLE_STATE_ENTER_SEQ_NORET(IDLE_INST)			\
	/* Magic NAP/SLEEP/WINKLE mode enter sequence */	\
	std	r0,0(r1);					\
	ptesync;						\
	ld	r0,0(r1);					\
236:	cmpd	cr0,r0,r0;					\
	bne	236b;						\
	IDLE_INST;


	.globl pnv_enter_arch207_idle_mode
pnv_enter_arch207_idle_mode:
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	/* Tell KVM we're entering idle */
	li	r4,KVM_HWTHREAD_IN_IDLE
	/******************************************************/
	/*  N O T E   W E L L    ! ! !    N O T E   W E L L   */
	/* The following store to HSTATE_HWTHREAD_STATE(r13)  */
	/* MUST occur in real mode, i.e. with the MMU off,    */
	/* and the MMU must stay off until we clear this flag */
	/* and test HSTATE_HWTHREAD_REQ(r13) in               */
	/* pnv_powersave_wakeup in this file.                 */
	/* The reason is that another thread can switch the   */
	/* MMU to a guest context whenever this flag is set   */
	/* to KVM_HWTHREAD_IN_IDLE, and if the MMU was on,    */
	/* that would potentially cause this thread to start  */
	/* executing instructions from guest memory in        */
	/* hypervisor mode, leading to a host crash or data   */
	/* corruption, or worse.                              */
	/******************************************************/
	stb	r4,HSTATE_HWTHREAD_STATE(r13)
#endif
	stb	r3,PACA_THREAD_IDLE_STATE(r13)
	cmpwi	cr3,r3,PNV_THREAD_SLEEP
	bge	cr3,2f
	IDLE_STATE_ENTER_SEQ_NORET(PPC_NAP)
	/* No return */
2:
	/* Sleep or winkle */
	lbz	r7,PACA_THREAD_MASK(r13)
	ld	r14,PACA_CORE_IDLE_STATE_PTR(r13)
	li	r5,0
	beq	cr3,3f
	lis	r5,PNV_CORE_IDLE_WINKLE_COUNT@h
3:
lwarx_loop1:
	lwarx	r15,0,r14

	andis.	r9,r15,PNV_CORE_IDLE_LOCK_BIT@h
	bnel-	core_idle_lock_held

	add	r15,r15,r5			/* Add if winkle */
	andc	r15,r15,r7			/* Clear thread bit */

	andi.	r9,r15,PNV_CORE_IDLE_THREAD_BITS

/*
 * If cr0 = 0, then current thread is the last thread of the core entering
 * sleep. Last thread needs to execute the hardware bug workaround code if
 * required by the platform.
 * Make the workaround call unconditionally here. The below branch call is
 * patched out when the idle states are discovered if the platform does not
 * require it.
 */
.global pnv_fastsleep_workaround_at_entry
pnv_fastsleep_workaround_at_entry:
	beq	fastsleep_workaround_at_entry

	stwcx.	r15,0,r14
	bne-	lwarx_loop1
	isync

common_enter: /* common code for all the threads entering sleep or winkle */
	bgt	cr3,enter_winkle
	IDLE_STATE_ENTER_SEQ_NORET(PPC_SLEEP)

fastsleep_workaround_at_entry:
	oris	r15,r15,PNV_CORE_IDLE_LOCK_BIT@h
	stwcx.	r15,0,r14
	bne-	lwarx_loop1
	isync

	/* Fast sleep workaround */
	li	r3,1
	li	r4,1
	bl	opal_config_cpu_idle_state

	/* Unlock */
	xoris	r15,r15,PNV_CORE_IDLE_LOCK_BIT@h
	lwsync
	stw	r15,0(r14)
	b	common_enter

enter_winkle:
	bl	save_sprs_to_stack

	IDLE_STATE_ENTER_SEQ_NORET(PPC_WINKLE)

/*
 * r3 - PSSCR value corresponding to the requested stop state.
 */
power_enter_stop:
/*
 * Check if we are executing the lite variant with ESL=EC=0
 */
	andis.   r4,r3,PSSCR_EC_ESL_MASK_SHIFTED
	clrldi   r3,r3,60 /* r3 = Bits[60:63] = Requested Level (RL) */
	bne	 .Lhandle_esl_ec_set
	PPC_STOP
	li	r3,0  /* Since we didn't lose state, return 0 */
	std	r3, PACA_REQ_PSSCR(r13)

	/*
	 * pnv_wakeup_noloss() expects r12 to contain the SRR1 value so
	 * it can determine if the wakeup reason is an HMI in
	 * CHECK_HMI_INTERRUPT.
	 *
	 * However, when we wakeup with ESL=0, SRR1 will not contain the wakeup
	 * reason, so there is no point setting r12 to SRR1.
	 *
	 * Further, we clear r12 here, so that we don't accidentally enter the
	 * HMI in pnv_wakeup_noloss() if the value of r12[42:45] == WAKE_HMI.
	 */
	li	r12, 0
	b 	pnv_wakeup_noloss

.Lhandle_esl_ec_set:
BEGIN_FTR_SECTION
	/*
	 * POWER9 DD2.0 or earlier can incorrectly set PMAO when waking up after
	 * a state-loss idle. Saving and restoring MMCR0 over idle is a
	 * workaround.
	 */
	mfspr	r4,SPRN_MMCR0
	std	r4,_MMCR0(r1)
END_FTR_SECTION_IFCLR(CPU_FTR_POWER9_DD2_1)

/*
 * Check if the requested state is a deep idle state.
 */
	LOAD_REG_ADDRBASE(r5,pnv_first_deep_stop_state)
	ld	r4,ADDROFF(pnv_first_deep_stop_state)(r5)
	cmpd	r3,r4
	bge	.Lhandle_deep_stop
	PPC_STOP	/* Does not return (system reset interrupt) */

.Lhandle_deep_stop:
/*
 * Entering deep idle state.
 * Clear thread bit in PACA_CORE_IDLE_STATE, save SPRs to
 * stack and enter stop
 */
	lbz     r7,PACA_THREAD_MASK(r13)
	ld      r14,PACA_CORE_IDLE_STATE_PTR(r13)

lwarx_loop_stop:
	lwarx   r15,0,r14
	andis.	r9,r15,PNV_CORE_IDLE_LOCK_BIT@h
	bnel-	core_idle_lock_held
	andc    r15,r15,r7                      /* Clear thread bit */

	stwcx.  r15,0,r14
	bne-    lwarx_loop_stop
	isync

	bl	save_sprs_to_stack

	PPC_STOP	/* Does not return (system reset interrupt) */

/*
 * Entered with MSR[EE]=0 and no soft-masked interrupts pending.
 * r3 contains desired idle state (PNV_THREAD_NAP/SLEEP/WINKLE).
 */
_GLOBAL(power7_idle_insn)
	/* Now check if user or arch enabled NAP mode */
	LOAD_REG_ADDR(r4, pnv_enter_arch207_idle_mode)
	b	pnv_powersave_common

#define CHECK_HMI_INTERRUPT						\
BEGIN_FTR_SECTION_NESTED(66);						\
	rlwinm	r0,r12,45-31,0xf;  /* extract wake reason field (P8) */	\
FTR_SECTION_ELSE_NESTED(66);						\
	rlwinm	r0,r12,45-31,0xe;  /* P7 wake reason field is 3 bits */	\
ALT_FTR_SECTION_END_NESTED_IFSET(CPU_FTR_ARCH_207S, 66);		\
	cmpwi	r0,0xa;			/* Hypervisor maintenance ? */	\
	bne+	20f;							\
	/* Invoke opal call to handle hmi */				\
	ld	r2,PACATOC(r13);					\
	ld	r1,PACAR1(r13);						\
	std	r3,ORIG_GPR3(r1);	/* Save original r3 */		\
	li	r3,0;			/* NULL argument */		\
	bl	hmi_exception_realmode;					\
	nop;								\
	ld	r3,ORIG_GPR3(r1);	/* Restore original r3 */	\
20:	nop;

/*
 * Entered with MSR[EE]=0 and no soft-masked interrupts pending.
 * r3 contains desired PSSCR register value.
 *
 * Offline (CPU unplug) case also must notify KVM that the CPU is
 * idle.
 */
_GLOBAL(power9_offline_stop)
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	/*
	 * Tell KVM we're entering idle.
	 * This does not have to be done in real mode because the P9 MMU
	 * is independent per-thread. Some steppings share radix/hash mode
	 * between threads, but in that case KVM has a barrier sync in real
	 * mode before and after switching between radix and hash.
	 */
	li	r4,KVM_HWTHREAD_IN_IDLE
	stb	r4,HSTATE_HWTHREAD_STATE(r13)
#endif
	/* fall through */

_GLOBAL(power9_idle_stop)
	std	r3, PACA_REQ_PSSCR(r13)
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
BEGIN_FTR_SECTION
	sync
	lwz	r5, PACA_DONT_STOP(r13)
	cmpwi	r5, 0
	bne	1f
END_FTR_SECTION_IFSET(CPU_FTR_P9_TM_XER_SO_BUG)
#endif
	mtspr 	SPRN_PSSCR,r3
	LOAD_REG_ADDR(r4,power_enter_stop)
	b	pnv_powersave_common
	/* No return */
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
1:
	/*
	 * We get here when TM / thread reconfiguration bug workaround
	 * code wants to get the CPU into SMT4 mode, and therefore
	 * we are being asked not to stop.
	 */
	li	r3, 0
	std	r3, PACA_REQ_PSSCR(r13)
	blr		/* return 0 for wakeup cause / SRR1 value */
#endif

/*
 * On waking up from stop 0,1,2 with ESL=1 on POWER9 DD1,
 * HSPRG0 will be set to the HSPRG0 value of one of the
 * threads in this core. Thus the value we have in r13
 * may not be this thread's paca pointer.
 *
 * Fortunately, the TIR remains invariant. Since this thread's
 * paca pointer is recorded in all its sibling's paca, we can
 * correctly recover this thread's paca pointer if we
 * know the index of this thread in the core.
 *
 * This index can be obtained from the TIR.
 *
 * i.e, thread's position in the core = TIR.
 * If this value is i, then this thread's paca is
 * paca->thread_sibling_pacas[i].
 */
power9_dd1_recover_paca:
	mfspr	r4, SPRN_TIR
	/*
	 * Since each entry in thread_sibling_pacas is 8 bytes
	 * we need to left-shift by 3 bits. Thus r4 = i * 8
	 */
	sldi	r4, r4, 3
	/* Get &paca->thread_sibling_pacas[0] in r5 */
	ld	r5, PACA_SIBLING_PACA_PTRS(r13)
	/* Load paca->thread_sibling_pacas[i] into r13 */
	ldx	r13, r4, r5
	SET_PACA(r13)
	/*
	 * Indicate that we have lost NVGPR state
	 * which needs to be restored from the stack.
	 */
	li	r3, 1
	stb	r3,PACA_NAPSTATELOST(r13)
	blr

/*
 * Called from machine check handler for powersave wakeups.
 * Low level machine check processing has already been done. Now just
 * go through the wake up path to get everything in order.
 *
 * r3 - The original SRR1 value.
 * Original SRR[01] have been clobbered.
 * MSR_RI is clear.
 */
.global pnv_powersave_wakeup_mce
pnv_powersave_wakeup_mce:
	/* Set cr3 for pnv_powersave_wakeup */
	rlwinm	r11,r3,47-31,30,31
	cmpwi	cr3,r11,2

	/*
	 * Now put the original SRR1 with SRR1_WAKEMCE_RESVD as the wake
	 * reason into r12, which allows reuse of the system reset wakeup
	 * code without being mistaken for another type of wakeup.
	 */
	oris	r12,r3,SRR1_WAKEMCE_RESVD@h

	b	pnv_powersave_wakeup

/*
 * Called from reset vector for powersave wakeups.
 * cr3 - set to gt if waking up with partial/complete hypervisor state loss
 * r12 - SRR1
 */
.global pnv_powersave_wakeup
pnv_powersave_wakeup:
	ld	r2, PACATOC(r13)

BEGIN_FTR_SECTION
BEGIN_FTR_SECTION_NESTED(70)
	bl	power9_dd1_recover_paca
END_FTR_SECTION_NESTED_IFSET(CPU_FTR_POWER9_DD1, 70)
	bl	pnv_restore_hyp_resource_arch300
FTR_SECTION_ELSE
	bl	pnv_restore_hyp_resource_arch207
ALT_FTR_SECTION_END_IFSET(CPU_FTR_ARCH_300)

	li	r0,PNV_THREAD_RUNNING
	stb	r0,PACA_THREAD_IDLE_STATE(r13)	/* Clear thread state */

	mr	r3,r12

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	lbz	r0,HSTATE_HWTHREAD_STATE(r13)
	cmpwi	r0,KVM_HWTHREAD_IN_KERNEL
	beq	0f
	li	r0,KVM_HWTHREAD_IN_KERNEL
	stb	r0,HSTATE_HWTHREAD_STATE(r13)
	/* Order setting hwthread_state vs. testing hwthread_req */
	sync
0:	lbz	r0,HSTATE_HWTHREAD_REQ(r13)
	cmpwi	r0,0
	beq	1f
	b	kvm_start_guest
1:
#endif

	/* Return SRR1 from power7_nap() */
	blt	cr3,pnv_wakeup_noloss
	b	pnv_wakeup_loss

/*
 * Check whether we have woken up with hypervisor state loss.
 * If yes, restore hypervisor state and return back to link.
 *
 * cr3 - set to gt if waking up with partial/complete hypervisor state loss
 */
pnv_restore_hyp_resource_arch300:
	/*
	 * Workaround for POWER9, if we lost resources, the ERAT
	 * might have been mixed up and needs flushing. We also need
	 * to reload MMCR0 (see comment above). We also need to set
	 * then clear bit 60 in MMCRA to ensure the PMU starts running.
	 */
	blt	cr3,1f
BEGIN_FTR_SECTION
	PPC_INVALIDATE_ERAT
	ld	r1,PACAR1(r13)
	ld	r4,_MMCR0(r1)
	mtspr	SPRN_MMCR0,r4
END_FTR_SECTION_IFCLR(CPU_FTR_POWER9_DD2_1)
	mfspr	r4,SPRN_MMCRA
	ori	r4,r4,(1 << (63-60))
	mtspr	SPRN_MMCRA,r4
	xori	r4,r4,(1 << (63-60))
	mtspr	SPRN_MMCRA,r4
1:
	/*
	 * POWER ISA 3. Use PSSCR to determine if we
	 * are waking up from deep idle state
	 */
	LOAD_REG_ADDRBASE(r5,pnv_first_deep_stop_state)
	ld	r4,ADDROFF(pnv_first_deep_stop_state)(r5)

BEGIN_FTR_SECTION_NESTED(71)
	/*
	 * Assume that we are waking up from the state
	 * same as the Requested Level (RL) in the PSSCR
	 * which are Bits 60-63
	 */
	ld	r5,PACA_REQ_PSSCR(r13)
	rldicl  r5,r5,0,60
FTR_SECTION_ELSE_NESTED(71)
	/*
	 * 0-3 bits correspond to Power-Saving Level Status
	 * which indicates the idle state we are waking up from
	 */
	mfspr	r5, SPRN_PSSCR
	rldicl  r5,r5,4,60
ALT_FTR_SECTION_END_NESTED_IFSET(CPU_FTR_POWER9_DD1, 71)
	li	r0, 0		/* clear requested_psscr to say we're awake */
	std	r0, PACA_REQ_PSSCR(r13)
	cmpd	cr4,r5,r4
	bge	cr4,pnv_wakeup_tb_loss /* returns to caller */

	blr	/* Waking up without hypervisor state loss. */

/* Same calling convention as arch300 */
pnv_restore_hyp_resource_arch207:
	/*
	 * POWER ISA 2.07 or less.
	 * Check if we slept with sleep or winkle.
	 */
	lbz	r4,PACA_THREAD_IDLE_STATE(r13)
	cmpwi	cr2,r4,PNV_THREAD_NAP
	bgt	cr2,pnv_wakeup_tb_loss	/* Either sleep or Winkle */

	/*
	 * We fall through here if PACA_THREAD_IDLE_STATE shows we are waking
	 * up from nap. At this stage CR3 shouldn't contains 'gt' since that
	 * indicates we are waking with hypervisor state loss from nap.
	 */
	bgt	cr3,.

	blr	/* Waking up without hypervisor state loss */

/*
 * Called if waking up from idle state which can cause either partial or
 * complete hyp state loss.
 * In POWER8, called if waking up from fastsleep or winkle
 * In POWER9, called if waking up from stop state >= pnv_first_deep_stop_state
 *
 * r13 - PACA
 * cr3 - gt if waking up with partial/complete hypervisor state loss
 *
 * If ISA300:
 * cr4 - gt or eq if waking up from complete hypervisor state loss.
 *
 * If ISA207:
 * r4 - PACA_THREAD_IDLE_STATE
 */
pnv_wakeup_tb_loss:
	ld	r1,PACAR1(r13)
	/*
	 * Before entering any idle state, the NVGPRs are saved in the stack.
	 * If there was a state loss, or PACA_NAPSTATELOST was set, then the
	 * NVGPRs are restored. If we are here, it is likely that state is lost,
	 * but not guaranteed -- neither ISA207 nor ISA300 tests to reach
	 * here are the same as the test to restore NVGPRS:
	 * PACA_THREAD_IDLE_STATE test for ISA207, PSSCR test for ISA300,
	 * and SRR1 test for restoring NVGPRs.
	 *
	 * We are about to clobber NVGPRs now, so set NAPSTATELOST to
	 * guarantee they will always be restored. This might be tightened
	 * with careful reading of specs (particularly for ISA300) but this
	 * is already a slow wakeup path and it's simpler to be safe.
	 */
	li	r0,1
	stb	r0,PACA_NAPSTATELOST(r13)

	/*
	 *
	 * Save SRR1 and LR in NVGPRs as they might be clobbered in
	 * opal_call() (called in CHECK_HMI_INTERRUPT). SRR1 is required
	 * to determine the wakeup reason if we branch to kvm_start_guest. LR
	 * is required to return back to reset vector after hypervisor state
	 * restore is complete.
	 */
	mr	r19,r12
	mr	r18,r4
	mflr	r17
BEGIN_FTR_SECTION
	CHECK_HMI_INTERRUPT
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE)

	ld	r14,PACA_CORE_IDLE_STATE_PTR(r13)
	lbz	r7,PACA_THREAD_MASK(r13)

	/*
	 * Take the core lock to synchronize against other threads.
	 *
	 * Lock bit is set in one of the 2 cases-
	 * a. In the sleep/winkle enter path, the last thread is executing
	 * fastsleep workaround code.
	 * b. In the wake up path, another thread is executing fastsleep
	 * workaround undo code or resyncing timebase or restoring context
	 * In either case loop until the lock bit is cleared.
	 */
1:
	lwarx	r15,0,r14
	andis.	r9,r15,PNV_CORE_IDLE_LOCK_BIT@h
	bnel-	core_idle_lock_held
	oris	r15,r15,PNV_CORE_IDLE_LOCK_BIT@h
	stwcx.	r15,0,r14
	bne-	1b
	isync

	andi.	r9,r15,PNV_CORE_IDLE_THREAD_BITS
	cmpwi	cr2,r9,0

	/*
	 * At this stage
	 * cr2 - eq if first thread to wakeup in core
	 * cr3-  gt if waking up with partial/complete hypervisor state loss
	 * ISA300:
	 * cr4 - gt or eq if waking up from complete hypervisor state loss.
	 */

BEGIN_FTR_SECTION
	/*
	 * Were we in winkle?
	 * If yes, check if all threads were in winkle, decrement our
	 * winkle count, set all thread winkle bits if all were in winkle.
	 * Check if our thread has a winkle bit set, and set cr4 accordingly
	 * (to match ISA300, above). Pseudo-code for core idle state
	 * transitions for ISA207 is as follows (everything happens atomically
	 * due to store conditional and/or lock bit):
	 *
	 * nap_idle() { }
	 * nap_wake() { }
	 *
	 * sleep_idle()
	 * {
	 *	core_idle_state &= ~thread_in_core
	 * }
	 *
	 * sleep_wake()
	 * {
	 *     bool first_in_core, first_in_subcore;
	 *
	 *     first_in_core = (core_idle_state & IDLE_THREAD_BITS) == 0;
	 *     first_in_subcore = (core_idle_state & SUBCORE_SIBLING_MASK) == 0;
	 *
	 *     core_idle_state |= thread_in_core;
	 * }
	 *
	 * winkle_idle()
	 * {
	 *	core_idle_state &= ~thread_in_core;
	 *	core_idle_state += 1 << WINKLE_COUNT_SHIFT;
	 * }
	 *
	 * winkle_wake()
	 * {
	 *     bool first_in_core, first_in_subcore, winkle_state_lost;
	 *
	 *     first_in_core = (core_idle_state & IDLE_THREAD_BITS) == 0;
	 *     first_in_subcore = (core_idle_state & SUBCORE_SIBLING_MASK) == 0;
	 *
	 *     core_idle_state |= thread_in_core;
	 *
	 *     if ((core_idle_state & WINKLE_MASK) == (8 << WINKLE_COUNT_SIHFT))
	 *         core_idle_state |= THREAD_WINKLE_BITS;
	 *     core_idle_state -= 1 << WINKLE_COUNT_SHIFT;
	 *
	 *     winkle_state_lost = core_idle_state &
	 *				(thread_in_core << WINKLE_THREAD_SHIFT);
	 *     core_idle_state &= ~(thread_in_core << WINKLE_THREAD_SHIFT);
	 * }
	 *
	 */
	cmpwi	r18,PNV_THREAD_WINKLE
	bne	2f
	andis.	r9,r15,PNV_CORE_IDLE_WINKLE_COUNT_ALL_BIT@h
	subis	r15,r15,PNV_CORE_IDLE_WINKLE_COUNT@h
	beq	2f
	ori	r15,r15,PNV_CORE_IDLE_THREAD_WINKLE_BITS /* all were winkle */
2:
	/* Shift thread bit to winkle mask, then test if this thread is set,
	 * and remove it from the winkle bits */
	slwi	r8,r7,8
	and	r8,r8,r15
	andc	r15,r15,r8
	cmpwi	cr4,r8,1 /* cr4 will be gt if our bit is set, lt if not */

	lbz	r4,PACA_SUBCORE_SIBLING_MASK(r13)
	and	r4,r4,r15
	cmpwi	r4,0	/* Check if first in subcore */

	or	r15,r15,r7		/* Set thread bit */
	beq	first_thread_in_subcore
END_FTR_SECTION_IFCLR(CPU_FTR_ARCH_300)

	or	r15,r15,r7		/* Set thread bit */
	beq	cr2,first_thread_in_core

	/* Not first thread in core or subcore to wake up */
	b	clear_lock

first_thread_in_subcore:
	/*
	 * If waking up from sleep, subcore state is not lost. Hence
	 * skip subcore state restore
	 */
	blt	cr4,subcore_state_restored

	/* Restore per-subcore state */
	ld      r4,_SDR1(r1)
	mtspr   SPRN_SDR1,r4

	ld      r4,_RPR(r1)
	mtspr   SPRN_RPR,r4
	ld	r4,_AMOR(r1)
	mtspr	SPRN_AMOR,r4

subcore_state_restored:
	/*
	 * Check if the thread is also the first thread in the core. If not,
	 * skip to clear_lock.
	 */
	bne	cr2,clear_lock

first_thread_in_core:

	/*
	 * First thread in the core waking up from any state which can cause
	 * partial or complete hypervisor state loss. It needs to
	 * call the fastsleep workaround code if the platform requires it.
	 * Call it unconditionally here. The below branch instruction will
	 * be patched out if the platform does not have fastsleep or does not
	 * require the workaround. Patching will be performed during the
	 * discovery of idle-states.
	 */
.global pnv_fastsleep_workaround_at_exit
pnv_fastsleep_workaround_at_exit:
	b	fastsleep_workaround_at_exit

timebase_resync:
	/*
	 * Use cr3 which indicates that we are waking up with atleast partial
	 * hypervisor state loss to determine if TIMEBASE RESYNC is needed.
	 */
	ble	cr3,.Ltb_resynced
	/* Time base re-sync */
	bl	opal_resync_timebase;
	/*
	 * If waking up from sleep (POWER8), per core state
	 * is not lost, skip to clear_lock.
	 */
.Ltb_resynced:
	blt	cr4,clear_lock

	/*
	 * First thread in the core to wake up and its waking up with
	 * complete hypervisor state loss. Restore per core hypervisor
	 * state.
	 */
BEGIN_FTR_SECTION
	ld	r4,_PTCR(r1)
	mtspr	SPRN_PTCR,r4
	ld	r4,_RPR(r1)
	mtspr	SPRN_RPR,r4
	ld	r4,_AMOR(r1)
	mtspr	SPRN_AMOR,r4
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_300)

	ld	r4,_TSCR(r1)
	mtspr	SPRN_TSCR,r4
	ld	r4,_WORC(r1)
	mtspr	SPRN_WORC,r4

clear_lock:
	xoris	r15,r15,PNV_CORE_IDLE_LOCK_BIT@h
	lwsync
	stw	r15,0(r14)

common_exit:
	/*
	 * Common to all threads.
	 *
	 * If waking up from sleep, hypervisor state is not lost. Hence
	 * skip hypervisor state restore.
	 */
	blt	cr4,hypervisor_state_restored

	/* Waking up from winkle */

BEGIN_MMU_FTR_SECTION
	b	no_segments
END_MMU_FTR_SECTION_IFSET(MMU_FTR_TYPE_RADIX)
	/* Restore SLB  from PACA */
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
no_segments:

	/* Restore per thread state */

	ld	r4,_SPURR(r1)
	mtspr	SPRN_SPURR,r4
	ld	r4,_PURR(r1)
	mtspr	SPRN_PURR,r4
	ld	r4,_DSCR(r1)
	mtspr	SPRN_DSCR,r4
	ld	r4,_WORT(r1)
	mtspr	SPRN_WORT,r4

	/* Call cur_cpu_spec->cpu_restore() */
	LOAD_REG_ADDR(r4, cur_cpu_spec)
	ld	r4,0(r4)
	ld	r12,CPU_SPEC_RESTORE(r4)
#ifdef PPC64_ELF_ABI_v1
	ld	r12,0(r12)
#endif
	mtctr	r12
	bctrl

/*
 * On POWER9, we can come here on wakeup from a cpuidle stop state.
 * Hence restore the additional SPRs to the saved value.
 *
 * On POWER8, we come here only on winkle. Since winkle is used
 * only in the case of CPU-Hotplug, we don't need to restore
 * the additional SPRs.
 */
BEGIN_FTR_SECTION
	bl 	power9_restore_additional_sprs
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_300)
hypervisor_state_restored:

	mr	r12,r19
	mtlr	r17
	blr		/* return to pnv_powersave_wakeup */

fastsleep_workaround_at_exit:
	li	r3,1
	li	r4,0
	bl	opal_config_cpu_idle_state
	b	timebase_resync

/*
 * R3 here contains the value that will be returned to the caller
 * of power7_nap.
 * R12 contains SRR1 for CHECK_HMI_INTERRUPT.
 */
.global pnv_wakeup_loss
pnv_wakeup_loss:
	ld	r1,PACAR1(r13)
BEGIN_FTR_SECTION
	CHECK_HMI_INTERRUPT
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE)
	REST_NVGPRS(r1)
	REST_GPR(2, r1)
	ld	r4,PACAKMSR(r13)
	ld	r5,_LINK(r1)
	ld	r6,_CCR(r1)
	addi	r1,r1,INT_FRAME_SIZE
	mtlr	r5
	mtcr	r6
	mtmsrd	r4
	blr

/*
 * R3 here contains the value that will be returned to the caller
 * of power7_nap.
 * R12 contains SRR1 for CHECK_HMI_INTERRUPT.
 */
pnv_wakeup_noloss:
	lbz	r0,PACA_NAPSTATELOST(r13)
	cmpwi	r0,0
	bne	pnv_wakeup_loss
	ld	r1,PACAR1(r13)
BEGIN_FTR_SECTION
	CHECK_HMI_INTERRUPT
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE)
	ld	r4,PACAKMSR(r13)
	ld	r5,_NIP(r1)
	ld	r6,_CCR(r1)
	addi	r1,r1,INT_FRAME_SIZE
	mtlr	r5
	mtcr	r6
	mtmsrd	r4
	blr
