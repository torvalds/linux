// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/regset.h>

#include <asm/switch_to.h>

#include "ptrace-decl.h"

int ptrace_get_fpr(struct task_struct *child, int index, unsigned long *data)
{
	unsigned int fpidx = index - PT_FPR0;

	if (index > PT_FPSCR)
		return -EIO;

	flush_fp_to_thread(child);
	if (fpidx < (PT_FPSCR - PT_FPR0))
		memcpy(data, &child->thread.TS_FPR(fpidx), sizeof(long));
	else
		*data = child->thread.fp_state.fpscr;

	return 0;
}

int ptrace_put_fpr(struct task_struct *child, int index, unsigned long data)
{
	unsigned int fpidx = index - PT_FPR0;

	if (index > PT_FPSCR)
		return -EIO;

	flush_fp_to_thread(child);
	if (fpidx < (PT_FPSCR - PT_FPR0))
		memcpy(&child->thread.TS_FPR(fpidx), &data, sizeof(long));
	else
		child->thread.fp_state.fpscr = data;

	return 0;
}

