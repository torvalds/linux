/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Synopsys, Inc. (www.synopsys.com)
 *
 */

#ifndef _ASM_ARC_FPU_H
#define _ASM_ARC_FPU_H

#ifdef CONFIG_ARC_FPU_SAVE_RESTORE

#include <asm/ptrace.h>

#ifdef CONFIG_ISA_ARCOMPACT

/* These DPFP regs need to be saved/restored across ctx-sw */
struct arc_fpu {
	struct {
		unsigned int l, h;
	} aux_dpfp[2];
};

#define fpu_init_task(regs)

#else

/*
 * ARCv2 FPU Control aux register
 *   - bits to enable Traps on Exceptions
 *   - Rounding mode
 *
 * ARCv2 FPU Status aux register
 *   - FPU exceptions flags (Inv, Div-by-Zero, overflow, underflow, inexact)
 *   - Flag Write Enable to clear flags explicitly (vs. by fpu instructions
 *     only
 */

struct arc_fpu {
	unsigned int ctrl, status;
};

extern void fpu_init_task(struct pt_regs *regs);

#endif	/* !CONFIG_ISA_ARCOMPACT */

extern void fpu_save_restore(struct task_struct *p, struct task_struct *n);

#else	/* !CONFIG_ARC_FPU_SAVE_RESTORE */

#define fpu_save_restore(p, n)
#define fpu_init_task(regs)

#endif	/* CONFIG_ARC_FPU_SAVE_RESTORE */

#endif	/* _ASM_ARC_FPU_H */
