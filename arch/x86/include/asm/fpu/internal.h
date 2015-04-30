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

#define	MXCSR_DEFAULT		0x1f80

extern unsigned int mxcsr_feature_mask;

extern void fpu__init_cpu(void);
extern void fpu__init_system_xstate(void);
extern void fpu__init_cpu_xstate(void);
extern void fpu__init_system(struct cpuinfo_x86 *c);

extern void fpu__activate_curr(struct fpu *fpu);

extern void fpstate_init(struct fpu *fpu);
#ifdef CONFIG_MATH_EMULATION
extern void fpstate_init_soft(struct i387_soft_struct *soft);
#else
static inline void fpstate_init_soft(struct i387_soft_struct *soft) {}
#endif
static inline void fpstate_init_fxstate(struct i387_fxsave_struct *fx)
{
	fx->cwd = 0x37f;
	fx->mxcsr = MXCSR_DEFAULT;
}

extern int  dump_fpu(struct pt_regs *, struct user_i387_struct *);
extern int  fpu__exception_code(struct fpu *fpu, int trap_nr);

/*
 * High level FPU state handling functions:
 */
extern void fpu__save(struct fpu *fpu);
extern void fpu__restore(void);
extern int  fpu__restore_sig(void __user *buf, int ia32_frame);
extern void fpu__drop(struct fpu *fpu);
extern int  fpu__copy(struct fpu *dst_fpu, struct fpu *src_fpu);
extern void fpu__clear(struct fpu *fpu);

extern void fpu__init_check_bugs(void);
extern void fpu__resume_cpu(void);

DECLARE_PER_CPU(struct fpu *, fpu_fpregs_owner_ctx);

/*
 * Must be run with preemption disabled: this clears the fpu_fpregs_owner_ctx,
 * on this CPU.
 *
 * This will disable any lazy FPU state restore of the current FPU state,
 * but if the current thread owns the FPU, it will still be saved by.
 */
static inline void __cpu_disable_lazy_restore(unsigned int cpu)
{
	per_cpu(fpu_fpregs_owner_ctx, cpu) = NULL;
}

static inline int fpu_want_lazy_restore(struct fpu *fpu, unsigned int cpu)
{
	return fpu == this_cpu_read_stable(fpu_fpregs_owner_ctx) && cpu == fpu->last_cpu;
}

#define X87_FSW_ES (1 << 7)	/* Exception Summary */

static __always_inline __pure bool use_eager_fpu(void)
{
	return static_cpu_has_safe(X86_FEATURE_EAGER_FPU);
}

static __always_inline __pure bool use_xsaveopt(void)
{
	return static_cpu_has_safe(X86_FEATURE_XSAVEOPT);
}

static __always_inline __pure bool use_xsave(void)
{
	return static_cpu_has_safe(X86_FEATURE_XSAVE);
}

static __always_inline __pure bool use_fxsr(void)
{
	return static_cpu_has_safe(X86_FEATURE_FXSR);
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

static inline int fsave_user(struct i387_fsave_struct __user *fx)
{
	return user_insn(fnsave %[fx]; fwait,  [fx] "=m" (*fx), "m" (*fx));
}

static inline int fxsave_user(struct i387_fxsave_struct __user *fx)
{
	if (config_enabled(CONFIG_X86_32))
		return user_insn(fxsave %[fx], [fx] "=m" (*fx), "m" (*fx));
	else if (config_enabled(CONFIG_AS_FXSAVEQ))
		return user_insn(fxsaveq %[fx], [fx] "=m" (*fx), "m" (*fx));

	/* See comment in fpu_fxsave() below. */
	return user_insn(rex64/fxsave (%[fx]), "=m" (*fx), [fx] "R" (fx));
}

static inline int fxrstor_checking(struct i387_fxsave_struct *fx)
{
	if (config_enabled(CONFIG_X86_32))
		return check_insn(fxrstor %[fx], "=m" (*fx), [fx] "m" (*fx));
	else if (config_enabled(CONFIG_AS_FXSAVEQ))
		return check_insn(fxrstorq %[fx], "=m" (*fx), [fx] "m" (*fx));

	/* See comment in fpu_fxsave() below. */
	return check_insn(rex64/fxrstor (%[fx]), "=m" (*fx), [fx] "R" (fx),
			  "m" (*fx));
}

static inline int fxrstor_user(struct i387_fxsave_struct __user *fx)
{
	if (config_enabled(CONFIG_X86_32))
		return user_insn(fxrstor %[fx], "=m" (*fx), [fx] "m" (*fx));
	else if (config_enabled(CONFIG_AS_FXSAVEQ))
		return user_insn(fxrstorq %[fx], "=m" (*fx), [fx] "m" (*fx));

	/* See comment in fpu_fxsave() below. */
	return user_insn(rex64/fxrstor (%[fx]), "=m" (*fx), [fx] "R" (fx),
			  "m" (*fx));
}

static inline int frstor_checking(struct i387_fsave_struct *fx)
{
	return check_insn(frstor %[fx], "=m" (*fx), [fx] "m" (*fx));
}

static inline int frstor_user(struct i387_fsave_struct __user *fx)
{
	return user_insn(frstor %[fx], "=m" (*fx), [fx] "m" (*fx));
}

static inline void fpu_fxsave(struct fpu *fpu)
{
	if (config_enabled(CONFIG_X86_32))
		asm volatile( "fxsave %[fx]" : [fx] "=m" (fpu->state.fxsave));
	else if (config_enabled(CONFIG_AS_FXSAVEQ))
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
		xsave_state(&fpu->state.xsave);
		return 1;
	}

	if (likely(use_fxsr())) {
		fpu_fxsave(fpu);
		return 1;
	}

	/*
	 * Legacy FPU register saving, FNSAVE always clears FPU registers,
	 * so we have to mark them inactive:
	 */
	asm volatile("fnsave %[fx]; fwait" : [fx] "=m" (fpu->state.fsave));

	return 0;
}

