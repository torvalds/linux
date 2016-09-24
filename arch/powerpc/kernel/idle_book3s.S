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
#include <asm/book3s/64/mmu-hash.h>
#include <asm/mmu.h>

#undef DEBUG

/*
 * Use unused space in the interrupt stack to save and restore
 * registers for winkle support.
 */
#define _SDR1	GPR3
#define _RPR	GPR4
#define _SPURR	GPR5
#define _PURR	GPR6
#define _TSCR	GPR7
#define _DSCR	GPR8
#define _AMOR	GPR9
#define _WORT	GPR10
#define _WORC	GPR11
#define _PTCR	GPR12

#define PSSCR_HV_TEMPLATE	PSSCR_ESL | PSSCR_EC | \
				PSSCR_PSLL_MASK | PSSCR_TR_MASK | \
				PSSCR_MTL_MASK

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
	mfspr	r3,SPRN_PTCR
	std	r3,_PTCR(r1)
	/*
	 * Note - SDR1 is dropped in Power ISA v3. Hence not restoring
	 * SDR1 here
	 */
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

	blr

/*
 * Used by threads when the lock bit of core_idle_state is set.
 * Threads will spin in HMT_LOW until the lock bit is cleared.
 * r14 - pointer to core_idle_state
 * r15 - used to load contents of core_idle_state
 */

core_idle_lock_held:
	HMT_LOW
3:	lwz	r15,0(r14)
	andi.   r15,r15,PNV_CORE_IDLE_LOCK_BIT
	bne	3b
	HMT_MEDIUM
	lwarx	r15,0,r14
	blr

/*
 * Pass requested state in r3:
 *	r3 - PNV_THREAD_NAP/SLEEP/WINKLE in POWER8
 *	   - Requested STOP state in POWER9
 *
 * To check IRQ_HAPPENED in r4
 * 	0 - don't check
 * 	1 - check
 *
 * Address to 'rfid' to in r5
 */
_GLOBAL(pnv_powersave_common)
	/* Use r3 to pass state nap/sleep/winkle */
	/* NAP is a state loss, we create a regs frame on the
	 * stack, fill it up with the state we care about and
	 * stick a pointer to it in PACAR1. We really only
	 * need to save PC, some CR bits and the NV GPRs,
	 * but for now an interrupt frame will do.
	 */
	mflr	r0
	std	r0,16(r1)
	stdu	r1,-INT_FRAME_SIZE(r1)
	std	r0,_LINK(r1)
	std	r0,_NIP(r1)

	/* Hard disable interrupts */
	mfmsr	r9
	rldicl	r9,r9,48,1
	rotldi	r9,r9,16
	mtmsrd	r9,1			/* hard-disable interrupts */

	/* Check if something happened while soft-disabled */
	lbz	r0,PACAIRQHAPPENED(r13)
	andi.	r0,r0,~PACA_IRQ_HARD_DIS@l
	beq	1f
	cmpwi	cr0,r4,0
	beq	1f
	addi	r1,r1,INT_FRAME_SIZE
	ld	r0,16(r1)
	li	r3,0			/* Return 0 (no nap) */
	mtlr	r0
	blr

1:	/* We mark irqs hard disabled as this is the state we'll
	 * be in when returning and we need to tell arch_local_irq_restore()
	 * about it
	 */
	li	r0,PACA_IRQ_HARD_DIS
	stb	r0,PACAIRQHAPPENED(r13)

	/* We haven't lost state ... yet */
	li	r0,0
	stb	r0,PACA_NAPSTATELOST(r13)

	/* Continue saving state */
	SAVE_GPR(2, r1)
	SAVE_NVGPRS(r1)
	mfcr	r4
	std	r4,_CCR(r1)
	std	r9,_MSR(r1)
	std	r1,PACAR1(r13)

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	/* Tell KVM we're entering idle */
	li	r4,KVM_HWTHREAD_IN_IDLE
	stb	r4,HSTATE_HWTHREAD_STATE(r13)
