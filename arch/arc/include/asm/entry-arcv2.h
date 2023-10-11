/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_ARC_ENTRY_ARCV2_H
#define __ASM_ARC_ENTRY_ARCV2_H

#include <asm/asm-offsets.h>
#include <asm/dsp-impl.h>
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
.macro INTERRUPT_PROLOGUE

	; Before jumping to Interrupt Vector, hardware micro-ops did following:
	;   1. SP auto-switched to kernel mode stack
	;   2. STATUS32.Z flag set if in U mode at time of interrupt (U:1,K:0)
	;   3. Auto save: (mandatory) Push PC and STAT32 on stack
	;                 hardware does even if CONFIG_ARC_IRQ_NO_AUTOSAVE
	;  4a. Auto save: (optional) r0-r11, blink, LPE,LPS,LPC, JLI,LDI,EI
	;
	; Now
	;  4b. If Auto-save (optional) not enabled in hw, manually save them
	;   5. Manually save: r12,r30, sp,fp,gp, ACCL pair
	;
	; At the end, SP points to pt_regs

#ifdef CONFIG_ARC_IRQ_NO_AUTOSAVE
	; carve pt_regs on stack (case #3), PC/STAT32 already on stack
	sub	sp, sp, SZ_PT_REGS - 8

	__SAVE_REGFILE_HARD
#else
	; carve pt_regs on stack (case #4), which grew partially already
	sub	sp, sp, PT_r0
#endif

	__SAVE_REGFILE_SOFT
.endm

/*------------------------------------------------------------------------*/
.macro EXCEPTION_PROLOGUE_KEEP_AE

	; Before jumping to Exception Vector, hardware micro-ops did following:
	;   1. SP auto-switched to kernel mode stack
	;   2. STATUS32.Z flag set if in U mode at time of exception (U:1,K:0)
	;
	; Now manually save rest of reg file
	; At the end, SP points to pt_regs

	sub	sp, sp, SZ_PT_REGS	; carve space for pt_regs

	; _HARD saves r10 clobbered by _SOFT as scratch hence comes first

	__SAVE_REGFILE_HARD
	__SAVE_REGFILE_SOFT

	st	r0, [sp]	; orig_r0

	lr	r10, [eret]
	lr	r11, [erstatus]
	ST2	r10, r11, PT_ret

	lr	r10, [ecr]
	lr	r11, [erbta]
	ST2	r10, r11, PT_event

	; OUTPUT: r10 has ECR expected by EV_Trap
.endm

.macro EXCEPTION_PROLOGUE

	EXCEPTION_PROLOGUE_KEEP_AE	; return ECR in r10

	lr  r0, [efa]
	mov r1, sp

	FAKE_RET_FROM_EXCPN		; clobbers r9
.endm

/*------------------------------------------------------------------------
 * This macro saves the registers manually which would normally be autosaved
 * by hardware on taken interrupts. It is used by
 *   - exception handlers (which don't have autosave)
 *   - interrupt autosave disabled due to CONFIG_ARC_IRQ_NO_AUTOSAVE
 */
.macro __SAVE_REGFILE_HARD

	ST2	r0,  r1,  PT_r0
	ST2	r2,  r3,  PT_r2
	ST2	r4,  r5,  PT_r4
	ST2	r6,  r7,  PT_r6
	ST2	r8,  r9,  PT_r8
	ST2	r10, r11, PT_r10

	st	blink, [sp, PT_blink]

	lr	r10, [lp_end]
	lr	r11, [lp_start]
	ST2	r10, r11, PT_lpe

	st	lp_count, [sp, PT_lpc]

	; skip JLI, LDI, EI for now
.endm

/*------------------------------------------------------------------------
 * This macros saves a bunch of other registers which can't be autosaved for
 * various reasons:
 *   - r12: the last caller saved scratch reg since hardware saves in pairs so r0-r11
 *   - r30: free reg, used by gcc as scratch
 *   - ACCL/ACCH pair when they exist
 */
.macro __SAVE_REGFILE_SOFT

	st	fp,  [sp, PT_fp]	; r27
	st	r30, [sp, PT_r30]
	st	r12, [sp, PT_r12]
	st	r26, [sp, PT_r26]	; gp

	; Saving pt_regs->sp correctly requires some extra work due to the way
	; Auto stack switch works
	;  - U mode: retrieve it from AUX_USER_SP
	;  - K mode: add the offset from current SP where H/w starts auto push
	;
	; 1. Utilize the fact that Z bit is set if Intr taken in U mode
	; 2. Upon entry SP is always saved (for any inspection, unwinding etc),
	;    but on return, restored only if U mode

	lr	r10, [AUX_USER_SP]	; U mode SP

	; ISA requires ADD.nz to have same dest and src reg operands
	mov.nz	r10, sp
	add2.nz	r10, r10, SZ_PT_REGS/4	; K mode SP

	st	r10, [sp, PT_sp]	; SP (pt_regs->sp)

#ifdef CONFIG_ARC_HAS_ACCL_REGS
	ST2	r58, r59, PT_r58
#endif

	/* clobbers r10, r11 registers pair */
	DSP_SAVE_REGFILE_IRQ

#ifdef CONFIG_ARC_CURR_IN_REG
	GET_CURR_TASK_ON_CPU	gp
#endif

.endm

/*------------------------------------------------------------------------*/
.macro __RESTORE_REGFILE_SOFT

	ld	fp,  [sp, PT_fp]
	ld	r30, [sp, PT_r30]
	ld	r12, [sp, PT_r12]
	ld	r26, [sp, PT_r26]

	; Restore SP (into AUX_USER_SP) only if returning to U mode
	;  - for K mode, it will be implicitly restored as stack is unwound
	;  - Z flag set on K is inverse of what hardware does on interrupt entry
	;    but that doesn't really matter
	bz	1f

	ld	r10, [sp, PT_sp]	; SP (pt_regs->sp)
	sr	r10, [AUX_USER_SP]
1:

	/* clobbers r10, r11 registers pair */
	DSP_RESTORE_REGFILE_IRQ

#ifdef CONFIG_ARC_HAS_ACCL_REGS
	LD2	r58, r59, PT_r58
#endif
.endm

/*------------------------------------------------------------------------*/
.macro __RESTORE_REGFILE_HARD

	ld	blink, [sp, PT_blink]

	LD2	r10, r11, PT_lpe
	sr	r10, [lp_end]
	sr	r11, [lp_start]

	ld	r10, [sp, PT_lpc]	; lp_count can't be target of LD
	mov	lp_count, r10

	LD2	r0,  r1,  PT_r0
	LD2	r2,  r3,  PT_r2
	LD2	r4,  r5,  PT_r4
	LD2	r6,  r7,  PT_r6
	LD2	r8,  r9,  PT_r8
	LD2	r10, r11, PT_r10
.endm


/*------------------------------------------------------------------------*/
.macro INTERRUPT_EPILOGUE

	; INPUT: r0 has STAT32 of calling context
	; INPUT: Z flag set if returning to K mode

	; _SOFT clobbers r10 restored by _HARD hence the order

	__RESTORE_REGFILE_SOFT

#ifdef CONFIG_ARC_IRQ_NO_AUTOSAVE
	__RESTORE_REGFILE_HARD

	; SP points to PC/STAT32: hw restores them despite NO_AUTOSAVE
	add	sp, sp, SZ_PT_REGS - 8
#else
	add	sp, sp, PT_r0
#endif

.endm

/*------------------------------------------------------------------------*/
.macro EXCEPTION_EPILOGUE

	; INPUT: r0 has STAT32 of calling context

	btst	r0, STATUS_U_BIT	; Z flag set if K, used in restoring SP

	ld	r10, [sp, PT_bta]
	sr	r10, [erbta]

	LD2	r10, r11, PT_ret
	sr	r10, [eret]
	sr	r11, [erstatus]

	__RESTORE_REGFILE_SOFT
	__RESTORE_REGFILE_HARD

	add	sp, sp, SZ_PT_REGS
.endm

.macro FAKE_RET_FROM_EXCPN
	lr      r9, [status32]
	bclr    r9, r9, STATUS_AE_BIT
	bset    r9, r9, STATUS_IE_BIT
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
