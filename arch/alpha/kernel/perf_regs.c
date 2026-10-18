// SPDX-License-Identifier: GPL-2.0

#include <linux/bits.h>
#include <linux/perf_event.h>
#include <linux/sched/task_stack.h>
#include <asm/perf_regs.h>

#define REG(idx, field) \
	[PERF_REG_ALPHA_##idx] = offsetof(struct pt_regs, field)

#define REG_RESERVED	(~GENMASK_ULL(PERF_REG_ALPHA_MAX - 1, 0) | \
			 GENMASK_ULL(PERF_REG_ALPHA_R15, PERF_REG_ALPHA_R9))

static const unsigned int pt_regs_offset[PERF_REG_ALPHA_MAX] = {
	REG(R0, r0),	REG(R1, r1),	REG(R2, r2),	REG(R3, r3),
	REG(R4, r4),	REG(R5, r5),	REG(R6, r6),	REG(R7, r7),
	REG(R8, r8),	REG(R16, r16),	REG(R17, r17),	REG(R18, r18),
	REG(R19, r19),	REG(R20, r20),	REG(R21, r21),	REG(R22, r22),
	REG(R23, r23),	REG(R24, r24),	REG(R25, r25),	REG(R26, r26),
	REG(R27, r27),	REG(R28, r28),	REG(GP, gp),	REG(PC, pc),
};

u64 perf_reg_value(struct pt_regs *regs, int idx)
{
	if (idx == PERF_REG_ALPHA_SP) {
		if (user_mode(regs))
			return user_stack_pointer(regs);
		/*
		 * Software events built by perf_fetch_caller_regs() use a
		 * per-CPU pt_regs rather than a trap frame, and pt_regs has
		 * nowhere to record sp, so report none.
		 */
		if (!object_is_on_stack(regs))
			return 0;
		/* Same kernel sp approximation as stacktrace.c. */
		return (unsigned long)(regs + 1);
	}

	if (WARN_ON_ONCE((u32)idx >= PERF_REG_ALPHA_MAX ||
			 BIT_ULL(idx) & REG_RESERVED))
		return 0;

	return *(unsigned long *)((void *)regs + pt_regs_offset[idx]);
}

int perf_reg_validate(u64 mask)
{
	if (!mask || mask & REG_RESERVED)
		return -EINVAL;

	return 0;
}

u64 perf_reg_abi(struct task_struct *task)
{
	return PERF_SAMPLE_REGS_ABI_64;
}

void perf_get_regs_user(struct perf_regs *regs_user,
			struct pt_regs *regs)
{
	regs_user->regs = task_pt_regs(current);
	regs_user->abi = perf_reg_abi(current);
}