#endif

	/*
	 * Go to real mode to do the nap, as required by the architecture.
	 * Also, we need to be in real mode before setting hwthread_state,
	 * because as soon as we do that, another thread can switch
	 * the MMU context to the guest.
	 */
	LOAD_REG_IMMEDIATE(r7, MSR_IDLE)
	li	r6, MSR_RI
	andc	r6, r9, r6
	mtmsrd	r6, 1		/* clear RI before setting SRR0/1 */
	mtspr	SPRN_SRR0, r5
	mtspr	SPRN_SRR1, r7
	rfid

	.globl pnv_enter_arch207_idle_mode
pnv_enter_arch207_idle_mode:
	stb	r3,PACA_THREAD_IDLE_STATE(r13)
	cmpwi	cr3,r3,PNV_THREAD_SLEEP
	bge	cr3,2f
	IDLE_STATE_ENTER_SEQ(PPC_NAP)
	/* No return */
2:
	/* Sleep or winkle */
	lbz	r7,PACA_THREAD_MASK(r13)
	ld	r14,PACA_CORE_IDLE_STATE_PTR(r13)
lwarx_loop1:
	lwarx	r15,0,r14

	andi.   r9,r15,PNV_CORE_IDLE_LOCK_BIT
	bnel	core_idle_lock_held

	andc	r15,r15,r7			/* Clear thread bit */

	andi.	r15,r15,PNV_CORE_IDLE_THREAD_BITS

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
	IDLE_STATE_ENTER_SEQ(PPC_SLEEP)

fastsleep_workaround_at_entry:
	ori	r15,r15,PNV_CORE_IDLE_LOCK_BIT
	stwcx.	r15,0,r14
	bne-	lwarx_loop1
	isync

	/* Fast sleep workaround */
	li	r3,1
	li	r4,1
	bl	opal_rm_config_cpu_idle_state

	/* Clear Lock bit */
	li	r0,0
	lwsync
	stw	r0,0(r14)
	b	common_enter

enter_winkle:
	bl	save_sprs_to_stack

	IDLE_STATE_ENTER_SEQ(PPC_WINKLE)

/*
 * r3 - requested stop state
 */
power_enter_stop:
/*
 * Check if the requested state is a deep idle state.
 */
	LOAD_REG_ADDRBASE(r5,pnv_first_deep_stop_state)
	ld	r4,ADDROFF(pnv_first_deep_stop_state)(r5)
	cmpd	r3,r4
	bge	2f
	IDLE_STATE_ENTER_SEQ(PPC_STOP)
2:
/*
 * Entering deep idle state.
 * Clear thread bit in PACA_CORE_IDLE_STATE, save SPRs to
 * stack and enter stop
 */
	lbz     r7,PACA_THREAD_MASK(r13)
	ld      r14,PACA_CORE_IDLE_STATE_PTR(r13)

lwarx_loop_stop:
	lwarx   r15,0,r14
	andi.   r9,r15,PNV_CORE_IDLE_LOCK_BIT
	bnel    core_idle_lock_held
	andc    r15,r15,r7                      /* Clear thread bit */

	stwcx.  r15,0,r14
	bne-    lwarx_loop_stop
	isync

	bl	save_sprs_to_stack

	IDLE_STATE_ENTER_SEQ(PPC_STOP)

_GLOBAL(power7_idle)
	/* Now check if user or arch enabled NAP mode */
	LOAD_REG_ADDRBASE(r3,powersave_nap)
	lwz	r4,ADDROFF(powersave_nap)(r3)
	cmpwi	0,r4,0
	beqlr
	li	r3, 1
	/* fall through */

_GLOBAL(power7_nap)
	mr	r4,r3
	li	r3,PNV_THREAD_NAP
	LOAD_REG_ADDR(r5, pnv_enter_arch207_idle_mode)
	b	pnv_powersave_common
	/* No return */

_GLOBAL(power7_sleep)
	li	r3,PNV_THREAD_SLEEP
	li	r4,1
	LOAD_REG_ADDR(r5, pnv_enter_arch207_idle_mode)
	b	pnv_powersave_common
	/* No return */

