/*
 * Copyright (C) 1994 Linus Torvalds
 *
 * Pentium III FXSR, SSE support
 * General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * x86-64 work by Andi Kleen 2002
 */

#ifndef _FPU_INTERNAL_H
#define _FPU_INTERNAL_H

#include <linux/regset.h>
#include <linux/compat.h>
#include <linux/slab.h>

#include <asm/user.h>
#include <asm/i387.h>
#include <asm/xsave.h>

#ifdef CONFIG_X86_64
# include <asm/sigcontext32.h>
# include <asm/user32.h>
struct ksignal;
int ia32_setup_rt_frame(int sig, struct ksignal *ksig,
			compat_sigset_t *set, struct pt_regs *regs);
int ia32_setup_frame(int sig, struct ksignal *ksig,
		     compat_sigset_t *set, struct pt_regs *regs);
#else
# define user_i387_ia32_struct	user_i387_struct
# define user32_fxsr_struct	user_fxsr_struct
# define ia32_setup_frame	__setup_frame
# define ia32_setup_rt_frame	__setup_rt_frame
#endif

extern unsigned int mxcsr_feature_mask;
extern void fpu__cpu_init(void);
extern void eager_fpu_init(void);

DECLARE_PER_CPU(struct fpu *, fpu_fpregs_owner_ctx);

extern void convert_from_fxsr(struct user_i387_ia32_struct *env,
			      struct task_struct *tsk);
extern void convert_to_fxsr(struct task_struct *tsk,
			    const struct user_i387_ia32_struct *env);

extern user_regset_active_fn fpregs_active, xfpregs_active;
extern user_regset_get_fn fpregs_get, xfpregs_get, fpregs_soft_get,
				xstateregs_get;
extern user_regset_set_fn fpregs_set, xfpregs_set, fpregs_soft_set,
				 xstateregs_set;

/*
 * xstateregs_active == fpregs_active. Please refer to the comment
 * at the definition of fpregs_active.
 */
#define xstateregs_active	fpregs_active

#ifdef CONFIG_MATH_EMULATION
extern void finit_soft_fpu(struct i387_soft_struct *soft);
#else
static inline void finit_soft_fpu(struct i387_soft_struct *soft) {}
#endif

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

static inline int is_ia32_compat_frame(void)
{
	return config_enabled(CONFIG_IA32_EMULATION) &&
	       test_thread_flag(TIF_IA32);
}

static inline int is_ia32_frame(void)
{
	return config_enabled(CONFIG_X86_32) || is_ia32_compat_frame();
}

static inline int is_x32_frame(void)
{
	return config_enabled(CONFIG_X86_X32_ABI) && test_thread_flag(TIF_X32);
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

static inline void fx_finit(struct i387_fxsave_struct *fx)
{
	fx->cwd = 0x37f;
	fx->mxcsr = MXCSR_DEFAULT;
}

extern void __sanitize_i387_state(struct task_struct *);

static inline void sanitize_i387_state(struct task_struct *tsk)
{
	if (!use_xsaveopt())
		return;
	__sanitize_i387_state(tsk);
}

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
		asm volatile( "fxsave %[fx]" : [fx] "=m" (fpu->state->fxsave));
	else if (config_enabled(CONFIG_AS_FXSAVEQ))
		asm volatile("fxsaveq %[fx]" : [fx] "=m" (fpu->state->fxsave));
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
		 *  asm volatile("rex64/fxsave %0" : "=m" (fpu->state->fxsave));
		 *
		 * This, however, we can work around by forcing the compiler to
		 * select an addressing mode that doesn't require extended
		 * registers.
		 */
		asm volatile( "rex64/fxsave (%[fx])"
			     : "=m" (fpu->state->fxsave)
			     : [fx] "R" (&fpu->state->fxsave));
	}
}

/*
 * These must be called with preempt disabled. Returns
 * 'true' if the FPU state is still intact.
 */
static inline int fpu_save_init(struct fpu *fpu)
{
	if (use_xsave()) {
		xsave_state(&fpu->state->xsave);

		/*
		 * xsave header may indicate the init state of the FP.
		 */
		if (!(fpu->state->xsave.xsave_hdr.xstate_bv & XSTATE_FP))
			return 1;
	} else if (use_fxsr()) {
		fpu_fxsave(fpu);
	} else {
		asm volatile("fnsave %[fx]; fwait"
			     : [fx] "=m" (fpu->state->fsave));
		return 0;
	}

	/*
	 * If exceptions are pending, we need to clear them so
	 * that we don't randomly get exceptions later.
	 *
	 * FIXME! Is this perhaps only true for the old-style
	 * irq13 case? Maybe we could leave the x87 state
	 * intact otherwise?
	 */
	if (unlikely(fpu->state->fxsave.swd & X87_FSW_ES)) {
		asm volatile("fnclex");
		return 0;
	}
	return 1;
}

static inline int fpu_restore_checking(struct fpu *fpu)
{
	if (use_xsave())
		return fpu_xrstor_checking(&fpu->state->xsave);
	else if (use_fxsr())
		return fxrstor_checking(&fpu->state->fxsave);
	else
		return frstor_checking(&fpu->state->fsave);
}

static inline int restore_fpu_checking(struct fpu *fpu)
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
			: : [addr] "m" (fpu->has_fpu));
	}

	return fpu_restore_checking(fpu);
}

