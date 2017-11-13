// SPDX-License-Identifier: GPL-2.0
#include <linux/sched.h>
#include <asm/ptrace-abi.h>

void clear_flushed_tls(struct task_struct *task)
{
}

int arch_copy_tls(struct task_struct *t)
{
	/*
	 * If CLONE_SETTLS is set, we need to save the thread id
	 * (which is argument 5, child_tid, of clone) so it can be set
	 * during context switches.
	 */
	t->thread.arch.fs = t->thread.regs.regs.gp[R8 / sizeof(long)];

	return 0;
}
