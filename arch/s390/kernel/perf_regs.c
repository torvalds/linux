// SPDX-License-Identifier: GPL-2.0
#include <linux/perf_event.h>
#include <linux/perf_regs.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/bug.h>
#include <asm/ptrace.h>
#include <asm/fpu/api.h>
#include <asm/fpu/types.h>

u64 perf_reg_value(struct pt_regs *regs, int idx)
{
	freg_t fp;

	if (idx >= PERF_REG_S390_R0 && idx <= PERF_REG_S390_R15)
		return regs->gprs[idx];

	if (idx >= PERF_REG_S390_FP0 && idx <= PERF_REG_S390_FP15) {
		if (!user_mode(regs))
			return 0;

		idx -= PERF_REG_S390_FP0;
		fp = MACHINE_HAS_VX ? *(freg_t *)(current->thread.fpu.vxrs + idx)
				    : current->thread.fpu.fprs[idx];
		return fp.ui;
	}

	if (idx == PERF_REG_S390_MASK)
		return regs->psw.mask;
	if (idx == PERF_REG_S390_PC)
		return regs->psw.addr;

	WARN_ON_ONCE((u32)idx >= PERF_REG_S390_MAX);
	return 0;
}

#define REG_RESERVED (~((1UL << PERF_REG_S390_MAX) - 1))

int perf_reg_validate(u64 mask)
{
	if (!mask || mask & REG_RESERVED)
		return -EINVAL;

	return 0;
}

u64 perf_reg_abi(struct task_struct *task)
{
	if (test_tsk_thread_flag(task, TIF_31BIT))
		return PERF_SAMPLE_REGS_ABI_32;

	return PERF_SAMPLE_REGS_ABI_64;
}

void perf_get_regs_user(struct perf_regs *regs_user,
			struct pt_regs *regs)
{
	/*
	 * Use the regs from the first interruption and let
	 * perf_sample_regs_intr() handle interrupts (regs == get_irq_regs()).
	 *
	 * Also save FPU registers for user-space tasks only.
	 */
	regs_user->regs = task_pt_regs(current);
	if (user_mode(regs_user->regs))
		save_fpu_regs();
	regs_user->abi = perf_reg_abi(current);
}
