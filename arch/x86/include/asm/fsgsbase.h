/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_FSGSBASE_H
#define _ASM_FSGSBASE_H 1

#ifndef __ASSEMBLY__

#ifdef CONFIG_X86_64

#include <asm/msr-index.h>

/*
 * Read/write a task's fsbase or gsbase. This returns the value that
 * the FS/GS base would have (if the task were to be resumed). These
 * work on current or on a different non-running task.
 */
unsigned long x86_fsbase_read_task(struct task_struct *task);
unsigned long x86_gsbase_read_task(struct task_struct *task);
int x86_fsbase_write_task(struct task_struct *task, unsigned long fsbase);
int x86_gsbase_write_task(struct task_struct *task, unsigned long gsbase);

/* Helper functions for reading/writing FS/GS base */

static inline unsigned long x86_fsbase_read_cpu(void)
{
	unsigned long fsbase;

	rdmsrl(MSR_FS_BASE, fsbase);
	return fsbase;
}

void x86_fsbase_write_cpu(unsigned long fsbase);

static inline unsigned long x86_gsbase_read_cpu_inactive(void)
{
	unsigned long gsbase;

	rdmsrl(MSR_KERNEL_GS_BASE, gsbase);
	return gsbase;
}

void x86_gsbase_write_cpu_inactive(unsigned long gsbase);

#endif /* CONFIG_X86_64 */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_FSGSBASE_H */
