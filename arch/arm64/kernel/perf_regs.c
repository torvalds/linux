// SPDX-License-Identifier: GPL-2.0
#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/bug.h>
#include <linux/sched/task_stack.h>

#include <asm/perf_regs.h>
#include <asm/ptrace.h>

u64 perf_reg_value(struct pt_regs *regs, int idx)
{
	if (WARN_ON_ONCE((u32)idx >= PERF_REG_ARM64_MAX))
		return 0;

	/*
	 * Our handling of compat tasks (PERF_SAMPLE_REGS_ABI_32) is weird, but
	 * we're stuck with it for ABI compatibility reasons.
	 *
	 * For a 32-bit consumer inspecting a 32-bit task, then it will look at
	 * the first 16 registers (see arch/arm/include/uapi/asm/perf_regs.h).
	 * These correspond directly to a prefix of the registers saved in our
	 * 'struct pt_regs', with the exception of the PC, so we copy that down
	 * (x15 corresponds to SP_hyp in the architecture).
	 *
	 * So far, so good.
	 *
	 * The oddity arises when a 64-bit consumer looks at a 32-bit task and
	 * asks for registers beyond PERF_REG_ARM_MAX. In this case, we return
	 * SP_usr, LR_usr and PC in the positions where the AArch64 SP, LR and
	 * PC registers would normally live. The initial idea was to allow a
	 * 64-bit unwinder to unwind a 32-bit task and, although it's not clear
	 * how well that works in practice, somebody might be relying on it.
	 *
	 * At the time we make a sample, we don't know whether the consumer is
	 * 32-bit or 64-bit, so we have to cater for both possibilities.
	 */
	if (compat_user_mode(regs)) {
		if ((u32)idx == PERF_REG_ARM64_SP)
			return regs->compat_sp;
		if ((u32)idx == PERF_REG_ARM64_LR)
			return regs->compat_lr;
		if (idx == 15)
			return regs->pc;
	}

	if ((u32)idx == PERF_REG_ARM64_SP)
		return regs->sp;

	if ((u32)idx == PERF_REG_ARM64_PC)
		return regs->pc;

	return regs->regs[idx];
}

#define REG_RESERVED (~((1ULL << PERF_REG_ARM64_MAX) - 1))

int perf_reg_validate(u64 mask)
{
	if (!mask || mask & REG_RESERVED)
		return -EINVAL;

	return 0;
}

u64 perf_reg_abi(struct task_struct *task)
{
	if (is_compat_thread(task_thread_info(task)))
		return PERF_SAMPLE_REGS_ABI_32;
	else
		return PERF_SAMPLE_REGS_ABI_64;
}

void perf_get_regs_user(struct perf_regs *regs_user,
			struct pt_regs *regs,
			struct pt_regs *regs_user_copy)
{
	regs_user->regs = task_pt_regs(current);
	regs_user->abi = perf_reg_abi(current);
}
