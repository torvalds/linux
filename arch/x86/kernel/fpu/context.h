/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __X86_KERNEL_FPU_CONTEXT_H
#define __X86_KERNEL_FPU_CONTEXT_H

#include <asm/fpu/xstate.h>
#include <asm/trace/fpu.h>

/* Functions related to FPU context tracking */

/*
 * The in-register FPU state for an FPU context on a CPU is assumed to be
 * valid if the fpu->last_cpu matches the CPU, and the fpu_fpregs_owner_ctx
 * matches the FPU.
 *
 * If the FPU register state is valid, the kernel can skip restoring the
 * FPU state from memory.
 *
 * Any code that clobbers the FPU registers or updates the in-memory
 * FPU state for a task MUST let the rest of the kernel know that the
 * FPU registers are no longer valid for this task.
 *
 * Either one of these invalidation functions is enough. Invalidate
 * a resource you control: CPU if using the CPU for something else
 * (with preemption disabled), FPU for the current task, or a task that
 * is prevented from running by the current task.
 */
static inline void __cpu_invalidate_fpregs_state(void)
{
	__this_cpu_write(fpu_fpregs_owner_ctx, NULL);
}

static inline void __fpu_invalidate_fpregs_state(struct fpu *fpu)
{
	fpu->last_cpu = -1;
}

static inline int fpregs_state_valid(struct fpu *fpu, unsigned int cpu)
{
	return fpu == this_cpu_read(fpu_fpregs_owner_ctx) && cpu == fpu->last_cpu;
}

static inline void fpregs_deactivate(struct fpu *fpu)
{
	__this_cpu_write(fpu_fpregs_owner_ctx, NULL);
	trace_x86_fpu_regs_deactivated(fpu);
}

static inline void fpregs_activate(struct fpu *fpu)
{
	__this_cpu_write(fpu_fpregs_owner_ctx, fpu);
	trace_x86_fpu_regs_activated(fpu);
}

/* Internal helper for switch_fpu_return() and signal frame setup */
static inline void fpregs_restore_userregs(void)
{
	struct fpu *fpu = &current->thread.fpu;
	int cpu = smp_processor_id();

	if (WARN_ON_ONCE(current->flags & PF_KTHREAD))
		return;

	if (!fpregs_state_valid(fpu, cpu)) {
		/*
		 * This restores _all_ xstate which has not been
		 * established yet.
		 *
		 * If PKRU is enabled, then the PKRU value is already
		 * correct because it was either set in switch_to() or in
		 * flush_thread(). So it is excluded because it might be
		 * not up to date in current->thread.fpu.xsave state.
		 *
		 * XFD state is handled in restore_fpregs_from_fpstate().
		 */
		restore_fpregs_from_fpstate(fpu->fpstate, XFEATURE_MASK_FPSTATE);

		fpregs_activate(fpu);
		fpu->last_cpu = cpu;
	}
	clear_thread_flag(TIF_NEED_FPU_LOAD);
}

#endif
