// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2016 Anju T, IBM Corporation.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/perf_event.h>
#include <linux/bug.h>
#include <linux/stddef.h>
#include <asm/ptrace.h>
#include <asm/perf_regs.h>

u64 PERF_REG_EXTENDED_MASK;

#define PT_REGS_OFFSET(id, r) [id] = offsetof(struct pt_regs, r)

#define REG_RESERVED (~(PERF_REG_EXTENDED_MASK | PERF_REG_PMU_MASK))

static unsigned int pt_regs_offset[PERF_REG_POWERPC_MAX] = {
	PT_REGS_OFFSET(PERF_REG_POWERPC_R0,  gpr[0]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R1,  gpr[1]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R2,  gpr[2]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R3,  gpr[3]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R4,  gpr[4]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R5,  gpr[5]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R6,  gpr[6]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R7,  gpr[7]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R8,  gpr[8]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R9,  gpr[9]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R10, gpr[10]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R11, gpr[11]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R12, gpr[12]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R13, gpr[13]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R14, gpr[14]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R15, gpr[15]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R16, gpr[16]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R17, gpr[17]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R18, gpr[18]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R19, gpr[19]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R20, gpr[20]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R21, gpr[21]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R22, gpr[22]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R23, gpr[23]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R24, gpr[24]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R25, gpr[25]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R26, gpr[26]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R27, gpr[27]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R28, gpr[28]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R29, gpr[29]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R30, gpr[30]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_R31, gpr[31]),
	PT_REGS_OFFSET(PERF_REG_POWERPC_NIP, nip),
	PT_REGS_OFFSET(PERF_REG_POWERPC_MSR, msr),
	PT_REGS_OFFSET(PERF_REG_POWERPC_ORIG_R3, orig_gpr3),
	PT_REGS_OFFSET(PERF_REG_POWERPC_CTR, ctr),
	PT_REGS_OFFSET(PERF_REG_POWERPC_LINK, link),
	PT_REGS_OFFSET(PERF_REG_POWERPC_XER, xer),
	PT_REGS_OFFSET(PERF_REG_POWERPC_CCR, ccr),
#ifdef CONFIG_PPC64
	PT_REGS_OFFSET(PERF_REG_POWERPC_SOFTE, softe),
#else
	PT_REGS_OFFSET(PERF_REG_POWERPC_SOFTE, mq),
#endif
	PT_REGS_OFFSET(PERF_REG_POWERPC_TRAP, trap),
	PT_REGS_OFFSET(PERF_REG_POWERPC_DAR, dar),
	PT_REGS_OFFSET(PERF_REG_POWERPC_DSISR, dsisr),
	PT_REGS_OFFSET(PERF_REG_POWERPC_SIER, dar),
	PT_REGS_OFFSET(PERF_REG_POWERPC_MMCRA, dsisr),
};

/* Function to return the extended register values */
static u64 get_ext_regs_value(int idx)
{
	switch (idx) {
	case PERF_REG_POWERPC_PMC1 ... PERF_REG_POWERPC_PMC6:
		return get_pmcs_ext_regs(idx - PERF_REG_POWERPC_PMC1);
	case PERF_REG_POWERPC_MMCR0:
		return mfspr(SPRN_MMCR0);
	case PERF_REG_POWERPC_MMCR1:
		return mfspr(SPRN_MMCR1);
	case PERF_REG_POWERPC_MMCR2:
		return mfspr(SPRN_MMCR2);
#ifdef CONFIG_PPC64
	case PERF_REG_POWERPC_MMCR3:
		return mfspr(SPRN_MMCR3);
	case PERF_REG_POWERPC_SIER2:
		return mfspr(SPRN_SIER2);
	case PERF_REG_POWERPC_SIER3:
		return mfspr(SPRN_SIER3);
#endif
	default: return 0;
	}
}

u64 perf_reg_value(struct pt_regs *regs, int idx)
{
	if (idx == PERF_REG_POWERPC_SIER &&
	   (IS_ENABLED(CONFIG_FSL_EMB_PERF_EVENT) ||
	    IS_ENABLED(CONFIG_PPC32) ||
	    !is_sier_available()))
		return 0;

	if (idx == PERF_REG_POWERPC_MMCRA &&
	   (IS_ENABLED(CONFIG_FSL_EMB_PERF_EVENT) ||
	    IS_ENABLED(CONFIG_PPC32)))
		return 0;

	if (idx >= PERF_REG_POWERPC_MAX && idx < PERF_REG_EXTENDED_MAX)
		return get_ext_regs_value(idx);

	/*
	 * If the idx is referring to value beyond the
	 * supported registers, return 0 with a warning
	 */
	if (WARN_ON_ONCE(idx >= PERF_REG_EXTENDED_MAX))
		return 0;

	return regs_get_register(regs, pt_regs_offset[idx]);
}

int perf_reg_validate(u64 mask)
{
	if (!mask || mask & REG_RESERVED)
		return -EINVAL;
	return 0;
}

u64 perf_reg_abi(struct task_struct *task)
{
#ifdef CONFIG_PPC64
	if (!test_tsk_thread_flag(task, TIF_32BIT))
		return PERF_SAMPLE_REGS_ABI_64;
	else
#endif
	return PERF_SAMPLE_REGS_ABI_32;
}

void perf_get_regs_user(struct perf_regs *regs_user,
			struct pt_regs *regs)
{
	regs_user->regs = task_pt_regs(current);
	regs_user->abi = (regs_user->regs) ? perf_reg_abi(current) :
			 PERF_SAMPLE_REGS_ABI_NONE;
}
