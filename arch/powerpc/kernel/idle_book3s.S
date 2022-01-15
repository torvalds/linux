/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright 2018, IBM Corporation.
 *
 *  This file contains general idle entry/exit functions to save
 *  and restore stack and NVGPRs which allows C code to call idle
 *  states that lose GPRs, and it will return transparently with
 *  SRR1 wakeup reason return value.
 *
 *  The platform / CPU caller must ensure SPRs and any other non-GPR
 *  state is saved and restored correctly, handle KVM, interrupts, etc.
 */

#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ppc-opcode.h>
#include <asm/cpuidle.h>
#include <asm/thread_info.h> /* TLF_NAPPING */

#ifdef CONFIG_PPC_P7_NAP
/*
 * Desired PSSCR in r3
 *
 * No state will be lost regardless of wakeup mechanism (interrupt or NIA).
 *
 * An EC=0 type wakeup will return with a value of 0. SRESET wakeup (which can
 * happen with xscom SRESET and possibly MCE) may clobber volatiles except LR,
 * and must blr, to return to caller with r3 set according to caller's expected
 * return code (for Book3S/64 that is SRR1).
 */
_GLOBAL(isa300_idle_stop_noloss)
	mtspr 	SPRN_PSSCR,r3
	PPC_STOP
	li	r3,0
	blr

/*
 * Desired PSSCR in r3
 *
 * GPRs may be lost, so they are saved here. Wakeup is by interrupt only.
 * The SRESET wakeup returns to this function's caller by calling
 * idle_return_gpr_loss with r3 set to desired return value.
 *
 * A wakeup without GPR loss may alteratively be handled as in
 * isa300_idle_stop_noloss and blr directly, as an optimisation.
 *
 * The caller is responsible for saving/restoring SPRs, MSR, timebase,
 * etc.
 */
_GLOBAL(isa300_idle_stop_mayloss)
	mtspr 	SPRN_PSSCR,r3
	std	r1,PACAR1(r13)
	mflr	r4
	mfcr	r5
	/*
	 * Use the stack red zone rather than a new frame for saving regs since
	 * in the case of no GPR loss the wakeup code branches directly back to
	 * the caller without deallocating the stack frame first.
	 */
	std	r2,-8*1(r1)
	std	r14,-8*2(r1)
	std	r15,-8*3(r1)
	std	r16,-8*4(r1)
	std	r17,-8*5(r1)
	std	r18,-8*6(r1)
	std	r19,-8*7(r1)
	std	r20,-8*8(r1)
	std	r21,-8*9(r1)
	std	r22,-8*10(r1)
	std	r23,-8*11(r1)
	std	r24,-8*12(r1)
	std	r25,-8*13(r1)
	std	r26,-8*14(r1)
	std	r27,-8*15(r1)
	std	r28,-8*16(r1)
	std	r29,-8*17(r1)
	std	r30,-8*18(r1)
	std	r31,-8*19(r1)
	std	r4,-8*20(r1)
	std	r5,-8*21(r1)
	/* 168 bytes */
	PPC_STOP
	b	.	/* catch bugs */

/*
 * Desired return value in r3
 *
 * The idle wakeup SRESET interrupt can call this after calling
 * to return to the idle sleep function caller with r3 as the return code.
 *
 * This must not be used if idle was entered via a _noloss function (use
 * a simple blr instead).
 */
_GLOBAL(idle_return_gpr_loss)
	ld	r1,PACAR1(r13)
	ld	r4,-8*20(r1)
	ld	r5,-8*21(r1)
	mtlr	r4
	mtcr	r5
	/*
	 * KVM nap requires r2 to be saved, rather than just restoring it
	 * from PACATOC. This could be avoided for that less common case
	 * if KVM saved its r2.
	 */
	ld	r2,-8*1(r1)
	ld	r14,-8*2(r1)
	ld	r15,-8*3(r1)
	ld	r16,-8*4(r1)
	ld	r17,-8*5(r1)
	ld	r18,-8*6(r1)
	ld	r19,-8*7(r1)
	ld	r20,-8*8(r1)
	ld	r21,-8*9(r1)
	ld	r22,-8*10(r1)
	ld	r23,-8*11(r1)
	ld	r24,-8*12(r1)
	ld	r25,-8*13(r1)
	ld	r26,-8*14(r1)
	ld	r27,-8*15(r1)
	ld	r28,-8*16(r1)
	ld	r29,-8*17(r1)
	ld	r30,-8*18(r1)
	ld	r31,-8*19(r1)
	blr

