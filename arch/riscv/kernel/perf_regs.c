// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 Hangzhou C-SKY Microsystems co.,ltd. */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/bug.h>
#include <asm/perf_regs.h>
#include <asm/ptrace.h>

u64 perf_reg_value(struct pt_regs *regs, int idx)
{
	if (WARN_ON_ONCE((u32)idx >= PERF_REG_RISCV_MAX))
		return 0;

	return ((unsigned long *)regs)[idx];
}

#define REG_RESERVED (~((1ULL << PERF_REG_RISCV_MAX) - 1))

int perf_reg_validate(u64 mask)
{
	if (!mask || mask & REG_RESERVED)
		return -EINVAL;

	return 0;
}

u64 perf_reg_abi(struct task_struct *task)
{
#if __riscv_xlen == 64
	return PERF_SAMPLE_REGS_ABI_64;
#else
	return PERF_SAMPLE_REGS_ABI_32;
#endif
}

void perf_get_regs_user(struct perf_regs *regs_user,
			struct pt_regs *regs,
			struct pt_regs *regs_user_copy)
{
	regs_user->regs = task_pt_regs(current);
	regs_user->abi = perf_reg_abi(current);
}