_GLOBAL(power7_winkle)
	li	r3,PNV_THREAD_WINKLE
	li	r4,1
	LOAD_REG_ADDR(r5, pnv_enter_arch207_idle_mode)
	b	pnv_powersave_common
	/* No return */

#define CHECK_HMI_INTERRUPT						\
	mfspr	r0,SPRN_SRR1;						\
BEGIN_FTR_SECTION_NESTED(66);						\
	rlwinm	r0,r0,45-31,0xf;  /* extract wake reason field (P8) */	\
FTR_SECTION_ELSE_NESTED(66);						\
	rlwinm	r0,r0,45-31,0xe;  /* P7 wake reason field is 3 bits */	\
ALT_FTR_SECTION_END_NESTED_IFSET(CPU_FTR_ARCH_207S, 66);		\
	cmpwi	r0,0xa;			/* Hypervisor maintenance ? */	\
	bne	20f;							\
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
 * r3 - requested stop state
 */
_GLOBAL(power9_idle_stop)
	LOAD_REG_IMMEDIATE(r4, PSSCR_HV_TEMPLATE)
	or	r4,r4,r3
	mtspr	SPRN_PSSCR, r4
	li	r4, 1
	LOAD_REG_ADDR(r5,power_enter_stop)
	b	pnv_powersave_common
	/* No return */
/*
 * Called from reset vector. Check whether we have woken up with
 * hypervisor state loss. If yes, restore hypervisor state and return
 * back to reset vector.
 *
 * r13 - Contents of HSPRG0
 * cr3 - set to gt if waking up with partial/complete hypervisor state loss
 */
_GLOBAL(pnv_restore_hyp_resource)
BEGIN_FTR_SECTION
	ld	r2,PACATOC(r13);
	/*
	 * POWER ISA 3. Use PSSCR to determine if we
	 * are waking up from deep idle state
	 */
	LOAD_REG_ADDRBASE(r5,pnv_first_deep_stop_state)
	ld	r4,ADDROFF(pnv_first_deep_stop_state)(r5)

	mfspr	r5,SPRN_PSSCR
	/*
	 * 0-3 bits correspond to Power-Saving Level Status
	 * which indicates the idle state we are waking up from
	 */
	rldicl  r5,r5,4,60
	cmpd	cr4,r5,r4
	bge	cr4,pnv_wakeup_tb_loss
	/*
	 * Waking up without hypervisor state loss. Return to
	 * reset vector
	 */
	blr

END_FTR_SECTION_IFSET(CPU_FTR_ARCH_300)

	/*
	 * POWER ISA 2.07 or less.
	 * Check if last bit of HSPGR0 is set. This indicates whether we are
	 * waking up from winkle.
	 */
	clrldi	r5,r13,63
	clrrdi	r13,r13,1

	/* Now that we are sure r13 is corrected, load TOC */
	ld	r2,PACATOC(r13);
	cmpwi	cr4,r5,1
	mtspr	SPRN_HSPRG0,r13

	lbz	r0,PACA_THREAD_IDLE_STATE(r13)
	cmpwi   cr2,r0,PNV_THREAD_NAP
	bgt     cr2,pnv_wakeup_tb_loss	/* Either sleep or Winkle */

	/*
	 * We fall through here if PACA_THREAD_IDLE_STATE shows we are waking
	 * up from nap. At this stage CR3 shouldn't contains 'gt' since that
	 * indicates we are waking with hypervisor state loss from nap.
	 */
	bgt	cr3,.

	blr	/* Return back to System Reset vector from where
		   pnv_restore_hyp_resource was invoked */

/*
 * Called if waking up from idle state which can cause either partial or
 * complete hyp state loss.
 * In POWER8, called if waking up from fastsleep or winkle
 * In POWER9, called if waking up from stop state >= pnv_first_deep_stop_state
 *
 * r13 - PACA
 * cr3 - gt if waking up with partial/complete hypervisor state loss
 * cr4 - gt or eq if waking up from complete hypervisor state loss.
 */
