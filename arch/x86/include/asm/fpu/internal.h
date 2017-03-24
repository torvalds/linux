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

#include <asm/user.h>
#include <asm/fpu/api.h>
#include <asm/fpu/xstate.h>
#include <asm/cpufeature.h>
#include <asm/trace/fpu.h>

/*
 * High level FPU state handling functions:
 */
extern void fpu__activate_curr(struct fpu *fpu);
extern void fpu__activate_fpstate_read(struct fpu *fpu);
extern void fpu__activate_fpstate_write(struct fpu *fpu);
extern void fpu__current_fpstate_write_begin(void);
extern void fpu__current_fpstate_write_end(void);
extern void fpu__save(struct fpu *fpu);
extern void fpu__restore(struct fpu *fpu);
extern int  fpu__restore_sig(void __user *buf, int ia32_frame);
extern void fpu__drop(struct fpu *fpu);
extern int  fpu__copy(struct fpu *dst_fpu, struct fpu *src_fpu);
extern void fpu__clear(struct fpu *fpu);
extern int  fpu__exception_code(struct fpu *fpu, int trap_nr);
extern int  dump_fpu(struct pt_regs *ptregs, struct user_i387_struct *fpstate);

/*
 * Boot time FPU initialization functions:
 */
extern void fpu__init_cpu(void);
extern void fpu__init_system_xstate(void);
extern void fpu__init_cpu_xstate(void);
extern void fpu__init_system(struct cpuinfo_x86 *c);
extern void fpu__init_check_bugs(void);
extern void fpu__resume_cpu(void);
extern u64 fpu__get_supported_xfeatures_mask(void);

/*
 * Debugging facility:
 */
#ifdef CONFIG_X86_DEBUG_FPU
# define WARN_ON_FPU(x) WARN_ON_ONCE(x)
#else
# define WARN_ON_FPU(x) ({ (void)(x); 0; })
#endif

/*
 * FPU related CPU feature flag helper routines:
 */
static __always_inline __pure bool use_xsaveopt(void)
{
	return static_cpu_has(X86_FEATURE_XSAVEOPT);
}

static __always_inline __pure bool use_xsave(void)
{
	return static_cpu_has(X86_FEATURE_XSAVE);
}

static __always_inline __pure bool use_fxsr(void)
{
	return static_cpu_has(X86_FEATURE_FXSR);
}

/*
 * fpstate handling functions:
 */

extern union fpregs_state init_fpstate;

extern void fpstate_init(union fpregs_state *state);
#ifdef CONFIG_MATH_EMULATION
extern void fpstate_init_soft(struct swregs_state *soft);
#else
static inline void fpstate_init_soft(struct swregs_state *soft) {}
#endif

static inline void fpstate_init_xstate(struct xregs_state *xsave)
{
	/*
	 * XRSTORS requires these bits set in xcomp_bv, or it will
	 * trigger #GP:
	 */
	xsave->header.xcomp_bv = XCOMP_BV_COMPACTED_FORMAT | xfeatures_mask;
}

static inline void fpstate_init_fxstate(struct fxregs_state *fx)
{
	fx->cwd = 0x37f;
	fx->mxcsr = MXCSR_DEFAULT;
}
extern void fpstate_sanitize_xstate(struct fpu *fpu);

#define user_insn(insn, output, input...)				\
({									\
	int err;							\
	asm volatile(ASM_STAC "\n"					\
		     "1:" #insn "\n\t"					\
		     "2: " ASM_CLAC "\n"				\
		     ".section .fixup,\"ax\"\n"				\
		     "3:  movl $-1,%[err]\n"				\
		     "    jmp  2b\n"					\
		     ".previous\n"					\
		     _ASM_EXTABLE(1b, 3b)				\
		     : [err] "=r" (err), output				\
		     : "0"(0), input);					\
	err;								\
})

#define check_insn(insn, output, input...)				\
({									\
	int err;							\
	asm volatile("1:" #insn "\n\t"					\
		     "2:\n"						\
		     ".section .fixup,\"ax\"\n"				\
		     "3:  movl $-1,%[err]\n"				\
		     "    jmp  2b\n"					\
		     ".previous\n"					\
		     _ASM_EXTABLE(1b, 3b)				\
		     : [err] "=r" (err), output				\
		     : "0"(0), input);					\
	err;								\
})

