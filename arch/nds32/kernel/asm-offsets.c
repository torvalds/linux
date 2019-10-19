// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/kbuild.h>
#include <asm/thread_info.h>
#include <asm/ptrace.h>

int main(void)
{
	DEFINE(TSK_TI_FLAGS, offsetof(struct task_struct, thread_info.flags));
	DEFINE(TSK_TI_PREEMPT,
	       offsetof(struct task_struct, thread_info.preempt_count));
	DEFINE(THREAD_CPU_CONTEXT,
	       offsetof(struct task_struct, thread.cpu_context));
	DEFINE(OSP_OFFSET, offsetof(struct pt_regs, osp));
	DEFINE(SP_OFFSET, offsetof(struct pt_regs, sp));
	DEFINE(FUCOP_CTL_OFFSET, offsetof(struct pt_regs, fucop_ctl));
	DEFINE(IPSW_OFFSET, offsetof(struct pt_regs, ipsw));
	DEFINE(SYSCALLNO_OFFSET, offsetof(struct pt_regs, syscallno));
	DEFINE(IPC_OFFSET, offsetof(struct pt_regs, ipc));
	DEFINE(R0_OFFSET, offsetof(struct pt_regs, uregs[0]));
	DEFINE(R15_OFFSET, offsetof(struct pt_regs, uregs[15]));
	DEFINE(CLOCK_REALTIME_RES, MONOTONIC_RES_NSEC);
	DEFINE(CLOCK_COARSE_RES, LOW_RES_NSEC);
	return 0;
}
