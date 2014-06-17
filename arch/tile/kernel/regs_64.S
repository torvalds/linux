/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/linkage.h>
#include <asm/ptrace.h>
#include <asm/asm-offsets.h>
#include <arch/spr_def.h>
#include <asm/processor.h>
#include <asm/switch_to.h>

/*
 * See <asm/switch_to.h>; called with prev and next task_struct pointers.
 * "prev" is returned in r0 for _switch_to and also for ret_from_fork.
 *
 * We want to save pc/sp in "prev", and get the new pc/sp from "next".
 * We also need to save all the callee-saved registers on the stack.
 *
 * Intel enables/disables access to the hardware cycle counter in
 * seccomp (secure computing) environments if necessary, based on
 * has_secure_computing().  We might want to do this at some point,
 * though it would require virtualizing the other SPRs under WORLD_ACCESS.
 *
 * Since we're saving to the stack, we omit sp from this list.
 * And for parallels with other architectures, we save lr separately,
 * in the thread_struct itself (as the "pc" field).
 *
 * This code also needs to be aligned with process.c copy_thread()
 */

#if CALLEE_SAVED_REGS_COUNT != 24
# error Mismatch between <asm/switch_to.h> and kernel/entry.S
#endif
#define FRAME_SIZE ((2 + CALLEE_SAVED_REGS_COUNT) * 8)

#define SAVE_REG(r) { st r12, r; addi r12, r12, 8 }
#define LOAD_REG(r) { ld r, r12; addi r12, r12, 8 }
#define FOR_EACH_CALLEE_SAVED_REG(f)					\
							f(r30); f(r31); \
	f(r32); f(r33); f(r34); f(r35);	f(r36); f(r37); f(r38); f(r39); \
	f(r40); f(r41); f(r42); f(r43); f(r44); f(r45); f(r46); f(r47); \
	f(r48); f(r49); f(r50); f(r51); f(r52);

STD_ENTRY_SECTION(__switch_to, .sched.text)
	{
	  move r10, sp
	  st sp, lr
	}
	{
	  addli r11, sp, -FRAME_SIZE + 8
	  addli sp, sp, -FRAME_SIZE
	}
	{
	  st r11, r10
	  addli r4, r1, TASK_STRUCT_THREAD_KSP_OFFSET
	}
	{
	  ld r13, r4   /* Load new sp to a temp register early. */
	  addi r12, sp, 16
	}
	FOR_EACH_CALLEE_SAVED_REG(SAVE_REG)
	addli r3, r0, TASK_STRUCT_THREAD_KSP_OFFSET
	{
	  st r3, sp
	  addli r3, r0, TASK_STRUCT_THREAD_PC_OFFSET
	}
	{
	  st r3, lr
	  addli r4, r1, TASK_STRUCT_THREAD_PC_OFFSET
	}
	{
	  ld lr, r4
	  addi r12, r13, 16
	}
	{
	  /* Update sp and ksp0 simultaneously to avoid backtracer warnings. */
	  move sp, r13
	  mtspr SPR_SYSTEM_SAVE_K_0, r2
	}
	FOR_EACH_CALLEE_SAVED_REG(LOAD_REG)
.L__switch_to_pc:
	{
	  addli sp, sp, FRAME_SIZE
	  jrp lr   /* r0 is still valid here, so return it */
	}
	STD_ENDPROC(__switch_to)

/* Return a suitable address for the backtracer for suspended threads */
STD_ENTRY_SECTION(get_switch_to_pc, .sched.text)
	lnk r0
	{
	  addli r0, r0, .L__switch_to_pc - .
	  jrp lr
	}
	STD_ENDPROC(get_switch_to_pc)

STD_ENTRY(get_pt_regs)
	.irp reg, r0, r1, r2, r3, r4, r5, r6, r7, \
		 r8, r9, r10, r11, r12, r13, r14, r15, \
		 r16, r17, r18, r19, r20, r21, r22, r23, \
		 r24, r25, r26, r27, r28, r29, r30, r31, \
		 r32, r33, r34, r35, r36, r37, r38, r39, \
		 r40, r41, r42, r43, r44, r45, r46, r47, \
		 r48, r49, r50, r51, r52, tp, sp
	{
	 st r0, \reg
	 addi r0, r0, 8
	}
	.endr
	{
	 st r0, lr
	 addi r0, r0, PTREGS_OFFSET_PC - PTREGS_OFFSET_LR
	}
	lnk r1
	{
	 st r0, r1
	 addi r0, r0, PTREGS_OFFSET_EX1 - PTREGS_OFFSET_PC
	}
	mfspr r1, INTERRUPT_CRITICAL_SECTION
	shli r1, r1, SPR_EX_CONTEXT_1_1__ICS_SHIFT
	ori r1, r1, KERNEL_PL
	{
	 st r0, r1
	 addi r0, r0, PTREGS_OFFSET_FAULTNUM - PTREGS_OFFSET_EX1
	}
	{
	 st r0, zero       /* clear faultnum */
	 addi r0, r0, PTREGS_OFFSET_ORIG_R0 - PTREGS_OFFSET_FAULTNUM
	}
	{
	 st r0, zero       /* clear orig_r0 */
	 addli r0, r0, -PTREGS_OFFSET_ORIG_R0    /* restore r0 to base */
	}
	jrp lr
	STD_ENDPROC(get_pt_regs)