static inline int __copy_fpstate_to_fpregs(struct fpu *fpu)
{
	if (use_xsave())
		return fpu_xrstor_checking(&fpu->state.xsave);
	else if (use_fxsr())
		return fxrstor_checking(&fpu->state.fxsave);
	else
		return frstor_checking(&fpu->state.fsave);
}

static inline int copy_fpstate_to_fpregs(struct fpu *fpu)
{
	/*
	 * AMD K7/K8 CPUs don't save/restore FDP/FIP/FOP unless an exception is
	 * pending. Clear the x87 state here by setting it to fixed values.
	 * "m" is a random variable that should be in L1.
	 */
	if (unlikely(static_cpu_has_bug_safe(X86_BUG_FXSAVE_LEAK))) {
		asm volatile(
			"fnclex\n\t"
			"emms\n\t"
			"fildl %P[addr]"	/* set F?P to defined value */
			: : [addr] "m" (fpu->fpregs_active));
	}

	return __copy_fpstate_to_fpregs(fpu);
}

/*
 * Wrap lazy FPU TS handling in a 'hw fpregs activation/deactivation'
 * idiom, which is then paired with the sw-flag (fpregs_active) later on:
 */

static inline void __fpregs_activate_hw(void)
{
	if (!use_eager_fpu())
		clts();
}

static inline void __fpregs_deactivate_hw(void)
{
	if (!use_eager_fpu())
		stts();
}

/* Must be paired with an 'stts' (fpregs_deactivate_hw()) after! */
static inline void __fpregs_deactivate(struct fpu *fpu)
{
	fpu->fpregs_active = 0;
	this_cpu_write(fpu_fpregs_owner_ctx, NULL);
}

/* Must be paired with a 'clts' (fpregs_activate_hw()) before! */
static inline void __fpregs_activate(struct fpu *fpu)
{
	fpu->fpregs_active = 1;
	this_cpu_write(fpu_fpregs_owner_ctx, fpu);
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
 * Encapsulate the CR0.TS handling together with the
 * software flag.
 *
 * These generally need preemption protection to work,
 * do try to avoid using these on their own.
 */
static inline void fpregs_activate(struct fpu *fpu)
{
	__fpregs_activate_hw();
	__fpregs_activate(fpu);
}

static inline void fpregs_deactivate(struct fpu *fpu)
{
	__fpregs_deactivate(fpu);
	__fpregs_deactivate_hw();
}

static inline void restore_init_xstate(void)
{
	if (use_xsave())
		xrstor_state(&init_xstate_ctx, -1);
	else
		fxrstor_checking(&init_xstate_ctx.i387);
}

/*
 * Definitions for the eXtended Control Register instructions
 */

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

/*
 * FPU state switching for scheduling.
 *
 * This is a two-stage process:
 *
 *  - switch_fpu_prepare() saves the old state and
 *    sets the new state of the CR0.TS bit. This is
 *    done within the context of the old process.
 *
 *  - switch_fpu_finish() restores the new state as
 *    necessary.
 */
typedef struct { int preload; } fpu_switch_t;

static inline fpu_switch_t
switch_fpu_prepare(struct fpu *old_fpu, struct fpu *new_fpu, int cpu)
{
	fpu_switch_t fpu;

	/*
	 * If the task has used the math, pre-load the FPU on xsave processors
	 * or if the past 5 consecutive context-switches used math.
	 */
	fpu.preload = new_fpu->fpstate_active &&
		      (use_eager_fpu() || new_fpu->counter > 5);

	if (old_fpu->fpregs_active) {
		if (!copy_fpregs_to_fpstate(old_fpu))
			old_fpu->last_cpu = -1;
		else
			old_fpu->last_cpu = cpu;

		/* But leave fpu_fpregs_owner_ctx! */
		old_fpu->fpregs_active = 0;

		/* Don't change CR0.TS if we just switch! */
		if (fpu.preload) {
			new_fpu->counter++;
			__fpregs_activate(new_fpu);
			prefetch(&new_fpu->state);
		} else {
			__fpregs_deactivate_hw();
		}
	} else {
		old_fpu->counter = 0;
		old_fpu->last_cpu = -1;
		if (fpu.preload) {
			new_fpu->counter++;
			if (fpu_want_lazy_restore(new_fpu, cpu))
				fpu.preload = 0;
			else
				prefetch(&new_fpu->state);
			fpregs_activate(new_fpu);
		}
	}
	return fpu;
}

/*
 * By the time this gets called, we've already cleared CR0.TS and
 * given the process the FPU if we are going to preload the FPU
 * state - all we need to do is to conditionally restore the register
 * state itself.
 */
static inline void switch_fpu_finish(struct fpu *new_fpu, fpu_switch_t fpu_switch)
{
	if (fpu_switch.preload) {
		if (unlikely(copy_fpstate_to_fpregs(new_fpu)))
			fpu__clear(new_fpu);
	}
}

/*
 * Signal frame handlers...
 */
extern int copy_fpstate_to_sigframe(void __user *buf, void __user *fx, int size);

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

#endif /* _ASM_X86_FPU_INTERNAL_H */
