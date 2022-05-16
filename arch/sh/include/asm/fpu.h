/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_FPU_H
#define __ASM_SH_FPU_H

#ifndef __ASSEMBLY__

#include <asm/ptrace.h>

struct task_struct;

#ifdef CONFIG_SH_FPU
static inline void release_fpu(struct pt_regs *regs)
{
	regs->sr |= SR_FD;
}

static inline void grab_fpu(struct pt_regs *regs)
{
	regs->sr &= ~SR_FD;
}

extern void save_fpu(struct task_struct *__tsk);
extern void restore_fpu(struct task_struct *__tsk);
extern void fpu_state_restore(struct pt_regs *regs);
extern void __fpu_state_restore(void);
#else
#define save_fpu(tsk)			do { } while (0)
#define restore_fpu(tsk)		do { } while (0)
#define release_fpu(regs)		do { } while (0)
#define grab_fpu(regs)			do { } while (0)
#define fpu_state_restore(regs)		do { } while (0)
#define __fpu_state_restore(regs)	do { } while (0)
#endif

struct user_regset;

extern int do_fpu_inst(unsigned short, struct pt_regs *);
extern int init_fpu(struct task_struct *);

static inline void __unlazy_fpu(struct task_struct *tsk, struct pt_regs *regs)
{
	if (task_thread_info(tsk)->status & TS_USEDFPU) {
		task_thread_info(tsk)->status &= ~TS_USEDFPU;
		save_fpu(tsk);
		release_fpu(regs);
	} else
		tsk->thread.fpu_counter = 0;
}

static inline void unlazy_fpu(struct task_struct *tsk, struct pt_regs *regs)
{
	preempt_disable();
	__unlazy_fpu(tsk, regs);
	preempt_enable();
}

static inline void clear_fpu(struct task_struct *tsk, struct pt_regs *regs)
{
	preempt_disable();
	if (task_thread_info(tsk)->status & TS_USEDFPU) {
		task_thread_info(tsk)->status &= ~TS_USEDFPU;
		release_fpu(regs);
	}
	preempt_enable();
}

#endif /* __ASSEMBLY__ */

#endif /* __ASM_SH_FPU_H */
