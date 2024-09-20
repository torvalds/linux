/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_OPENRISC_FPU_H
#define __ASM_OPENRISC_FPU_H

struct task_struct;

#ifdef CONFIG_FPU
static inline void save_fpu(struct task_struct *task)
{
	task->thread.fpcsr = mfspr(SPR_FPCSR);
}

static inline void restore_fpu(struct task_struct *task)
{
	mtspr(SPR_FPCSR, task->thread.fpcsr);
}
#else
#define save_fpu(tsk)			do { } while (0)
#define restore_fpu(tsk)		do { } while (0)
#endif

#endif /* __ASM_OPENRISC_FPU_H */
