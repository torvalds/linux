/*
 *  This file contains the power_save function for Power7 CPUs.
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
#include <asm/mmu-hash64.h>

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

/* Idle state entry routines */

#define	IDLE_STATE_ENTER_SEQ(IDLE_INST)				\
	/* Magic NAP/SLEEP/WINKLE mode enter sequence */	\
	std	r0,0(r1);					\
	ptesync;						\
	ld	r0,0(r1);					\
1:	cmp	cr0,r0,r0;					\
	bne	1b;						\
	IDLE_INST;						\
	b	.

	.text

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
 *	r3 - PNV_THREAD_NAP/SLEEP/WINKLE
 *
 * To check IRQ_HAPPENED in r4
 * 	0 - don't check
 * 	1 - check
 */
_GLOBAL(power7_powersave_common)
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

#ifndef CONFIG_SMP
	/* Make sure FPU, VSX etc... are flushed as we may lose
	 * state when going to nap mode
	 */
	bl	discard_lazy_cpu_state
#endif /* CONFIG_SMP */

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

	/*
	 * Go to real mode to do the nap, as required by the architecture.
	 * Also, we need to be in real mode before setting hwthread_state,
	 * because as soon as we do that, another thread can switch
	 * the MMU context to the guest.
	 */
	LOAD_REG_IMMEDIATE(r5, MSR_IDLE)
	li	r6, MSR_RI
	andc	r6, r9, r6
	LOAD_REG_ADDR(r7, power7_enter_nap_mode)
	mtmsrd	r6, 1		/* clear RI before setting SRR0/1 */
	mtspr	SPRN_SRR0, r7
	mtspr	SPRN_SRR1, r5
	rfid

	.globl	power7_enter_nap_mode
power7_enter_nap_mode:
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	/* Tell KVM we're napping */
	li	r4,KVM_HWTHREAD_IN_NAP
	stb	r4,HSTATE_HWTHREAD_STATE(r13)
#endif
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
	li	r0,OPAL_CONFIG_CPU_IDLE_STATE
	bl	opal_call_realmode

	/* Clear Lock bit */
	li	r0,0
	lwsync
	stw	r0,0(r14)
	b	common_enter

enter_winkle:
	/*
	 * Note all register i.e per-core, per-subcore or per-thread is saved
	 * here since any thread in the core might wake up first
	 */
	mfspr	r3,SPRN_SDR1
	std	r3,_SDR1(r1)
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
	IDLE_STATE_ENTER_SEQ(PPC_WINKLE)

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
	b	power7_powersave_common
	/* No return */

_GLOBAL(power7_sleep)
	li	r3,PNV_THREAD_SLEEP
	li	r4,1
	b	power7_powersave_common
	/* No return */

_GLOBAL(power7_winkle)
	li	r3,3
	li	r4,1
	b	power7_powersave_common
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
	li	r0,OPAL_HANDLE_HMI;	/* Pass opal token argument*/	\
	bl	opal_call_realmode;					\
	ld	r3,ORIG_GPR3(r1);	/* Restore original r3 */	\
20:	nop;


_GLOBAL(power7_wakeup_tb_loss)
	ld	r2,PACATOC(r13);
	ld	r1,PACAR1(r13)
	/*
	 * Before entering any idle state, the NVGPRs are saved in the stack
	 * and they are restored before switching to the process context. Hence
	 * until they are restored, they are free to be used.
	 *
	 * Save SRR1 in a NVGPR as it might be clobbered in opal_call_realmode
	 * (called in CHECK_HMI_INTERRUPT). SRR1 is required to determine the
	 * wakeup reason if we branch to kvm_start_guest.
	 */

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
	lbz	r4,PACA_SUBCORE_SIBLING_MASK(r13)
	and	r4,r4,r15
	cmpwi	cr1,r4,0	/* Check if first in subcore */

	/*
	 * At this stage
	 * cr1 - 0b0100 if first thread to wakeup in subcore
	 * cr2 - 0b0100 if first thread to wakeup in core
	 * cr3-  0b0010 if waking up from sleep or winkle
	 * cr4 - 0b0100 if waking up from winkle
	 */

	or	r15,r15,r7		/* Set thread bit */

	beq	cr1,first_thread_in_subcore

	/* Not first thread in subcore to wake up */
	stwcx.	r15,0,r14
	bne-	lwarx_loop2
	isync
	b	common_exit