static inline int copy_fregs_to_user(struct fregs_state __user *fx)
{
	return user_insn(fnsave %[fx]; fwait,  [fx] "=m" (*fx), "m" (*fx));
}

static inline int copy_fxregs_to_user(struct fxregs_state __user *fx)
{
	if (IS_ENABLED(CONFIG_X86_32))
		return user_insn(fxsave %[fx], [fx] "=m" (*fx), "m" (*fx));
	else if (IS_ENABLED(CONFIG_AS_FXSAVEQ))
		return user_insn(fxsaveq %[fx], [fx] "=m" (*fx), "m" (*fx));

	/* See comment in copy_fxregs_to_kernel() below. */
	return user_insn(rex64/fxsave (%[fx]), "=m" (*fx), [fx] "R" (fx));
}

static inline void copy_kernel_to_fxregs(struct fxregs_state *fx)
{
	int err;

	if (IS_ENABLED(CONFIG_X86_32)) {
		err = check_insn(fxrstor %[fx], "=m" (*fx), [fx] "m" (*fx));
	} else {
		if (IS_ENABLED(CONFIG_AS_FXSAVEQ)) {
			err = check_insn(fxrstorq %[fx], "=m" (*fx), [fx] "m" (*fx));
		} else {
			/* See comment in copy_fxregs_to_kernel() below. */
			err = check_insn(rex64/fxrstor (%[fx]), "=m" (*fx), [fx] "R" (fx), "m" (*fx));
		}
	}
	/* Copying from a kernel buffer to FPU registers should never fail: */
	WARN_ON_FPU(err);
}

static inline int copy_user_to_fxregs(struct fxregs_state __user *fx)
{
	if (IS_ENABLED(CONFIG_X86_32))
		return user_insn(fxrstor %[fx], "=m" (*fx), [fx] "m" (*fx));
	else if (IS_ENABLED(CONFIG_AS_FXSAVEQ))
		return user_insn(fxrstorq %[fx], "=m" (*fx), [fx] "m" (*fx));

	/* See comment in copy_fxregs_to_kernel() below. */
	return user_insn(rex64/fxrstor (%[fx]), "=m" (*fx), [fx] "R" (fx),
			  "m" (*fx));
}

static inline void copy_kernel_to_fregs(struct fregs_state *fx)
{
	int err = check_insn(frstor %[fx], "=m" (*fx), [fx] "m" (*fx));

	WARN_ON_FPU(err);
}

static inline int copy_user_to_fregs(struct fregs_state __user *fx)
{
	return user_insn(frstor %[fx], "=m" (*fx), [fx] "m" (*fx));
}

static inline void copy_fxregs_to_kernel(struct fpu *fpu)
{
	if (IS_ENABLED(CONFIG_X86_32))
		asm volatile( "fxsave %[fx]" : [fx] "=m" (fpu->state.fxsave));
	else if (IS_ENABLED(CONFIG_AS_FXSAVEQ))
		asm volatile("fxsaveq %[fx]" : [fx] "=m" (fpu->state.fxsave));
	else {
		/* Using "rex64; fxsave %0" is broken because, if the memory
		 * operand uses any extended registers for addressing, a second
		 * REX prefix will be generated (to the assembler, rex64
		 * followed by semicolon is a separate instruction), and hence
		 * the 64-bitness is lost.
		 *
		 * Using "fxsaveq %0" would be the ideal choice, but is only
		 * supported starting with gas 2.16.
		 *
		 * Using, as a workaround, the properly prefixed form below
		 * isn't accepted by any binutils version so far released,
		 * complaining that the same type of prefix is used twice if
		 * an extended register is needed for addressing (fix submitted
		 * to mainline 2005-11-21).
		 *
		 *  asm volatile("rex64/fxsave %0" : "=m" (fpu->state.fxsave));
		 *
		 * This, however, we can work around by forcing the compiler to
		 * select an addressing mode that doesn't require extended
		 * registers.
		 */
		asm volatile( "rex64/fxsave (%[fx])"
			     : "=m" (fpu->state.fxsave)
			     : [fx] "R" (&fpu->state.fxsave));
	}
}

