/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

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

	DEFINE(THREAD_KSP, offsetof(struct thread_struct, ksp));
	DEFINE(THREAD_CALLEE_REG, offsetof(struct thread_struct, callee_reg));
#ifdef CONFIG_ARC_CURR_IN_REG
	DEFINE(THREAD_USER_R25, offsetof(struct thread_struct, user_r25));
#endif
	DEFINE(THREAD_FAULT_ADDR,
	       offsetof(struct thread_struct, fault_address));

	BLANK();

	DEFINE(THREAD_INFO_FLAGS, offsetof(struct thread_info, flags));
	DEFINE(THREAD_INFO_PREEMPT_COUNT,
	       offsetof(struct thread_info, preempt_count));

	BLANK();

	DEFINE(TASK_ACT_MM, offsetof(struct task_struct, active_mm));
	DEFINE(TASK_TGID, offsetof(struct task_struct, tgid));

	DEFINE(MM_CTXT, offsetof(struct mm_struct, context));
	DEFINE(MM_PGD, offsetof(struct mm_struct, pgd));

	DEFINE(MM_CTXT_ASID, offsetof(mm_context_t, asid));

	BLANK();

	DEFINE(PT_status32, offsetof(struct pt_regs, status32));
	DEFINE(PT_orig_r8, offsetof(struct pt_regs, orig_r8_word));
	DEFINE(PT_sp, offsetof(struct pt_regs, sp));
	DEFINE(PT_r0, offsetof(struct pt_regs, r0));
	DEFINE(PT_r1, offsetof(struct pt_regs, r1));
	DEFINE(PT_r2, offsetof(struct pt_regs, r2));
	DEFINE(PT_r3, offsetof(struct pt_regs, r3));
	DEFINE(PT_r4, offsetof(struct pt_regs, r4));
	DEFINE(PT_r5, offsetof(struct pt_regs, r5));
	DEFINE(PT_r6, offsetof(struct pt_regs, r6));
	DEFINE(PT_r7, offsetof(struct pt_regs, r7));

	return 0;
}
