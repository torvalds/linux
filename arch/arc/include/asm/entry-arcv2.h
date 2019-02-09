
#ifndef __ASM_ARC_ENTRY_ARCV2_H
#define __ASM_ARC_ENTRY_ARCV2_H

#include <asm/asm-offsets.h>
#include <asm/irqflags-arcv2.h>
#include <asm/thread_info.h>	/* For THREAD_SIZE */

/*------------------------------------------------------------------------*/
.macro INTERRUPT_PROLOGUE	called_from

	; Before jumping to Interrupt Vector, hardware micro-ops did following:
	;   1. SP auto-switched to kernel mode stack
	;   2. STATUS32.Z flag set to U mode at time of interrupt (U:1, K:0)
	;   3. Auto saved: r0-r11, blink, LPE,LPS,LPC, JLI,LDI,EI, PC, STAT32
	;
	; Now manually save: r12, sp, fp, gp, r25

	PUSH	r12

	; Saving pt_regs->sp correctly requires some extra work due to the way
	; Auto stack switch works
	;  - U mode: retrieve it from AUX_USER_SP
	;  - K mode: add the offset from current SP where H/w starts auto push
	;
	; Utilize the fact that Z bit is set if Intr taken in U mode
	mov.nz	r9, sp
	add.nz	r9, r9, SZ_PT_REGS - PT_sp - 4
	bnz	1f

	lr	r9, [AUX_USER_SP]
1:
	PUSH	r9	; SP

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

	; Don't touch AUX_USER_SP if returning to K mode (Z bit set)
	; (Z bit set on K mode is inverse of INTERRUPT_PROLOGUE)
	add.z	sp, sp, 4
	bz	1f

	POPAX	AUX_USER_SP
1:
	POP	r12

.endm

/*------------------------------------------------------------------------*/
.macro EXCEPTION_PROLOGUE

	; Before jumping to Exception Vector, hardware micro-ops did following:
	;   1. SP auto-switched to kernel mode stack
	;   2. STATUS32.Z flag set to U mode at time of interrupt (U:1,K:0)
	;
	; Now manually save the complete reg file

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
	;
	; Set Z flag if this was from U mode (expected by INTERRUPT_PROLOGUE)
	; Although H/w exception micro-ops do set Z flag for U mode (just like
	; for interrupts), it could get clobbered in case we soft land here from
	; a TLB Miss exception handler (tlbex.S)

	and	r10, r10, STATUS_U_MASK
	xor.f	0, r10, STATUS_U_MASK

	INTERRUPT_PROLOGUE  exception

	PUSHAX	erbta
	PUSHAX	ecr		; r9 contains ECR, expected by EV_Trap

	PUSH	r0		; orig_r0
.endm

/*------------------------------------------------------------------------*/
.macro EXCEPTION_EPILOGUE

	; Assumes r0 has PT_status32
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
	or      r9, r9, (STATUS_L_MASK|STATUS_IE_MASK)
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
