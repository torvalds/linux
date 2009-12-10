#ifndef __ASM_SH_FPU_H
#define __ASM_SH_FPU_H

#ifndef __ASSEMBLY__
#include <linux/preempt.h>
#include <asm/ptrace.h>

#ifdef CONFIG_SH_FPU
static inline void release_fpu(struct pt_regs *regs)
{
	regs->sr |= SR_FD;
}

static inline void grab_fpu(struct pt_regs *regs)
{
	regs->sr &= ~SR_FD;
}

struct task_struct;

extern void save_fpu(struct task_struct *__tsk);
void fpu_state_restore(struct pt_regs *regs);
#else

#define save_fpu(tsk)		do { } while (0)
#define release_fpu(regs)	do { } while (0)
#define grab_fpu(regs)		do { } while (0)
#define fpu_state_restore(regs)	do { } while (0)

#endif

struct user_regset;

extern int do_fpu_inst(unsigned short, struct pt_regs *);

extern int fpregs_get(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      void *kbuf, void __user *ubuf);

static inline void __unlazy_fpu(struct task_struct *tsk, struct pt_regs *regs)
{
	if (task_thread_info(tsk)->status & TS_USEDFPU) {
		task_thread_info(tsk)->status &= ~TS_USEDFPU;
		save_fpu(tsk);
		release_fpu(regs);
	} else
		tsk->fpu_counter = 0;
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

static inline int init_fpu(struct task_struct *tsk)
{
	if (tsk_used_math(tsk)) {
		if ((boot_cpu_data.flags & CPU_HAS_FPU) && tsk == current)
			unlazy_fpu(tsk, task_pt_regs(tsk));
		return 0;
	}

	set_stopped_child_used_math(tsk);
	return 0;
}

#endif /* __ASSEMBLY__ */

#endif /* __ASM_SH_FPU_H */