/* These macros all use (%edi)/(%rdi) as the single memory argument. */
#define XSAVE		".byte " REX_PREFIX "0x0f,0xae,0x27"
#define XSAVEOPT	".byte " REX_PREFIX "0x0f,0xae,0x37"
#define XSAVES		".byte " REX_PREFIX "0x0f,0xc7,0x2f"
#define XRSTOR		".byte " REX_PREFIX "0x0f,0xae,0x2f"
#define XRSTORS		".byte " REX_PREFIX "0x0f,0xc7,0x1f"

#define XSTATE_OP(op, st, lmask, hmask, err)				\
	asm volatile("1:" op "\n\t"					\
		     "xor %[err], %[err]\n"				\
		     "2:\n\t"						\
		     ".pushsection .fixup,\"ax\"\n\t"			\
		     "3: movl $-2,%[err]\n\t"				\
		     "jmp 2b\n\t"					\
		     ".popsection\n\t"					\
		     _ASM_EXTABLE(1b, 3b)				\
		     : [err] "=r" (err)					\
		     : "D" (st), "m" (*st), "a" (lmask), "d" (hmask)	\
		     : "memory")

/*
 * If XSAVES is enabled, it replaces XSAVEOPT because it supports a compact
 * format and supervisor states in addition to modified optimization in
 * XSAVEOPT.
 *
 * Otherwise, if XSAVEOPT is enabled, XSAVEOPT replaces XSAVE because XSAVEOPT
 * supports modified optimization which is not supported by XSAVE.
 *
 * We use XSAVE as a fallback.
 *
 * The 661 label is defined in the ALTERNATIVE* macros as the address of the
 * original instruction which gets replaced. We need to use it here as the
 * address of the instruction where we might get an exception at.
 */
#define XSTATE_XSAVE(st, lmask, hmask, err)				\
	asm volatile(ALTERNATIVE_2(XSAVE,				\
				   XSAVEOPT, X86_FEATURE_XSAVEOPT,	\
				   XSAVES,   X86_FEATURE_XSAVES)	\
		     "\n"						\
		     "xor %[err], %[err]\n"				\
		     "3:\n"						\
		     ".pushsection .fixup,\"ax\"\n"			\
		     "4: movl $-2, %[err]\n"				\
		     "jmp 3b\n"						\
		     ".popsection\n"					\
		     _ASM_EXTABLE(661b, 4b)				\
		     : [err] "=r" (err)					\
		     : "D" (st), "m" (*st), "a" (lmask), "d" (hmask)	\
		     : "memory")

/*
 * Use XRSTORS to restore context if it is enabled. XRSTORS supports compact
 * XSAVE area format.
 */
#define XSTATE_XRESTORE(st, lmask, hmask, err)				\
	asm volatile(ALTERNATIVE(XRSTOR,				\
				 XRSTORS, X86_FEATURE_XSAVES)		\
		     "\n"						\
		     "xor %[err], %[err]\n"				\
		     "3:\n"						\
		     ".pushsection .fixup,\"ax\"\n"			\
		     "4: movl $-2, %[err]\n"				\
		     "jmp 3b\n"						\
		     ".popsection\n"					\
		     _ASM_EXTABLE(661b, 4b)				\
		     : [err] "=r" (err)					\
		     : "D" (st), "m" (*st), "a" (lmask), "d" (hmask)	\
		     : "memory")

/*
 * This function is called only during boot time when x86 caps are not set
 * up and alternative can not be used yet.
 */
static inline void copy_xregs_to_kernel_booting(struct xregs_state *xstate)
{
	u64 mask = -1;
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err;

	WARN_ON(system_state != SYSTEM_BOOTING);

	if (static_cpu_has(X86_FEATURE_XSAVES))
		XSTATE_OP(XSAVES, xstate, lmask, hmask, err);
	else
		XSTATE_OP(XSAVE, xstate, lmask, hmask, err);

	/* We should never fault when copying to a kernel buffer: */
	WARN_ON_FPU(err);
}

/*
 * This function is called only during boot time when x86 caps are not set
 * up and alternative can not be used yet.
 */
