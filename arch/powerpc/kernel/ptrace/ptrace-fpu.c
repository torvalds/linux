// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/regset.h>

#include <asm/switch_to.h>

#include "ptrace-decl.h"

int ptrace_get_fpr(struct task_struct *child, int index, unsigned long *data)
{
#ifdef CONFIG_PPC_FPU_REGS
	unsigned int fpidx = index - PT_FPR0;
#endif

	if (index > PT_FPSCR)
		return -EIO;

#ifdef CONFIG_PPC_FPU_REGS
	flush_fp_to_thread(child);
	if (fpidx < (PT_FPSCR - PT_FPR0)) {
		if (IS_ENABLED(CONFIG_PPC32))
			// On 32-bit the index we are passed refers to 32-bit words
			*data = ((u32 *)child->thread.fp_state.fpr)[fpidx];
		else
			memcpy(data, &child->thread.TS_FPR(fpidx), sizeof(long));
	} else
		*data = child->thread.fp_state.fpscr;
#else
	*data = 0;
#endif

	return 0;
}

int ptrace_put_fpr(struct task_struct *child, int index, unsigned long data)
{
#ifdef CONFIG_PPC_FPU_REGS
	unsigned int fpidx = index - PT_FPR0;
#endif

	if (index > PT_FPSCR)
		return -EIO;

#ifdef CONFIG_PPC_FPU_REGS
	flush_fp_to_thread(child);
	if (fpidx < (PT_FPSCR - PT_FPR0)) {
		if (IS_ENABLED(CONFIG_PPC32))
			// On 32-bit the index we are passed refers to 32-bit words
			((u32 *)child->thread.fp_state.fpr)[fpidx] = data;
		else
			memcpy(&child->thread.TS_FPR(fpidx), &data, sizeof(long));
	} else
		child->thread.fp_state.fpscr = data;
#endif

	return 0;
}

