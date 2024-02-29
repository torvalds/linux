/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __ASM_ARC_ENTRY_H
#define __ASM_ARC_ENTRY_H

#include <asm/unistd.h>		/* For NR_syscalls defination */
#include <asm/arcregs.h>
#include <asm/ptrace.h>
#include <asm/processor.h>	/* For VMALLOC_START */
#include <asm/mmu.h>

#ifdef __ASSEMBLY__

#ifdef CONFIG_ISA_ARCOMPACT
#include <asm/entry-compact.h>	/* ISA specific bits */
#else
#include <asm/entry-arcv2.h>
#endif

/*
 * save user mode callee regs as struct callee_regs
 *  - needed by fork/do_signal/unaligned-access-emulation.
 */
.macro SAVE_CALLEE_SAVED_USER
	SAVE_ABI_CALLEE_REGS
.endm

/*
 * restore user mode callee regs as struct callee_regs
 *  - could have been changed by ptrace tracer or unaligned-access fixup
 */
.macro RESTORE_CALLEE_SAVED_USER
	RESTORE_ABI_CALLEE_REGS
.endm

/*
 * save/restore kernel mode callee regs at the time of context switch
 */
.macro SAVE_CALLEE_SAVED_KERNEL
	SAVE_ABI_CALLEE_REGS
.endm

.macro RESTORE_CALLEE_SAVED_KERNEL
	RESTORE_ABI_CALLEE_REGS
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

/*
 * Retrieve the current running task on this CPU
 *  - loads it from backing _current_task[] (and can't use the
 *    caching reg for current task
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
	mov gp, \tsk
#endif

.endm


#else   /* Uniprocessor implementation of macros */

.macro  GET_CURR_TASK_ON_CPU    reg
	ld  \reg, [@_current_task]
.endm

.macro  SET_CURR_TASK_ON_CPU    tsk, tmp
	st  \tsk, [@_current_task]
#ifdef CONFIG_ARC_CURR_IN_REG
	mov gp, \tsk
#endif
.endm

#endif /* SMP / UNI */

/*
 * Get the ptr to some field of Current Task at @off in task struct
 *  - Uses current task cached in reg if enabled
 */
#ifdef CONFIG_ARC_CURR_IN_REG

.macro GET_CURR_TASK_FIELD_PTR  off,  reg
	add \reg, gp, \off
.endm

#else

.macro GET_CURR_TASK_FIELD_PTR  off,  reg
	GET_CURR_TASK_ON_CPU  \reg
	add \reg, \reg, \off
.endm

#endif	/* CONFIG_ARC_CURR_IN_REG */

#else	/* !__ASSEMBLY__ */

extern void do_signal(struct pt_regs *);
extern void do_notify_resume(struct pt_regs *);
extern int do_privilege_fault(unsigned long, struct pt_regs *);
extern int do_extension_fault(unsigned long, struct pt_regs *);
extern int insterror_is_error(unsigned long, struct pt_regs *);
extern int do_memory_error(unsigned long, struct pt_regs *);
extern int trap_is_brkpt(unsigned long, struct pt_regs *);
extern int do_misaligned_error(unsigned long, struct pt_regs *);
extern int do_trap5_error(unsigned long, struct pt_regs *);
extern int do_misaligned_access(unsigned long, struct pt_regs *, struct callee_regs *);
extern void do_machine_check_fault(unsigned long, struct pt_regs *);
extern void do_non_swi_trap(unsigned long, struct pt_regs *);
extern void do_insterror_or_kprobe(unsigned long, struct pt_regs *);
extern void do_page_fault(unsigned long, struct pt_regs *);

#endif

#endif  /* __ASM_ARC_ENTRY_H */