static inline void copy_kernel_to_xregs_booting(struct xregs_state *xstate)
{
	u64 mask = -1;
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err;

	WARN_ON(system_state != SYSTEM_BOOTING);

	if (static_cpu_has(X86_FEATURE_XSAVES))
		XSTATE_OP(XRSTORS, xstate, lmask, hmask, err);
	else
		XSTATE_OP(XRSTOR, xstate, lmask, hmask, err);

	/* We should never fault when copying from a kernel buffer: */
	WARN_ON_FPU(err);
}

/*
 * Save processor xstate to xsave area.
 */
static inline void copy_xregs_to_kernel(struct xregs_state *xstate)
{
	u64 mask = -1;
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err;

	WARN_ON(!alternatives_patched);

	XSTATE_XSAVE(xstate, lmask, hmask, err);

	/* We should never fault when copying to a kernel buffer: */
	WARN_ON_FPU(err);
}

/*
 * Restore processor xstate from xsave area.
 */
static inline void copy_kernel_to_xregs(struct xregs_state *xstate, u64 mask)
{
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err;

	XSTATE_XRESTORE(xstate, lmask, hmask, err);

	/* We should never fault when copying from a kernel buffer: */
	WARN_ON_FPU(err);
}

/*
 * Save xstate to user space xsave area.
 *
 * We don't use modified optimization because xrstor/xrstors might track
 * a different application.
 *
 * We don't use compacted format xsave area for
 * backward compatibility for old applications which don't understand
 * compacted format of xsave area.
 */
static inline int copy_xregs_to_user(struct xregs_state __user *buf)
{
	int err;

	/*
	 * Clear the xsave header first, so that reserved fields are
	 * initialized to zero.
	 */
	err = __clear_user(&buf->header, sizeof(buf->header));
	if (unlikely(err))
		return -EFAULT;

	stac();
	XSTATE_OP(XSAVE, buf, -1, -1, err);
	clac();

	return err;
}

/*
 * Restore xstate from user space xsave area.
 */
static inline int copy_user_to_xregs(struct xregs_state __user *buf, u64 mask)
{
	struct xregs_state *xstate = ((__force struct xregs_state *)buf);
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err;

	stac();
	XSTATE_OP(XRSTOR, xstate, lmask, hmask, err);
	clac();

	return err;
}

/*
 * These must be called with preempt disabled. Returns
 * 'true' if the FPU state is still intact and we can
 * keep registers active.
 *
 * The legacy FNSAVE instruction cleared all FPU state
 * unconditionally, so registers are essentially destroyed.
 * Modern FPU state can be kept in registers, if there are
 * no pending FP exceptions.
 */
static inline int copy_fpregs_to_fpstate(struct fpu *fpu)
{
	if (likely(use_xsave())) {
		copy_xregs_to_kernel(&fpu->state.xsave);
		return 1;
	}

	if (likely(use_fxsr())) {
		copy_fxregs_to_kernel(fpu);
		return 1;
	}

	/*
	 * Legacy FPU register saving, FNSAVE always clears FPU registers,
	 * so we have to mark them inactive:
	 */
	asm volatile("fnsave %[fp]; fwait" : [fp] "=m" (fpu->state.fsave));

	return 0;
}

static inline void __copy_kernel_to_fpregs(union fpregs_state *fpstate)
{
	if (use_xsave()) {
		copy_kernel_to_xregs(&fpstate->xsave, -1);
	} else {
		if (use_fxsr())
			copy_kernel_to_fxregs(&fpstate->fxsave);
		else
			copy_kernel_to_fregs(&fpstate->fsave);
	}
}

static inline void copy_kernel_to_fpregs(union fpregs_state *fpstate)
{
	/*
	 * AMD K7/K8 CPUs don't save/restore FDP/FIP/FOP unless an exception is
	 * pending. Clear the x87 state here by setting it to fixed values.
	 * "m" is a random variable that should be in L1.
	 */
	if (unlikely(static_cpu_has_bug(X86_BUG_FXSAVE_LEAK))) {
		asm volatile(
			"fnclex\n\t"
			"emms\n\t"
			"fildl %P[addr]"	/* set F?P to defined value */
			: : [addr] "m" (fpstate));
	}

	__copy_kernel_to_fpregs(fpstate);
}