/*
 * This is the sequence required to execute idle instructions, as
 * specified in ISA v2.07 (and earlier). MSR[IR] and MSR[DR] must be 0.
 * We have to store a GPR somewhere, ptesync, then reload it, and create
 * a false dependency on the result of the load. It doesn't matter which
 * GPR we store, or where we store it. We have already stored r2 to the
 * stack at -8(r1) in isa206_idle_insn_mayloss, so use that.
 */
#define IDLE_STATE_ENTER_SEQ_NORET(IDLE_INST)			\
	/* Magic NAP/SLEEP/WINKLE mode enter sequence */	\
	std	r2,-8(r1);					\
	ptesync;						\
	ld	r2,-8(r1);					\
236:	cmpd	cr0,r2,r2;					\
	bne	236b;						\
	IDLE_INST;						\
	b	.	/* catch bugs */

/*
 * Desired instruction type in r3
 *
 * GPRs may be lost, so they are saved here. Wakeup is by interrupt only.
 * The SRESET wakeup returns to this function's caller by calling
 * idle_return_gpr_loss with r3 set to desired return value.
 *
 * A wakeup without GPR loss may alteratively be handled as in
 * isa300_idle_stop_noloss and blr directly, as an optimisation.
 *
 * The caller is responsible for saving/restoring SPRs, MSR, timebase,
 * etc.
 *
 * This must be called in real-mode (MSR_IDLE).
 */
_GLOBAL(isa206_idle_insn_mayloss)
	std	r1,PACAR1(r13)
	mflr	r4
	mfcr	r5
	/*
	 * Use the stack red zone rather than a new frame for saving regs since
	 * in the case of no GPR loss the wakeup code branches directly back to
	 * the caller without deallocating the stack frame first.
	 */
	std	r2,-8*1(r1)
	std	r14,-8*2(r1)
	std	r15,-8*3(r1)
	std	r16,-8*4(r1)
	std	r17,-8*5(r1)
	std	r18,-8*6(r1)
	std	r19,-8*7(r1)
	std	r20,-8*8(r1)
	std	r21,-8*9(r1)
	std	r22,-8*10(r1)
	std	r23,-8*11(r1)
	std	r24,-8*12(r1)
	std	r25,-8*13(r1)
	std	r26,-8*14(r1)
	std	r27,-8*15(r1)
	std	r28,-8*16(r1)
	std	r29,-8*17(r1)
	std	r30,-8*18(r1)
	std	r31,-8*19(r1)
	std	r4,-8*20(r1)
	std	r5,-8*21(r1)
	cmpwi	r3,PNV_THREAD_NAP
	bne	1f
	IDLE_STATE_ENTER_SEQ_NORET(PPC_NAP)
1:	cmpwi	r3,PNV_THREAD_SLEEP
	bne	2f
	IDLE_STATE_ENTER_SEQ_NORET(PPC_SLEEP)
2:	IDLE_STATE_ENTER_SEQ_NORET(PPC_WINKLE)
#endif

#ifdef CONFIG_PPC_970_NAP
_GLOBAL(power4_idle_nap)
	LOAD_REG_IMMEDIATE(r7, MSR_KERNEL|MSR_EE|MSR_POW)
	ld	r9,PACA_THREAD_INFO(r13)
	ld	r8,TI_LOCAL_FLAGS(r9)
	ori	r8,r8,_TLF_NAPPING
	std	r8,TI_LOCAL_FLAGS(r9)
	/*
	 * NAPPING bit is set, from this point onward power4_fixup_nap
	 * will cause exceptions to return to power4_idle_nap_return.
	 */
1:	sync
	isync
	mtmsrd	r7
	isync
	b	1b

	.globl power4_idle_nap_return
power4_idle_nap_return:
	blr
#endif
