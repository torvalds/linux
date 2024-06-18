// SPDX-License-Identifier: GPL-2.0
#include <linux/sched.h>
#include <asm/ptrace-abi.h>

void clear_flushed_tls(struct task_struct *task)
{
}

int arch_set_tls(struct task_struct *t, unsigned long tls)
{
	/*
	 * If CLONE_SETTLS is set, we need to save the thread id
	 * so it can be set during context switches.
	 */
	t->thread.regs.regs.gp[FS_BASE / sizeof(unsigned long)] = tls;

	return 0;
}