_GLOBAL(pnv_wakeup_tb_loss)
	ld	r1,PACAR1(r13)
	/*
	 * Before entering any idle state, the NVGPRs are saved in the stack
	 * and they are restored before switching to the process context. Hence
	 * until they are restored, they are free to be used.
	 *
	 * Save SRR1 and LR in NVGPRs as they might be clobbered in
	 * opal_call() (called in CHECK_HMI_INTERRUPT). SRR1 is required
	 * to determine the wakeup reason if we branch to kvm_start_guest. LR
	 * is required to return back to reset vector after hypervisor state
	 * restore is complete.
	 */
	mflr	r17
	mfspr	r16,SPRN_SRR1
BEGIN_FTR_SECTION
	CHECK_HMI_INTERRUPT
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE)

	lbz	r7,PACA_THREAD_MASK(r13)
	ld	r14,PACA_CORE_IDLE_STATE_PTR(r13)
lwarx_loop2:
	lwarx	r15,0,r14
	andi.	r9,r15,PNV_CORE_IDLE_LOCK_BIT
	/*
	 * Lock bit is set in one of the 2 cases-
	 * a. In the sleep/winkle enter path, the last thread is executing
	 * fastsleep workaround code.
	 * b. In the wake up path, another thread is executing fastsleep
	 * workaround undo code or resyncing timebase or restoring context
	 * In either case loop until the lock bit is cleared.
	 */
	bnel	core_idle_lock_held

	cmpwi	cr2,r15,0

	/*
	 * At this stage
	 * cr2 - eq if first thread to wakeup in core
	 * cr3-  gt if waking up with partial/complete hypervisor state loss
	 * cr4 - gt or eq if waking up from complete hypervisor state loss.
	 */

	ori	r15,r15,PNV_CORE_IDLE_LOCK_BIT
	stwcx.	r15,0,r14
	bne-	lwarx_loop2
	isync

BEGIN_FTR_SECTION
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
	ble	cr3,clear_lock
	/* Time base re-sync */
	bl	opal_rm_resync_timebase;
	/*
	 * If waking up from sleep, per core state is not lost, skip to
	 * clear_lock.
	 */
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
END_FTR_SECTION_IFSET(CPU_FTR_ARCH_300)

	ld	r4,_TSCR(r1)
	mtspr	SPRN_TSCR,r4
	ld	r4,_WORC(r1)
	mtspr	SPRN_WORC,r4

clear_lock:
	andi.	r15,r15,PNV_CORE_IDLE_THREAD_BITS
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

hypervisor_state_restored:

	mtspr	SPRN_SRR1,r16
	mtlr	r17
	blr	/* Return back to System Reset vector from where
		   pnv_restore_hyp_resource was invoked */

fastsleep_workaround_at_exit:
	li	r3,1
	li	r4,0
	bl	opal_rm_config_cpu_idle_state
	b	timebase_resync

/*
 * R3 here contains the value that will be returned to the caller
 * of power7_nap.
 */
_GLOBAL(pnv_wakeup_loss)
	ld	r1,PACAR1(r13)
BEGIN_FTR_SECTION
	CHECK_HMI_INTERRUPT
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE)
	REST_NVGPRS(r1)
	REST_GPR(2, r1)
	ld	r6,_CCR(r1)
	ld	r4,_MSR(r1)
	ld	r5,_NIP(r1)
	addi	r1,r1,INT_FRAME_SIZE
	mtcr	r6
	mtspr	SPRN_SRR1,r4
	mtspr	SPRN_SRR0,r5
	rfid

/*
 * R3 here contains the value that will be returned to the caller
 * of power7_nap.
 */
_GLOBAL(pnv_wakeup_noloss)
	lbz	r0,PACA_NAPSTATELOST(r13)
	cmpwi	r0,0
	bne	pnv_wakeup_loss
BEGIN_FTR_SECTION
	CHECK_HMI_INTERRUPT
END_FTR_SECTION_IFSET(CPU_FTR_HVMODE)
	ld	r1,PACAR1(r13)
	ld	r6,_CCR(r1)
	ld	r4,_MSR(r1)
	ld	r5,_NIP(r1)
	addi	r1,r1,INT_FRAME_SIZE
	mtcr	r6
	mtspr	SPRN_SRR1,r4
	mtspr	SPRN_SRR0,r5
	rfid
