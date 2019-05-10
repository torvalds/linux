/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_ARC_ENTRY_ARCV2_H
#define __ASM_ARC_ENTRY_ARCV2_H

#include <asm/asm-offsets.h>
#include <asm/irqflags-arcv2.h>
#include <asm/thread_info.h>	/* For THREAD_SIZE */

/*
 * Interrupt/Exception stack layout (pt_regs) for ARCv2
 *   (End of struct aligned to end of page [unless nested])
 *
 *  INTERRUPT                          EXCEPTION
 *
 *    manual    ---------------------  manual
 *              |      orig_r0      |
 *              |      event/ECR    |
 *              |      bta          |
 *              |      user_r25     |
 *              |      gp           |
 *              |      fp           |
 *              |      sp           |
 *              |      r12          |
 *              |      r30          |
 *              |      r58          |
 *              |      r59          |
 *  hw autosave ---------------------
 *    optional  |      r0           |
 *              |      r1           |
 *              ~                   ~
 *              |      r9           |
 *              |      r10          |
 *              |      r11          |
 *              |      blink        |
 *              |      lpe          |
 *              |      lps          |
 *              |      lpc          |
 *              |      ei base      |
 *              |      ldi base     |
 *              |      jli base     |
 *              ---------------------
 *  hw autosave |       pc / eret   |
 *   mandatory  | stat32 / erstatus |
 *              ---------------------
 */

/*------------------------------------------------------------------------*/
.macro INTERRUPT_PROLOGUE	called_from
	; (A) Before jumping to Interrupt Vector, hardware micro-ops did following:
	;   1. SP auto-switched to kernel mode stack
	;   2. STATUS32.Z flag set if in U mode at time of interrupt (U:1,K:0)
	;   3. Auto save: (mandatory) Push PC and STAT32 on stack
	;                 hardware does even if CONFIG_ARC_IRQ_NO_AUTOSAVE
	;   4. Auto save: (optional) r0-r11, blink, LPE,LPS,LPC, JLI,LDI,EI
	;
	; (B) Manually saved some regs: r12,r25,r30, sp,fp,gp, ACCL pair

#ifdef CONFIG_ARC_IRQ_NO_AUTOSAVE
.ifnc \called_from, exception
	st.as	r9, [sp, -10]	; save r9 in it's final stack slot
	sub	sp, sp, 12	; skip JLI, LDI, EI

	PUSH	lp_count
	PUSHAX	lp_start
	PUSHAX	lp_end
	PUSH	blink

	PUSH	r11
	PUSH	r10

	sub	sp, sp, 4	; skip r9

	PUSH	r8
	PUSH	r7
	PUSH	r6
	PUSH	r5
	PUSH	r4
	PUSH	r3
	PUSH	r2
	PUSH	r1
	PUSH	r0
.endif
#endif

#ifdef CONFIG_ARC_HAS_ACCL_REGS
	PUSH	r59
	PUSH	r58
#endif

	PUSH	r30
	PUSH	r12

	; Saving pt_regs->sp correctly requires some extra work due to the way
	; Auto stack switch works
	;  - U mode: retrieve it from AUX_USER_SP
	;  - K mode: add the offset from current SP where H/w starts auto push
	;
	; 1. Utilize the fact that Z bit is set if Intr taken in U mode
	; 2. Upon entry SP is always saved (for any inspection, unwinding etc),
	;    but on return, restored only if U mode

	lr	r9, [AUX_USER_SP]			; U mode SP

	mov.nz	r9, sp
	add.nz	r9, r9, SZ_PT_REGS - PT_sp - 4		; K mode SP

	PUSH	r9					; SP (pt_regs->sp)

	PUSH	fp
	PUSH	gp

#ifdef CONFIG_ARC_CURR_IN_REG
	PUSH	r25			; user_r25
	GET_CURR_TASK_ON_CPU	r25
#else
	sub	sp, sp, 4
#endif

.ifnc \called_from, exception
	sub	sp, sp, 12	; BTA/ECR/orig_r0 placeholder per pt_regs
.endif

.endm

/*------------------------------------------------------------------------*/
.macro INTERRUPT_EPILOGUE	called_from

	; INPUT: r0 has STAT32 of calling context
	; INPUT: Z flag set if returning to K mode
.ifnc \called_from, exception
	add	sp, sp, 12	; skip BTA/ECR/orig_r0 placeholderss
.endif

