/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_FSGSBASE_H
#define _ASM_FSGSBASE_H

#ifndef __ASSEMBLY__

#ifdef CONFIG_X86_64

#include <asm/msr-index.h>

/*
 * Read/write a task's FSBASE or GSBASE. This returns the value that
 * the FS/GS base would have (if the task were to be resumed). These
 * work on the current task or on a non-running (typically stopped
 * ptrace child) task.
 */
extern unsigned long x86_fsbase_read_task(struct task_struct *task);
extern unsigned long x86_gsbase_read_task(struct task_struct *task);
extern void x86_fsbase_write_task(struct task_struct *task, unsigned long fsbase);
extern void x86_gsbase_write_task(struct task_struct *task, unsigned long gsbase);

/* Helper functions for reading/writing FS/GS base */

static inline unsigned long x86_fsbase_read_cpu(void)
{
	unsigned long fsbase;

	rdmsrl(MSR_FS_BASE, fsbase);

	return fsbase;
}

static inline unsigned long x86_gsbase_read_cpu_inactive(void)
{
	unsigned long gsbase;

	rdmsrl(MSR_KERNEL_GS_BASE, gsbase);

	return gsbase;
}

static inline void x86_fsbase_write_cpu(unsigned long fsbase)
{
	wrmsrl(MSR_FS_BASE, fsbase);
}

static inline void x86_gsbase_write_cpu_inactive(unsigned long gsbase)
{
	wrmsrl(MSR_KERNEL_GS_BASE, gsbase);
}

#endif /* CONFIG_X86_64 */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_FSGSBASE_H */
