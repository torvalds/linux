// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 2013 Cavium, Inc.
 */

#include <linux/perf_event.h>

#include <asm/ptrace.h>

#ifdef CONFIG_32BIT
u64 perf_reg_abi(struct task_struct *tsk)
{
	return PERF_SAMPLE_REGS_ABI_32;
}
#else /* Must be CONFIG_64BIT */
u64 perf_reg_abi(struct task_struct *tsk)
{
	if (test_tsk_thread_flag(tsk, TIF_32BIT_REGS))
		return PERF_SAMPLE_REGS_ABI_32;
	else
		return PERF_SAMPLE_REGS_ABI_64;
}
#endif /* CONFIG_32BIT */

int perf_reg_validate(u64 mask)
{
	if (!mask)
		return -EINVAL;
	if (mask & ~((1ull << PERF_REG_LOONGARCH_MAX) - 1))
		return -EINVAL;
	return 0;
}

u64 perf_reg_value(struct pt_regs *regs, int idx)
{
	if (WARN_ON_ONCE((u32)idx >= PERF_REG_LOONGARCH_MAX))
		return 0;

	if ((u32)idx == PERF_REG_LOONGARCH_PC)
		return regs->csr_era;

	return regs->regs[idx];
}

void perf_get_regs_user(struct perf_regs *regs_user,
			struct pt_regs *regs)
{
	regs_user->regs = task_pt_regs(current);
	regs_user->abi = perf_reg_abi(current);
}