/* Must be paired with an 'stts' after! */
static inline void __thread_clear_has_fpu(struct fpu *fpu)
{
	fpu->has_fpu = 0;
	this_cpu_write(fpu_fpregs_owner_ctx, NULL);
}

/* Must be paired with a 'clts' before! */
static inline void __thread_set_has_fpu(struct fpu *fpu)
{
	fpu->has_fpu = 1;
	this_cpu_write(fpu_fpregs_owner_ctx, fpu);
}

/*
 * Encapsulate the CR0.TS handling together with the
 * software flag.
 *
 * These generally need preemption protection to work,
 * do try to avoid using these on their own.
 */
static inline void __thread_fpu_end(struct fpu *fpu)
{
	__thread_clear_has_fpu(fpu);
	if (!use_eager_fpu())
		stts();
}

static inline void __thread_fpu_begin(struct fpu *fpu)
{
	if (!use_eager_fpu())
		clts();
	__thread_set_has_fpu(fpu);
}

static inline void drop_fpu(struct fpu *fpu)
{
	/*
	 * Forget coprocessor state..
	 */
	preempt_disable();
	fpu->counter = 0;

	if (fpu->has_fpu) {
		/* Ignore delayed exceptions from user space */
		asm volatile("1: fwait\n"
			     "2:\n"
			     _ASM_EXTABLE(1b, 2b));
		__thread_fpu_end(fpu);
	}

	fpu->fpstate_active = 0;

	preempt_enable();
}

static inline void restore_init_xstate(void)
{
	if (use_xsave())
		xrstor_state(init_xstate_buf, -1);
	else
		fxrstor_checking(&init_xstate_buf->i387);
}

/*
 * Reset the FPU state in the eager case and drop it in the lazy case (later use
 * will reinit it).
 */
static inline void fpu_reset_state(struct fpu *fpu)
{
	if (!use_eager_fpu())
		drop_fpu(fpu);
	else
		restore_init_xstate();
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

	if (old_fpu->has_fpu) {
		if (!fpu_save_init(old_fpu))
			old_fpu->last_cpu = -1;
		else
			old_fpu->last_cpu = cpu;

		/* But leave fpu_fpregs_owner_ctx! */
		old_fpu->has_fpu = 0;

		/* Don't change CR0.TS if we just switch! */
		if (fpu.preload) {
			new_fpu->counter++;
			__thread_set_has_fpu(new_fpu);
			prefetch(new_fpu->state);
		} else if (!use_eager_fpu())
			stts();
	} else {
		old_fpu->counter = 0;
		old_fpu->last_cpu = -1;
		if (fpu.preload) {
			new_fpu->counter++;
			if (fpu_want_lazy_restore(new_fpu, cpu))
				fpu.preload = 0;
			else
				prefetch(new_fpu->state);
			__thread_fpu_begin(new_fpu);
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
		if (unlikely(restore_fpu_checking(new_fpu)))
			fpu_reset_state(new_fpu);
	}
}

/*
 * Signal frame handlers...
 */
extern int save_xstate_sig(void __user *buf, void __user *fx, int size);
extern int __restore_xstate_sig(void __user *buf, void __user *fx, int size);

static inline int xstate_sigframe_size(void)
{
	return use_xsave() ? xstate_size + FP_XSTATE_MAGIC2_SIZE : xstate_size;
}

static inline int restore_xstate_sig(void __user *buf, int ia32_frame)
{
	void __user *buf_fx = buf;
	int size = xstate_sigframe_size();

	if (ia32_frame && use_fxsr()) {
		buf_fx = buf + sizeof(struct i387_fsave_struct);
		size += sizeof(struct i387_fsave_struct);
	}

	return __restore_xstate_sig(buf, buf_fx, size);
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
	if (!user_has_fpu())
		__thread_fpu_begin(fpu);
	preempt_enable();
}

/*
 * i387 state interaction
 */
static inline unsigned short get_fpu_cwd(struct task_struct *tsk)
{
	if (cpu_has_fxsr) {
		return tsk->thread.fpu.state->fxsave.cwd;
	} else {
		return (unsigned short)tsk->thread.fpu.state->fsave.cwd;
	}
}

static inline unsigned short get_fpu_swd(struct task_struct *tsk)
{
	if (cpu_has_fxsr) {
		return tsk->thread.fpu.state->fxsave.swd;
	} else {
		return (unsigned short)tsk->thread.fpu.state->fsave.swd;
	}
}

static inline unsigned short get_fpu_mxcsr(struct task_struct *tsk)
{
	if (cpu_has_xmm) {
		return tsk->thread.fpu.state->fxsave.mxcsr;
	} else {
		return MXCSR_DEFAULT;
	}
}

extern void fpstate_cache_init(void);

extern int fpstate_alloc(struct fpu *fpu);
extern void fpstate_free(struct fpu *fpu);
extern int fpu__copy(struct task_struct *dst, struct task_struct *src);

static inline unsigned long
alloc_mathframe(unsigned long sp, int ia32_frame, unsigned long *buf_fx,
		unsigned long *size)
{
	unsigned long frame_size = xstate_sigframe_size();

	*buf_fx = sp = round_down(sp - frame_size, 64);
	if (ia32_frame && use_fxsr()) {
		frame_size += sizeof(struct i387_fsave_struct);
		sp -= sizeof(struct i387_fsave_struct);
	}

	*size = frame_size;
	return sp;
}

#endif
