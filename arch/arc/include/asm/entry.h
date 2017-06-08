/*
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARC_ENTRY_H
#define __ASM_ARC_ENTRY_H

#include <asm/unistd.h>		/* For NR_syscalls defination */
#include <asm/arcregs.h>
#include <asm/ptrace.h>
#include <asm/processor.h>	/* For VMALLOC_START */
#include <asm/mmu.h>

#ifdef CONFIG_ISA_ARCOMPACT
#include <asm/entry-compact.h>	/* ISA specific bits */
#else
#include <asm/entry-arcv2.h>
#endif

/* Note on the LD/ST addr modes with addr reg wback
 *
 * LD.a same as LD.aw
 *
 * LD.a    reg1, [reg2, x]  => Pre Incr
 *      Eff Addr for load = [reg2 + x]
 *
 * LD.ab   reg1, [reg2, x]  => Post Incr
 *      Eff Addr for load = [reg2]
 */

.macro PUSH reg
	st.a	\reg, [sp, -4]
.endm

.macro PUSHAX aux
	lr	r9, [\aux]
	PUSH	r9
.endm

.macro POP reg
	ld.ab	\reg, [sp, 4]
.endm

.macro POPAX aux
	POP	r9
	sr	r9, [\aux]
.endm

/*--------------------------------------------------------------
 * Helpers to save/restore Scratch Regs:
 * used by Interrupt/Exception Prologue/Epilogue
 *-------------------------------------------------------------*/
.macro  SAVE_R0_TO_R12
	PUSH	r0
	PUSH	r1
	PUSH	r2
	PUSH	r3
	PUSH	r4
	PUSH	r5
	PUSH	r6
	PUSH	r7
	PUSH	r8
	PUSH	r9
	PUSH	r10
	PUSH	r11
	PUSH	r12
.endm

.macro RESTORE_R12_TO_R0
	POP	r12
	POP	r11
	POP	r10
	POP	r9
	POP	r8
	POP	r7
	POP	r6
	POP	r5
	POP	r4
	POP	r3
	POP	r2
	POP	r1
	POP	r0

#ifdef CONFIG_ARC_CURR_IN_REG
	ld	r25, [sp, 12]
#endif
.endm

/*--------------------------------------------------------------
 * Helpers to save/restore callee-saved regs:
 * used by several macros below
 *-------------------------------------------------------------*/
.macro SAVE_R13_TO_R24
	PUSH	r13
	PUSH	r14
	PUSH	r15
	PUSH	r16
	PUSH	r17
	PUSH	r18
	PUSH	r19
	PUSH	r20
	PUSH	r21
	PUSH	r22
	PUSH	r23
	PUSH	r24
.endm

.macro RESTORE_R24_TO_R13
	POP	r24
	POP	r23
	POP	r22
	POP	r21
	POP	r20
	POP	r19
	POP	r18
	POP	r17
	POP	r16
	POP	r15
	POP	r14
	POP	r13
.endm

/*--------------------------------------------------------------
 * Collect User Mode callee regs as struct callee_regs - needed by
 * fork/do_signal/unaligned-access-emulation.
 * (By default only scratch regs are saved on entry to kernel)
 *
 * Special handling for r25 if used for caching Task Pointer.
 * It would have been saved in task->thread.user_r25 already, but to keep
 * the interface same it is copied into regular r25 placeholder in
 * struct callee_regs.
 *-------------------------------------------------------------*/
.macro SAVE_CALLEE_SAVED_USER

	mov	r12, sp		; save SP as ref to pt_regs
	SAVE_R13_TO_R24

#ifdef CONFIG_ARC_CURR_IN_REG
	; Retrieve orig r25 and save it with rest of callee_regs
	ld	r12, [r12, PT_user_r25]
	PUSH	r12
#else
	PUSH	r25
#endif

.endm

/*--------------------------------------------------------------
 * Save kernel Mode callee regs at the time of Contect Switch.
 *
 * Special handling for r25 if used for caching Task Pointer.
 * Kernel simply skips saving it since it will be loaded with
 * incoming task pointer anyways
 *-------------------------------------------------------------*/
.macro SAVE_CALLEE_SAVED_KERNEL

	SAVE_R13_TO_R24

#ifdef CONFIG_ARC_CURR_IN_REG
	sub     sp, sp, 4
#else
	PUSH	r25
#endif
.endm

