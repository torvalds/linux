/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_FPU_SCHED_H
#define _ASM_X86_FPU_SCHED_H

#include <linux/sched.h>

#include <asm/cpufeature.h>
#include <asm/fpu/types.h>

#include <asm/trace/fpu.h>

extern void save_fpregs_to_fpstate(struct fpu *fpu);
extern void fpu__drop(struct task_struct *tsk);
extern int  fpu_clone(struct task_struct *dst, u64 clone_flags, bool minimal,
		      unsigned long shstk_addr);
extern void fpu_flush_thread(void);

/*
 * FPU state switching for scheduling.
 *
 * switch_fpu() saves the old state and sets TIF_NEED_FPU_LOAD if
 * TIF_NEED_FPU_LOAD is not set.  This is done within the context
 * of the old process.
 *
 * Once TIF_NEED_FPU_LOAD is set, it is required to load the
 * registers before returning to userland or using the content
 * otherwise.
 *
 * The FPU context is only stored/restored for a user task and
 * PF_KTHREAD is used to distinguish between kernel and user threads.
 */
static inline void switch_fpu(struct task_struct *old, int cpu)
{
	if (!test_tsk_thread_flag(old, TIF_NEED_FPU_LOAD) &&
	    cpu_feature_enabled(X86_FEATURE_FPU) &&
	    !(old->flags & (PF_KTHREAD | PF_USER_WORKER))) {
		struct fpu *old_fpu = x86_task_fpu(old);

		set_tsk_thread_flag(old, TIF_NEED_FPU_LOAD);
		save_fpregs_to_fpstate(old_fpu);
		/*
		 * The save operation preserved register state, so the
		 * fpu_fpregs_owner_ctx is still @old_fpu. Store the
		 * current CPU number in @old_fpu, so the next return
		 * to user space can avoid the FPU register restore
		 * when is returns on the same CPU and still owns the
		 * context. See fpregs_restore_userregs().
		 */
		old_fpu->last_cpu = cpu;

		trace_x86_fpu_regs_deactivated(old_fpu);
	}
}

#endif /* _ASM_X86_FPU_SCHED_H */
