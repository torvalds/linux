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

/* This value will be assigned to 0 during resume and will be used by
 * hibernation core for the subsequent resume sequence
 */
extern int in_suspend;

/* Low-level CPU suspend entry function */
int __cpu_suspend_enter(struct suspend_context *context);

/* High-level CPU suspend which will save context and call finish() */
int cpu_suspend(unsigned long arg,
		int (*finish)(unsigned long arg,
			      unsigned long entry,
			      unsigned long context));

/* Low-level CPU resume entry function */
int __cpu_resume_enter(unsigned long hartid, unsigned long context);

/* Low-level API to support hibernation */
int swsusp_arch_suspend(void);
int swsusp_arch_resume(void);
int arch_hibernation_header_save(void *addr, unsigned int max_size);
int arch_hibernation_header_restore(void *addr);
int __hibernate_cpu_resume(unsigned long context);

/* Used to resume on the CPU we hibernated on */
int hibernate_resume_nonboot_cpu_disable(void);

/* Used to save and restore the csr */
void suspend_save_csrs(struct suspend_context *context);
void suspend_restore_csrs(struct suspend_context *context);

asmlinkage void restore_image(unsigned long resume_satp, unsigned long satp_temp,
				unsigned long cpu_resume, unsigned long resume_context);
asmlinkage int core_restore_code(void);
#endif