/*--------------------------------------------------------------
 * Opposite of SAVE_CALLEE_SAVED_KERNEL
 *-------------------------------------------------------------*/
.macro RESTORE_CALLEE_SAVED_KERNEL

#ifdef CONFIG_ARC_CURR_IN_REG
	add     sp, sp, 4  /* skip usual r25 placeholder */
#else
	POP	r25
#endif
	RESTORE_R24_TO_R13
.endm

/*--------------------------------------------------------------
 * Opposite of SAVE_CALLEE_SAVED_USER
 *
 * ptrace tracer or unaligned-access fixup might have changed a user mode
 * callee reg which is saved back to usual r25 storage location
 *-------------------------------------------------------------*/
.macro RESTORE_CALLEE_SAVED_USER

#ifdef CONFIG_ARC_CURR_IN_REG
	POP	r12
#else
	POP	r25
#endif
	RESTORE_R24_TO_R13

	; SP is back to start of pt_regs
#ifdef CONFIG_ARC_CURR_IN_REG
	st	r12, [sp, PT_user_r25]
#endif
.endm

/*--------------------------------------------------------------
 * Super FAST Restore callee saved regs by simply re-adjusting SP
 *-------------------------------------------------------------*/
.macro DISCARD_CALLEE_SAVED_USER
	add     sp, sp, SZ_CALLEE_REGS
.endm

/*-------------------------------------------------------------
 * given a tsk struct, get to the base of it's kernel mode stack
 * tsk->thread_info is really a PAGE, whose bottom hoists stack
 * which grows upwards towards thread_info
 *------------------------------------------------------------*/

.macro GET_TSK_STACK_BASE tsk, out

	/* Get task->thread_info (this is essentially start of a PAGE) */
	ld  \out, [\tsk, TASK_THREAD_INFO]

	/* Go to end of page where stack begins (grows upwards) */
	add2 \out, \out, (THREAD_SIZE)/4

.endm

/*
 * @reg [OUT] thread_info->flags of "current"
 */
.macro GET_CURR_THR_INFO_FLAGS  reg
	GET_CURR_THR_INFO_FROM_SP  \reg
	ld  \reg, [\reg, THREAD_INFO_FLAGS]
.endm

#ifdef CONFIG_SMP

/*-------------------------------------------------
 * Retrieve the current running task on this CPU
 * 1. Determine curr CPU id.
 * 2. Use it to index into _current_task[ ]
 */
.macro  GET_CURR_TASK_ON_CPU   reg
	GET_CPU_ID  \reg
	ld.as  \reg, [@_current_task, \reg]
.endm

/*-------------------------------------------------
 * Save a new task as the "current" task on this CPU
 * 1. Determine curr CPU id.
 * 2. Use it to index into _current_task[ ]
 *
 * Coded differently than GET_CURR_TASK_ON_CPU (which uses LD.AS)
 * because ST r0, [r1, offset] can ONLY have s9 @offset
 * while   LD can take s9 (4 byte insn) or LIMM (8 byte insn)
 */

.macro  SET_CURR_TASK_ON_CPU    tsk, tmp
	GET_CPU_ID  \tmp
	add2 \tmp, @_current_task, \tmp
	st   \tsk, [\tmp]
#ifdef CONFIG_ARC_CURR_IN_REG
	mov r25, \tsk
#endif

.endm


#else   /* Uniprocessor implementation of macros */

.macro  GET_CURR_TASK_ON_CPU    reg
	ld  \reg, [@_current_task]
.endm

.macro  SET_CURR_TASK_ON_CPU    tsk, tmp
	st  \tsk, [@_current_task]
#ifdef CONFIG_ARC_CURR_IN_REG
	mov r25, \tsk
#endif
.endm

#endif /* SMP / UNI */

/* ------------------------------------------------------------------
 * Get the ptr to some field of Current Task at @off in task struct
 *  -Uses r25 for Current task ptr if that is enabled
 */

#ifdef CONFIG_ARC_CURR_IN_REG

.macro GET_CURR_TASK_FIELD_PTR  off,  reg
	add \reg, r25, \off
.endm

#else

.macro GET_CURR_TASK_FIELD_PTR  off,  reg
	GET_CURR_TASK_ON_CPU  \reg
	add \reg, \reg, \off
.endm

#endif	/* CONFIG_ARC_CURR_IN_REG */

#endif  /* __ASM_ARC_ENTRY_H */
