/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * Vineetg: Aug 2009
 *  -Moved core context switch macro out of entry.S into this file.
 *  -This is the more "natural" hand written assembler
 */

#include <linux/linkage.h>
#include <asm/entry.h>       /* For the SAVE_* macros */
#include <asm/asm-offsets.h>

#define KSP_WORD_OFF 	((TASK_THREAD + THREAD_KSP) / 4)

;################### Low Level Context Switch ##########################

	.section .sched.text,"ax",@progbits
	.align 4
	.global __switch_to
	.type   __switch_to, @function
__switch_to:
	CFI_STARTPROC

	/* Save regs on kernel mode stack of task */
	st.a    blink, [sp, -4]
	st.a    fp, [sp, -4]
	SAVE_CALLEE_SAVED_KERNEL

	/* Save the now KSP in task->thread.ksp */
#if KSP_WORD_OFF  <= 255
	st.as  sp, [r0, KSP_WORD_OFF]
#else
	/* Workaround for NR_CPUS=4k as ST.as can only take s9 offset */
	add2	r24, r0, KSP_WORD_OFF
	st	sp, [r24]
#endif
	/*
	* Return last task in r0 (return reg)
	* On ARC, Return reg = First Arg reg = r0.
	* Since we already have last task in r0,
	* don't need to do anything special to return it
	*/

	/*
	 * switch to new task, contained in r1
	 * Temp reg r3 is required to get the ptr to store val
	 */
	SET_CURR_TASK_ON_CPU  r1, r3

	/* reload SP with kernel mode stack pointer in task->thread.ksp */
	ld.as  sp, [r1, (TASK_THREAD + THREAD_KSP)/4]

	/* restore the registers */
	RESTORE_CALLEE_SAVED_KERNEL
	ld.ab   fp, [sp, 4]
	ld.ab   blink, [sp, 4]
	j       [blink]

END_CFI(__switch_to)
