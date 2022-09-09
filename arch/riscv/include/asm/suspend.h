/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#ifndef _ASM_RISCV_SUSPEND_H
#define _ASM_RISCV_SUSPEND_H

#include <asm/ptrace.h>

struct suspend_context {
	/* Saved and restored by low-level functions */
	struct pt_regs regs;
	/* Saved and restored by high-level functions */
	unsigned long scratch;
	unsigned long tvec;
	unsigned long ie;
#ifdef CONFIG_MMU
	unsigned long satp;
#endif
};

/* Low-level CPU suspend entry function */
int __cpu_suspend_enter(struct suspend_context *context);

/* High-level CPU suspend which will save context and call finish() */
int cpu_suspend(unsigned long arg,
		int (*finish)(unsigned long arg,
			      unsigned long entry,
			      unsigned long context));

/* Low-level CPU resume entry function */
int __cpu_resume_enter(unsigned long hartid, unsigned long context);

#endif