extern int copy_fpstate_to_sigframe(void __user *buf, void __user *fp, int size);

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
	return fpu == this_cpu_read_stable(fpu_fpregs_owner_ctx) && cpu == fpu->last_cpu;
}

/*
 * These generally need preemption protection to work,
 * do try to avoid using these on their own:
 */
static inline void fpregs_deactivate(struct fpu *fpu)
{
	WARN_ON_FPU(!fpu->fpregs_active);

	fpu->fpregs_active = 0;
	this_cpu_write(fpu_fpregs_owner_ctx, NULL);
	trace_x86_fpu_regs_deactivated(fpu);
}

static inline void fpregs_activate(struct fpu *fpu)
{
	WARN_ON_FPU(fpu->fpregs_active);

	fpu->fpregs_active = 1;
	this_cpu_write(fpu_fpregs_owner_ctx, fpu);
	trace_x86_fpu_regs_activated(fpu);
}

/*
 * The question "does this thread have fpu access?"
 * is slightly racy, since preemption could come in
 * and revoke it immediately after the test.
 *
 * However, even in that very unlikely scenario,
 * we can just assume we have FPU access - typically
 * to save the FP state - we'll just take a #NM
 * fault and get the FPU access back.
 */
static inline int fpregs_active(void)
{
	return current->thread.fpu.fpregs_active;
}

/*
 * FPU state switching for scheduling.
 *
 * This is a two-stage process:
 *
 *  - switch_fpu_prepare() saves the old state.
 *    This is done within the context of the old process.
 *
 *  - switch_fpu_finish() restores the new state as
 *    necessary.
 */
static inline void
switch_fpu_prepare(struct fpu *old_fpu, int cpu)
{
	if (old_fpu->fpregs_active) {
		if (!copy_fpregs_to_fpstate(old_fpu))
			old_fpu->last_cpu = -1;
		else
			old_fpu->last_cpu = cpu;

		/* But leave fpu_fpregs_owner_ctx! */
		old_fpu->fpregs_active = 0;
		trace_x86_fpu_regs_deactivated(old_fpu);
	} else
		old_fpu->last_cpu = -1;
}

/*
 * Misc helper functions:
 */

/*
 * Set up the userspace FPU context for the new task, if the task
 * has used the FPU.
 */
static inline void switch_fpu_finish(struct fpu *new_fpu, int cpu)
{
	bool preload = static_cpu_has(X86_FEATURE_FPU) &&
		       new_fpu->fpstate_active;

	if (preload) {
		if (!fpregs_state_valid(new_fpu, cpu))
			copy_kernel_to_fpregs(&new_fpu->state);
		fpregs_activate(new_fpu);
	}
}

/*
 * Needs to be preemption-safe.
 *
 * NOTE! user_fpu_begin() must be used only immediately before restoring
 * the save state. It does not do any saving/restoring on its own. In
 * lazy FPU mode, it is just an optimization to avoid a #NM exception,
 * the task can lose the FPU right after preempt_enable().
 */
static inline void user_fpu_begin(void)
{
	struct fpu *fpu = &current->thread.fpu;

	preempt_disable();
	if (!fpregs_active())
		fpregs_activate(fpu);
	preempt_enable();
}

/*
 * MXCSR and XCR definitions:
 */

extern unsigned int mxcsr_feature_mask;

#define XCR_XFEATURE_ENABLED_MASK	0x00000000

static inline u64 xgetbv(u32 index)
{
	u32 eax, edx;

	asm volatile(".byte 0x0f,0x01,0xd0" /* xgetbv */
		     : "=a" (eax), "=d" (edx)
		     : "c" (index));
	return eax + ((u64)edx << 32);
}

static inline void xsetbv(u32 index, u64 value)
{
	u32 eax = value;
	u32 edx = value >> 32;

	asm volatile(".byte 0x0f,0x01,0xd1" /* xsetbv */
		     : : "a" (eax), "d" (edx), "c" (index));
}

#endif /* _ASM_X86_FPU_INTERNAL_H */
