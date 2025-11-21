// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */
#define COMPILE_OFFSETS

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/thread_info.h>
#include <linux/kbuild.h>
#include <linux/ptrace.h>
#include <asm/hardirq.h>
#include <asm/page.h>


int main(void)
{
	DEFINE(TASK_THREAD, offsetof(struct task_struct, thread));
	DEFINE(TASK_THREAD_INFO, offsetof(struct task_struct, stack));

	BLANK();

	DEFINE(THREAD_CALLEE_REG, offsetof(struct thread_struct, callee_reg));
	DEFINE(THREAD_FAULT_ADDR,
	       offsetof(struct thread_struct, fault_address));

	BLANK();

	DEFINE(THREAD_INFO_KSP, offsetof(struct thread_info, ksp));
	DEFINE(THREAD_INFO_FLAGS, offsetof(struct thread_info, flags));
	DEFINE(THREAD_INFO_PREEMPT_COUNT,
	       offsetof(struct thread_info, preempt_count));

	BLANK();

	DEFINE(TASK_ACT_MM, offsetof(struct task_struct, active_mm));
	DEFINE(TASK_TGID, offsetof(struct task_struct, tgid));
	DEFINE(TASK_PID, offsetof(struct task_struct, pid));
	DEFINE(TASK_COMM, offsetof(struct task_struct, comm));

	DEFINE(MM_CTXT, offsetof(struct mm_struct, context));
	DEFINE(MM_PGD, offsetof(struct mm_struct, pgd));

	DEFINE(MM_CTXT_ASID, offsetof(mm_context_t, asid));

	BLANK();

	DEFINE(PT_status32, offsetof(struct pt_regs, status32));
	DEFINE(PT_event, offsetof(struct pt_regs, ecr));
	DEFINE(PT_bta, offsetof(struct pt_regs, bta));
	DEFINE(PT_sp, offsetof(struct pt_regs, sp));
	DEFINE(PT_r0, offsetof(struct pt_regs, r0));
	DEFINE(PT_r1, offsetof(struct pt_regs, r1));
	DEFINE(PT_r2, offsetof(struct pt_regs, r2));
	DEFINE(PT_r3, offsetof(struct pt_regs, r3));
	DEFINE(PT_r4, offsetof(struct pt_regs, r4));
	DEFINE(PT_r5, offsetof(struct pt_regs, r5));
	DEFINE(PT_r6, offsetof(struct pt_regs, r6));
	DEFINE(PT_r7, offsetof(struct pt_regs, r7));
	DEFINE(PT_r8, offsetof(struct pt_regs, r8));
	DEFINE(PT_r10, offsetof(struct pt_regs, r10));
	DEFINE(PT_r26, offsetof(struct pt_regs, r26));
	DEFINE(PT_ret, offsetof(struct pt_regs, ret));
	DEFINE(PT_blink, offsetof(struct pt_regs, blink));
	OFFSET(PT_fp, pt_regs, fp);
	DEFINE(PT_lpe, offsetof(struct pt_regs, lp_end));
	DEFINE(PT_lpc, offsetof(struct pt_regs, lp_count));
#ifdef CONFIG_ISA_ARCV2
	OFFSET(PT_r12, pt_regs, r12);
	OFFSET(PT_r30, pt_regs, r30);
#endif
#ifdef CONFIG_ARC_HAS_ACCL_REGS
	OFFSET(PT_r58, pt_regs, r58);
	OFFSET(PT_r59, pt_regs, r59);
#endif
#ifdef CONFIG_ARC_DSP_SAVE_RESTORE_REGS
	OFFSET(PT_DSP_CTRL, pt_regs, DSP_CTRL);
#endif

	DEFINE(SZ_CALLEE_REGS, sizeof(struct callee_regs));
	DEFINE(SZ_PT_REGS, sizeof(struct pt_regs));

	return 0;
}
