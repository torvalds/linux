#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/bug.h>

#include <asm/compat.h>
#include <asm/perf_regs.h>
#include <asm/ptrace.h>

u64 perf_reg_value(struct pt_regs *regs, int idx)
{
	if (WARN_ON_ONCE((u32)idx >= PERF_REG_ARM64_MAX))
		return 0;

	/*
	 * Compat (i.e. 32 bit) mode:
	 * - PC has been set in the pt_regs struct in kernel_entry,
	 * - Handle SP and LR here.
	 */
	if (compat_user_mode(regs)) {
		if ((u32)idx == PERF_REG_ARM64_SP)
			return regs->compat_sp;
		if ((u32)idx == PERF_REG_ARM64_LR)
			return regs->compat_lr;
	}

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
