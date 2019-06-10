// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 */

#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/ptrace.h>
#include <linux/hardirq.h>
#include <linux/thread_info.h>
#include <linux/kbuild.h>

int main(void)
{
	/* struct task_struct */
	OFFSET(TASK_THREAD, task_struct, thread);
	BLANK();

	/* struct thread_struct */
	OFFSET(THREAD_KSP, thread_struct, ksp);
	OFFSET(THREAD_KPSR, thread_struct, kpsr);
	BLANK();

	/* struct pt_regs */
	OFFSET(PT_ORIG_R2, pt_regs, orig_r2);
	OFFSET(PT_ORIG_R7, pt_regs, orig_r7);

	OFFSET(PT_R1, pt_regs, r1);
	OFFSET(PT_R2, pt_regs, r2);
	OFFSET(PT_R3, pt_regs, r3);
	OFFSET(PT_R4, pt_regs, r4);
	OFFSET(PT_R5, pt_regs, r5);
	OFFSET(PT_R6, pt_regs, r6);
	OFFSET(PT_R7, pt_regs, r7);
	OFFSET(PT_R8, pt_regs, r8);
	OFFSET(PT_R9, pt_regs, r9);
	OFFSET(PT_R10, pt_regs, r10);
	OFFSET(PT_R11, pt_regs, r11);
	OFFSET(PT_R12, pt_regs, r12);
	OFFSET(PT_R13, pt_regs, r13);
	OFFSET(PT_R14, pt_regs, r14);
	OFFSET(PT_R15, pt_regs, r15);
	OFFSET(PT_EA, pt_regs, ea);
	OFFSET(PT_RA, pt_regs, ra);
	OFFSET(PT_FP, pt_regs, fp);
	OFFSET(PT_SP, pt_regs, sp);
	OFFSET(PT_GP, pt_regs, gp);
	OFFSET(PT_ESTATUS, pt_regs, estatus);
	DEFINE(PT_REGS_SIZE, sizeof(struct pt_regs));
	BLANK();

	/* struct switch_stack */
	OFFSET(SW_R16, switch_stack, r16);
	OFFSET(SW_R17, switch_stack, r17);
	OFFSET(SW_R18, switch_stack, r18);
	OFFSET(SW_R19, switch_stack, r19);
	OFFSET(SW_R20, switch_stack, r20);
	OFFSET(SW_R21, switch_stack, r21);
	OFFSET(SW_R22, switch_stack, r22);
	OFFSET(SW_R23, switch_stack, r23);
	OFFSET(SW_FP, switch_stack, fp);
	OFFSET(SW_GP, switch_stack, gp);
	OFFSET(SW_RA, switch_stack, ra);
	DEFINE(SWITCH_STACK_SIZE, sizeof(struct switch_stack));
	BLANK();

	/* struct thread_info */
	OFFSET(TI_FLAGS, thread_info, flags);
	OFFSET(TI_PREEMPT_COUNT, thread_info, preempt_count);
	BLANK();

	return 0;
}