first_thread_in_subcore:
	/* First thread in subcore to wakeup */
	ori	r15,r15,PNV_CORE_IDLE_LOCK_BIT
	stwcx.	r15,0,r14
	bne-	lwarx_loop2
	isync

	/*
	 * If waking up from sleep, subcore state is not lost. Hence
	 * skip subcore state restore
	 */
	bne	cr4,subcore_state_restored

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
	 * First thread in the core waking up from fastsleep. It needs to
	 * call the fastsleep workaround code if the platform requires it.
	 * Call it unconditionally here. The below branch instruction will
	 * be patched out when the idle states are discovered if platform
	 * does not require workaround.
	 */
.global pnv_fastsleep_workaround_at_exit
pnv_fastsleep_workaround_at_exit:
	b	fastsleep_workaround_at_exit

timebase_resync:
	/* Do timebase resync if we are waking up from sleep. Use cr3 value
	 * set in exceptions-64s.S */
	ble	cr3,clear_lock
	/* Time base re-sync */
	li	r0,OPAL_RESYNC_TIMEBASE
	bl	opal_call_realmode;
	/* TODO: Check r3 for failure */

	/*
	 * If waking up from sleep, per core state is not lost, skip to
	 * clear_lock.
	 */
	bne	cr4,clear_lock

	/* Restore per core state */
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
	bne	cr4,hypervisor_state_restored

	/* Waking up from winkle */

	/* Restore per thread state */
	bl	__restore_cpu_power8

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

	ld	r4,_SPURR(r1)
	mtspr	SPRN_SPURR,r4
	ld	r4,_PURR(r1)
	mtspr	SPRN_PURR,r4
	ld	r4,_DSCR(r1)
	mtspr	SPRN_DSCR,r4
	ld	r4,_WORT(r1)
	mtspr	SPRN_WORT,r4

hypervisor_state_restored:

	li	r5,PNV_THREAD_RUNNING
	stb     r5,PACA_THREAD_IDLE_STATE(r13)

	mtspr	SPRN_SRR1,r16
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	li      r0,KVM_HWTHREAD_IN_KERNEL
	stb     r0,HSTATE_HWTHREAD_STATE(r13)
	/* Order setting hwthread_state vs. testing hwthread_req */
	sync
	lbz     r0,HSTATE_HWTHREAD_REQ(r13)
	cmpwi   r0,0
	beq     6f
	b       kvm_start_guest
6:
#endif

	REST_NVGPRS(r1)
	REST_GPR(2, r1)
	ld	r3,_CCR(r1)
	ld	r4,_MSR(r1)
	ld	r5,_NIP(r1)
	addi	r1,r1,INT_FRAME_SIZE
	mtcr	r3
	mfspr	r3,SPRN_SRR1		/* Return SRR1 */
	mtspr	SPRN_SRR1,r4
	mtspr	SPRN_SRR0,r5
	rfid

fastsleep_workaround_at_exit:
	li	r3,1
	li	r4,0
	li	r0,OPAL_CONFIG_CPU_IDLE_STATE
	bl	opal_call_realmode
	b	timebase_resync

/*
 * R3 here contains the value that will be returned to the caller
 * of power7_nap.
 */
_GLOBAL(power7_wakeup_loss)
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
_GLOBAL(power7_wakeup_noloss)
	lbz	r0,PACA_NAPSTATELOST(r13)
	cmpwi	r0,0
	bne	power7_wakeup_loss
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
