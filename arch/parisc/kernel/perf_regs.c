// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2025 by Helge Deller <deller@gmx.de> */

#include <linux/perf_event.h>
#include <linux/perf_regs.h>
#include <asm/ptrace.h>

u64 perf_reg_value(struct pt_regs *regs, int idx)
{
	switch (idx) {
	case PERF_REG_PARISC_R0 ... PERF_REG_PARISC_R31:
		return regs->gr[idx - PERF_REG_PARISC_R0];
	case PERF_REG_PARISC_SR0 ... PERF_REG_PARISC_SR7:
		return regs->sr[idx - PERF_REG_PARISC_SR0];
	case PERF_REG_PARISC_IASQ0 ... PERF_REG_PARISC_IASQ1:
		return regs->iasq[idx - PERF_REG_PARISC_IASQ0];
	case PERF_REG_PARISC_IAOQ0 ... PERF_REG_PARISC_IAOQ1:
		return regs->iasq[idx - PERF_REG_PARISC_IAOQ0];
	case PERF_REG_PARISC_SAR:	/* CR11 */
		return regs->sar;
	case PERF_REG_PARISC_IIR:	/* CR19 */
		return regs->iir;
	case PERF_REG_PARISC_ISR:	/* CR20 */
		return regs->isr;
	case PERF_REG_PARISC_IOR:	/* CR21 */
		return regs->ior;
	case PERF_REG_PARISC_IPSW:	/* CR22 */
		return regs->ipsw;
	};
	WARN_ON_ONCE((u32)idx >= PERF_REG_PARISC_MAX);
	return 0;
}

#define REG_RESERVED (~((1ULL << PERF_REG_PARISC_MAX) - 1))

int perf_reg_validate(u64 mask)
{
	if (!mask || mask & REG_RESERVED)
		return -EINVAL;

	return 0;
}

u64 perf_reg_abi(struct task_struct *task)
{
	if (!IS_ENABLED(CONFIG_64BIT))
		return PERF_SAMPLE_REGS_ABI_32;

	if (test_tsk_thread_flag(task, TIF_32BIT))
		return PERF_SAMPLE_REGS_ABI_32;

	return PERF_SAMPLE_REGS_ABI_64;
}

void perf_get_regs_user(struct perf_regs *regs_user,
			struct pt_regs *regs)
{
	regs_user->regs = task_pt_regs(current);
	regs_user->abi = perf_reg_abi(current);
}