#ifdef CONFIG_ARC_CURR_IN_REG
	POP	r25
#else
	add	sp, sp, 4
#endif

	POP	gp
	POP	fp

	; Restore SP (into AUX_USER_SP) only if returning to U mode
	;  - for K mode, it will be implicitly restored as stack is unwound
	;  - Z flag set on K is inverse of what hardware does on interrupt entry
	;    but that doesn't really matter
	bz	1f

	POPAX	AUX_USER_SP
1:
	POP	r12
	POP	r30

#ifdef CONFIG_ARC_HAS_ACCL_REGS
	POP	r58
	POP	r59
#endif

#ifdef CONFIG_ARC_IRQ_NO_AUTOSAVE
.ifnc \called_from, exception
	POP	r0
	POP	r1
	POP	r2
	POP	r3
	POP	r4
	POP	r5
	POP	r6
	POP	r7
	POP	r8
	POP	r9
	POP	r10
	POP	r11

	POP	blink
	POPAX	lp_end
	POPAX	lp_start

	POP	r9
	mov	lp_count, r9

	add	sp, sp, 12	; skip JLI, LDI, EI
	ld.as	r9, [sp, -10]	; reload r9 which got clobbered
.endif
#endif

.endm

/*------------------------------------------------------------------------*/
.macro EXCEPTION_PROLOGUE

	; (A) Before jumping to Exception Vector, hardware micro-ops did following:
	;   1. SP auto-switched to kernel mode stack
	;   2. STATUS32.Z flag set if in U mode at time of exception (U:1,K:0)
	;
	; (B) Manually save the complete reg file below

	PUSH	r9		; freeup a register: slot of erstatus

	PUSHAX	eret
	sub	sp, sp, 12	; skip JLI, LDI, EI
	PUSH	lp_count
	PUSHAX	lp_start
	PUSHAX	lp_end
	PUSH	blink

	PUSH	r11
	PUSH	r10

	ld.as	r9,  [sp, 10]	; load stashed r9 (status32 stack slot)
	lr	r10, [erstatus]
	st.as	r10, [sp, 10]	; save status32 at it's right stack slot

	PUSH	r9
	PUSH	r8
	PUSH	r7
	PUSH	r6
	PUSH	r5
	PUSH	r4
	PUSH	r3
	PUSH	r2
	PUSH	r1
	PUSH	r0

	; -- for interrupts, regs above are auto-saved by h/w in that order --
	; Now do what ISR prologue does (manually save r12, sp, fp, gp, r25)

	INTERRUPT_PROLOGUE  exception

	PUSHAX	erbta
	PUSHAX	ecr		; r9 contains ECR, expected by EV_Trap

	PUSH	r0		; orig_r0
	; OUTPUT: r9 has ECR
.endm

/*------------------------------------------------------------------------*/
.macro EXCEPTION_EPILOGUE

	; INPUT: r0 has STAT32 of calling context
	btst   r0, STATUS_U_BIT	; Z flag set if K, used in INTERRUPT_EPILOGUE

	add	sp, sp, 8	; orig_r0/ECR don't need restoring
	POPAX	erbta

	INTERRUPT_EPILOGUE  exception

	POP	r0
	POP	r1
	POP	r2
	POP	r3
	POP	r4
	POP	r5
	POP	r6
	POP	r7
	POP	r8
	POP	r9
	POP	r10
	POP	r11

	POP	blink
	POPAX	lp_end
	POPAX	lp_start

	POP	r9
	mov	lp_count, r9

	add	sp, sp, 12	; skip JLI, LDI, EI
	POPAX	eret
	POPAX	erstatus

	ld.as	r9, [sp, -12]	; reload r9 which got clobbered
.endm

.macro FAKE_RET_FROM_EXCPN
	lr      r9, [status32]
	bic     r9, r9, (STATUS_U_MASK|STATUS_DE_MASK|STATUS_AE_MASK)
	or      r9, r9, STATUS_IE_MASK
	kflag   r9
.endm

/* Get thread_info of "current" tsk */
.macro GET_CURR_THR_INFO_FROM_SP  reg
	bmskn \reg, sp, THREAD_SHIFT - 1
.endm

/* Get CPU-ID of this core */
.macro  GET_CPU_ID  reg
	lr  \reg, [identity]
	xbfu \reg, \reg, 0xE8	/* 00111    01000 */
				/* M = 8-1  N = 8 */
.endm

#endif
