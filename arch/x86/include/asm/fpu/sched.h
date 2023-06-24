/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_FPU_SCHED_H
#define _ASM_X86_FPU_SCHED_H

#include <linux/sched.h>

#include <asm/cpufeature.h>
#include <asm/fpu/types.h>

#include <asm/trace/fpu.h>

extern void save_fpregs_to_fpstate(struct fpu *fpu);
extern void fpu__drop(struct fpu *fpu);
extern int  fpu_clone(struct task_struct *dst, unsigned long clone_flags, bool minimal);
extern void fpu_flush_thread(void);

/*
 * FPU state switching for scheduling.
 *
 * This is a two-stage process:
 *
 *  - switch_fpu_prepare() saves the old state.
 *    This is done within the context of the old process.
 *
 *  - switch_fpu_finish() sets TIF_NEED_FPU_LOAD; the floating point state
 *    will get loaded on return to userspace, or when the kernel needs it.
 *
 * If TIF_NEED_FPU_LOAD is cleared then the CPU's FPU registers
 * are saved in the current thread's FPU register state.
 *
 * If TIF_NEED_FPU_LOAD is set then CPU's FPU registers may not
 * hold current()'s FPU registers. It is required to load the
 * registers before returning to userland or using the content
 * otherwise.
 *
 * The FPU context is only stored/restored for a user task and
 * PF_KTHREAD is used to distinguish between kernel and user threads.
 */
static inline void switch_fpu_prepare(struct fpu *old_fpu, int cpu)
{
	if (cpu_feature_enabled(X86_FEATURE_FPU) &&
	    !(current->flags & PF_KTHREAD)) {
		save_fpregs_to_fpstate(old_fpu);
		/*
		 * The save operation preserved register state, so the
		 * fpu_fpregs_owner_ctx is still @old_fpu. Store the
		 * current CPU number in @old_fpu, so the next return
		 * to user space can avoid the FPU register restore
		 * when is returns on the same CPU and still owns the
		 * context.
		 */
		old_fpu->last_cpu = cpu;

		trace_x86_fpu_regs_deactivated(old_fpu);
	}
}

/*
 * Delay loading of the complete FPU state until the return to userland.
 * PKRU is handled separately.
 */
static inline void switch_fpu_finish(void)
{
	if (cpu_feature_enabled(X86_FEATURE_FPU))
		set_thread_flag(TIF_NEED_FPU_LOAD);
}

#endif /* _ASM_X86_FPU_SCHED_H */
