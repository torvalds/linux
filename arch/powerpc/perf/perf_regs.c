/*
 * Copyright 2016 Anju T, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
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

#define PT_REGS_OFFSET(id, r) [id] = offsetof(struct pt_regs, r)

#define REG_RESERVED (~((1ULL << PERF_REG_POWERPC_MAX) - 1))

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
};

u64 perf_reg_value(struct pt_regs *regs, int idx)
{
	if (WARN_ON_ONCE(idx >= PERF_REG_POWERPC_MAX))
		return 0;

	if (idx == PERF_REG_POWERPC_SIER &&
	   (IS_ENABLED(CONFIG_FSL_EMB_PERF_EVENT) ||
	    IS_ENABLED(CONFIG_PPC32) ||
	    !is_sier_available()))
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
			struct pt_regs *regs,
			struct pt_regs *regs_user_copy)
{
	regs_user->regs = task_pt_regs(current);
	regs_user->abi = (regs_user->regs) ? perf_reg_abi(current) :
			 PERF_SAMPLE_REGS_ABI_NONE;
}
