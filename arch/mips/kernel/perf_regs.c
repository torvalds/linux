// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Some parts derived from x86 version of this file.
 *
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
	if (mask & ~((1ull << PERF_REG_MIPS_MAX) - 1))
		return -EINVAL;
	return 0;
}

u64 perf_reg_value(struct pt_regs *regs, int idx)
{
	long v;

	switch (idx) {
	case PERF_REG_MIPS_PC:
		v = regs->cp0_epc;
		break;
	case PERF_REG_MIPS_R1 ... PERF_REG_MIPS_R25:
		v = regs->regs[idx - PERF_REG_MIPS_R1 + 1];
		break;
	case PERF_REG_MIPS_R28 ... PERF_REG_MIPS_R31:
		v = regs->regs[idx - PERF_REG_MIPS_R28 + 28];
		break;

	default:
		WARN_ON_ONCE(1);
		return 0;
	}

	return (s64)v; /* Sign extend if 32-bit. */
}

void perf_get_regs_user(struct perf_regs *regs_user,
			struct pt_regs *regs)
{
	regs_user->regs = task_pt_regs(current);
	regs_user->abi = perf_reg_abi(current);
}
