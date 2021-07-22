// SPDX-License-Identifier: GPL-2.0-only
/*
 * fpu.c - save/restore of Floating Point Unit Registers on task switch
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/sched.h>
#include <asm/fpu.h>

#ifdef CONFIG_ISA_ARCOMPACT

/*
 * To save/restore FPU regs, simplest scheme would use LR/SR insns.
 * However since SR serializes the pipeline, an alternate "hack" can be used
 * which uses the FPU Exchange insn (DEXCL) to r/w FPU regs.
 *
 * Store to 64bit dpfp1 reg from a pair of core regs:
 *   dexcl1 0, r1, r0  ; where r1:r0 is the 64 bit val
 *
 * Read from dpfp1 into pair of core regs (w/o clobbering dpfp1)
 *   mov_s    r3, 0
 *   daddh11  r1, r3, r3   ; get "hi" into r1 (dpfp1 unchanged)
 *   dexcl1   r0, r1, r3   ; get "low" into r0 (dpfp1 low clobbered)
 *   dexcl1    0, r1, r0   ; restore dpfp1 to orig value
 *
 * However we can tweak the read, so that read-out of outgoing task's FPU regs
 * and write of incoming task's regs happen in one shot. So all the work is
 * done before context switch
 */

void fpu_save_restore(struct task_struct *prev, struct task_struct *next)
{
	unsigned int *saveto = &prev->thread.fpu.aux_dpfp[0].l;
	unsigned int *readfrom = &next->thread.fpu.aux_dpfp[0].l;

	const unsigned int zero = 0;

	__asm__ __volatile__(
		"daddh11  %0, %2, %2\n"
		"dexcl1   %1, %3, %4\n"
		: "=&r" (*(saveto + 1)), /* early clobber must here */
		  "=&r" (*(saveto))
		: "r" (zero), "r" (*(readfrom + 1)), "r" (*(readfrom))
	);

	__asm__ __volatile__(
		"daddh22  %0, %2, %2\n"
		"dexcl2   %1, %3, %4\n"
		: "=&r"(*(saveto + 3)),	/* early clobber must here */
		  "=&r"(*(saveto + 2))
		: "r" (zero), "r" (*(readfrom + 3)), "r" (*(readfrom + 2))
	);
}

#else

void fpu_init_task(struct pt_regs *regs)
{
	/* default rounding mode */
	write_aux_reg(ARC_REG_FPU_CTRL, 0x100);

	/* set "Write enable" to allow explicit write to exception flags */
	write_aux_reg(ARC_REG_FPU_STATUS, 0x80000000);
}

void fpu_save_restore(struct task_struct *prev, struct task_struct *next)
{
	struct arc_fpu *save = &prev->thread.fpu;
	struct arc_fpu *restore = &next->thread.fpu;

	save->ctrl = read_aux_reg(ARC_REG_FPU_CTRL);
	save->status = read_aux_reg(ARC_REG_FPU_STATUS);

	write_aux_reg(ARC_REG_FPU_CTRL, restore->ctrl);
	write_aux_reg(ARC_REG_FPU_STATUS, restore->status);
}

#endif
