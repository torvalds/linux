/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Synopsys, Inc. (www.synopsys.com)
 *
 */

#ifndef _ASM_ARC_FPU_H
#define _ASM_ARC_FPU_H

#ifdef CONFIG_ARC_FPU_SAVE_RESTORE

#include <asm/ptrace.h>

/* These DPFP regs need to be saved/restored across ctx-sw */
struct arc_fpu {
	struct {
		unsigned int l, h;
	} aux_dpfp[2];
};

extern void fpu_save_restore(struct task_struct *p, struct task_struct *n);

#else

#define fpu_save_restore(p, n)

#endif	/* CONFIG_ARC_FPU_SAVE_RESTORE */

#endif	/* _ASM_ARC_FPU_H */
