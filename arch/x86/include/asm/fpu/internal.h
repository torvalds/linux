/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1994 Linus Torvalds
 *
 * Pentium III FXSR, SSE support
 * General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * x86-64 work by Andi Kleen 2002
 */

#ifndef _ASM_X86_FPU_INTERNAL_H
#define _ASM_X86_FPU_INTERNAL_H

#include <linux/compat.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <asm/user.h>
#include <asm/fpu/api.h>
#include <asm/fpu/xstate.h>
#include <asm/fpu/xcr.h>
#include <asm/cpufeature.h>
#include <asm/trace/fpu.h>

/*
 * High level FPU state handling functions:
 */
extern bool fpu__restore_sig(void __user *buf, int ia32_frame);
extern void fpu__clear_user_states(struct fpu *fpu);
extern int  fpu__exception_code(struct fpu *fpu, int trap_nr);

extern void fpu_sync_fpstate(struct fpu *fpu);

/*
 * Boot time FPU initialization functions:
 */
extern void fpu__init_cpu(void);
extern void fpu__init_system_xstate(void);
extern void fpu__init_cpu_xstate(void);
extern void fpu__init_system(struct cpuinfo_x86 *c);
extern void fpu__init_check_bugs(void);
extern void fpu__resume_cpu(void);

extern union fpregs_state init_fpstate;
extern void fpstate_init_user(union fpregs_state *state);

#ifdef CONFIG_MATH_EMULATION
extern void fpstate_init_soft(struct swregs_state *soft);
#else
static inline void fpstate_init_soft(struct swregs_state *soft) {}
#endif

extern void restore_fpregs_from_fpstate(union fpregs_state *fpstate, u64 mask);

extern bool copy_fpstate_to_sigframe(void __user *buf, void __user *fp, int size);

/*
 * FPU context switch related helper methods:
 */

DECLARE_PER_CPU(struct fpu *, fpu_fpregs_owner_ctx);

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

/*
 * These generally need preemption protection to work,
 * do try to avoid using these on their own:
 */
static inline void fpregs_deactivate(struct fpu *fpu)
{
	this_cpu_write(fpu_fpregs_owner_ctx, NULL);
	trace_x86_fpu_regs_deactivated(fpu);
}

static inline void fpregs_activate(struct fpu *fpu)
{
	this_cpu_write(fpu_fpregs_owner_ctx, fpu);
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
		u64 mask;

		/*
		 * This restores _all_ xstate which has not been
		 * established yet.
		 *
		 * If PKRU is enabled, then the PKRU value is already
		 * correct because it was either set in switch_to() or in
		 * flush_thread(). So it is excluded because it might be
		 * not up to date in current->thread.fpu.xsave state.
		 */
		mask = xfeatures_mask_restore_user() |
			xfeatures_mask_supervisor();
		restore_fpregs_from_fpstate(&fpu->state, mask);

		fpregs_activate(fpu);
		fpu->last_cpu = cpu;
	}
	clear_thread_flag(TIF_NEED_FPU_LOAD);
}

#endif /* _ASM_X86_FPU_INTERNAL_H */
