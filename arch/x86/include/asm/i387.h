/*
 * Copyright (C) 1994 Linus Torvalds
 *
 * Pentium III FXSR, SSE support
 * General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * x86-64 work by Andi Kleen 2002
 */

#ifndef _ASM_X86_I387_H
#define _ASM_X86_I387_H

#ifndef __ASSEMBLY__

#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/regset.h>
#include <linux/hardirq.h>
#include <linux/slab.h>
#include <asm/asm.h>
#include <asm/cpufeature.h>
#include <asm/processor.h>
#include <asm/sigcontext.h>
#include <asm/user.h>
#include <asm/uaccess.h>
#include <asm/xsave.h>

extern unsigned int sig_xstate_size;
extern void fpu_init(void);
extern void mxcsr_feature_mask_init(void);
extern int init_fpu(struct task_struct *child);
extern void math_state_restore(void);
extern void __math_state_restore(void);
extern int dump_fpu(struct pt_regs *, struct user_i387_struct *);

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

extern struct _fpx_sw_bytes fx_sw_reserved;
#ifdef CONFIG_IA32_EMULATION
extern unsigned int sig_xstate_ia32_size;
extern struct _fpx_sw_bytes fx_sw_reserved_ia32;
struct _fpstate_ia32;
struct _xstate_ia32;
extern int save_i387_xstate_ia32(void __user *buf);
extern int restore_i387_xstate_ia32(void __user *buf);
#endif

#ifdef CONFIG_MATH_EMULATION
extern void finit_soft_fpu(struct i387_soft_struct *soft);
#else
static inline void finit_soft_fpu(struct i387_soft_struct *soft) {}
#endif

#define X87_FSW_ES (1 << 7)	/* Exception Summary */

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

extern void __sanitize_i387_state(struct task_struct *);

static inline void sanitize_i387_state(struct task_struct *tsk)
{
	if (!use_xsaveopt())
		return;
	__sanitize_i387_state(tsk);
}

#ifdef CONFIG_X86_64
static inline int fxrstor_checking(struct i387_fxsave_struct *fx)
{
	int err;

	/* See comment in fxsave() below. */
#ifdef CONFIG_AS_FXSAVEQ
	asm volatile("1:  fxrstorq %[fx]\n\t"
		     "2:\n"
		     ".section .fixup,\"ax\"\n"
		     "3:  movl $-1,%[err]\n"
		     "    jmp  2b\n"
		     ".previous\n"
		     _ASM_EXTABLE(1b, 3b)
		     : [err] "=r" (err)
		     : [fx] "m" (*fx), "0" (0));
#else
	asm volatile("1:  rex64/fxrstor (%[fx])\n\t"
		     "2:\n"
		     ".section .fixup,\"ax\"\n"
		     "3:  movl $-1,%[err]\n"
		     "    jmp  2b\n"
		     ".previous\n"
		     _ASM_EXTABLE(1b, 3b)
		     : [err] "=r" (err)
		     : [fx] "R" (fx), "m" (*fx), "0" (0));
#endif
	return err;
}

static inline int fxsave_user(struct i387_fxsave_struct __user *fx)
{
	int err;

	/*
	 * Clear the bytes not touched by the fxsave and reserved
	 * for the SW usage.
	 */
	err = __clear_user(&fx->sw_reserved,
			   sizeof(struct _fpx_sw_bytes));
	if (unlikely(err))
		return -EFAULT;

	/* See comment in fxsave() below. */
#ifdef CONFIG_AS_FXSAVEQ
	asm volatile("1:  fxsaveq %[fx]\n\t"
		     "2:\n"
		     ".section .fixup,\"ax\"\n"
		     "3:  movl $-1,%[err]\n"
		     "    jmp  2b\n"
		     ".previous\n"
		     _ASM_EXTABLE(1b, 3b)
		     : [err] "=r" (err), [fx] "=m" (*fx)
		     : "0" (0));
#else
	asm volatile("1:  rex64/fxsave (%[fx])\n\t"
		     "2:\n"
		     ".section .fixup,\"ax\"\n"
		     "3:  movl $-1,%[err]\n"
		     "    jmp  2b\n"
		     ".previous\n"
		     _ASM_EXTABLE(1b, 3b)
		     : [err] "=r" (err), "=m" (*fx)
		     : [fx] "R" (fx), "0" (0));
#endif
	if (unlikely(err) &&
	    __clear_user(fx, sizeof(struct i387_fxsave_struct)))
		err = -EFAULT;
	/* No need to clear here because the caller clears USED_MATH */
	return err;
}

static inline void fpu_fxsave(struct fpu *fpu)
{
	/* Using "rex64; fxsave %0" is broken because, if the memory operand
	   uses any extended registers for addressing, a second REX prefix
	   will be generated (to the assembler, rex64 followed by semicolon
	   is a separate instruction), and hence the 64-bitness is lost. */

#ifdef CONFIG_AS_FXSAVEQ
	/* Using "fxsaveq %0" would be the ideal choice, but is only supported
	   starting with gas 2.16. */
	__asm__ __volatile__("fxsaveq %0"
			     : "=m" (fpu->state->fxsave));
#else
	/* Using, as a workaround, the properly prefixed form below isn't
	   accepted by any binutils version so far released, complaining that
	   the same type of prefix is used twice if an extended register is
	   needed for addressing (fix submitted to mainline 2005-11-21).
	asm volatile("rex64/fxsave %0"
		     : "=m" (fpu->state->fxsave));
	   This, however, we can work around by forcing the compiler to select
	   an addressing mode that doesn't require extended registers. */
	asm volatile("rex64/fxsave (%[fx])"
		     : "=m" (fpu->state->fxsave)
		     : [fx] "R" (&fpu->state->fxsave));
#endif
}

#else  /* CONFIG_X86_32 */

/* perform fxrstor iff the processor has extended states, otherwise frstor */
static inline int fxrstor_checking(struct i387_fxsave_struct *fx)
{
	/*
	 * The "nop" is needed to make the instructions the same
	 * length.
	 */
	alternative_input(
		"nop ; frstor %1",
		"fxrstor %1",
		X86_FEATURE_FXSR,
		"m" (*fx));

	return 0;
}

static inline void fpu_fxsave(struct fpu *fpu)
{
	asm volatile("fxsave %[fx]"
		     : [fx] "=m" (fpu->state->fxsave));
}

#endif	/* CONFIG_X86_64 */

/* We need a safe address that is cheap to find and that is already
   in L1 during context switch. The best choices are unfortunately
   different for UP and SMP */
#ifdef CONFIG_SMP
#define safe_address (__per_cpu_offset[0])
#else
#define safe_address (kstat_cpu(0).cpustat.user)
#endif

/*
 * These must be called with preempt disabled
 */
static inline void fpu_save_init(struct fpu *fpu)
{
	if (use_xsave()) {
		fpu_xsave(fpu);

		/*
		 * xsave header may indicate the init state of the FP.
		 */
		if (!(fpu->state->xsave.xsave_hdr.xstate_bv & XSTATE_FP))
			return;
	} else if (use_fxsr()) {
		fpu_fxsave(fpu);
	} else {
		asm volatile("fnsave %[fx]; fwait"
			     : [fx] "=m" (fpu->state->fsave));
		return;
	}

	if (unlikely(fpu->state->fxsave.swd & X87_FSW_ES))
		asm volatile("fnclex");

	/* AMD K7/K8 CPUs don't save/restore FDP/FIP/FOP unless an exception
	   is pending.  Clear the x87 state here by setting it to fixed
	   values. safe_address is a random variable that should be in L1 */
	alternative_input(
		ASM_NOP8 ASM_NOP2,
		"emms\n\t"	  	/* clear stack tags */
		"fildl %P[addr]",	/* set F?P to defined value */
		X86_FEATURE_FXSAVE_LEAK,
		[addr] "m" (safe_address));
}

static inline void __save_init_fpu(struct task_struct *tsk)
{
	fpu_save_init(&tsk->thread.fpu);
	task_thread_info(tsk)->status &= ~TS_USEDFPU;
}

static inline int fpu_fxrstor_checking(struct fpu *fpu)
{
	return fxrstor_checking(&fpu->state->fxsave);
}

static inline int fpu_restore_checking(struct fpu *fpu)
{
	if (use_xsave())
		return fpu_xrstor_checking(fpu);
	else
		return fpu_fxrstor_checking(fpu);
}

static inline int restore_fpu_checking(struct task_struct *tsk)
{
	return fpu_restore_checking(&tsk->thread.fpu);
}

/*
 * Signal frame handlers...
 */
extern int save_i387_xstate(void __user *buf);
extern int restore_i387_xstate(void __user *buf);

static inline void __unlazy_fpu(struct task_struct *tsk)
{
	if (task_thread_info(tsk)->status & TS_USEDFPU) {
		__save_init_fpu(tsk);
		stts();
	} else
		tsk->fpu_counter = 0;
}

static inline void __clear_fpu(struct task_struct *tsk)
{
	if (task_thread_info(tsk)->status & TS_USEDFPU) {
		/* Ignore delayed exceptions from user space */
		asm volatile("1: fwait\n"
			     "2:\n"
			     _ASM_EXTABLE(1b, 2b));
		task_thread_info(tsk)->status &= ~TS_USEDFPU;
		stts();
	}
}

/*
 * Were we in an interrupt that interrupted kernel mode?
 *
 * We can do a kernel_fpu_begin/end() pair *ONLY* if that
 * pair does nothing at all: TS_USEDFPU must be clear (so
 * that we don't try to save the FPU state), and TS must
 * be set (so that the clts/stts pair does nothing that is
 * visible in the interrupted kernel thread).
 */
static inline bool interrupted_kernel_fpu_idle(void)
{
	return !(current_thread_info()->status & TS_USEDFPU) &&
		(read_cr0() & X86_CR0_TS);
}

/*
 * Were we in user mode (or vm86 mode) when we were
 * interrupted?
 *
 * Doing kernel_fpu_begin/end() is ok if we are running
 * in an interrupt context from user mode - we'll just
 * save the FPU state as required.
 */
static inline bool interrupted_user_mode(void)
{
	struct pt_regs *regs = get_irq_regs();
	return regs && user_mode_vm(regs);
}

/*
 * Can we use the FPU in kernel mode with the
 * whole "kernel_fpu_begin/end()" sequence?
 *
 * It's always ok in process context (ie "not interrupt")
 * but it is sometimes ok even from an irq.
 */
static inline bool irq_fpu_usable(void)
{
	return !in_interrupt() ||
		interrupted_user_mode() ||
		interrupted_kernel_fpu_idle();
}

static inline void kernel_fpu_begin(void)
{
	struct thread_info *me = current_thread_info();

	WARN_ON_ONCE(!irq_fpu_usable());
	preempt_disable();
	if (me->status & TS_USEDFPU)
		__save_init_fpu(me->task);
	else
		clts();
}

static inline void kernel_fpu_end(void)
{
	stts();
	preempt_enable();
}

/*
 * Some instructions like VIA's padlock instructions generate a spurious
 * DNA fault but don't modify SSE registers. And these instructions
 * get used from interrupt context as well. To prevent these kernel instructions
 * in interrupt context interacting wrongly with other user/kernel fpu usage, we
 * should use them only in the context of irq_ts_save/restore()
 */
static inline int irq_ts_save(void)
{
	/*
	 * If in process context and not atomic, we can take a spurious DNA fault.
	 * Otherwise, doing clts() in process context requires disabling preemption
	 * or some heavy lifting like kernel_fpu_begin()
	 */
	if (!in_atomic())
		return 0;

	if (read_cr0() & X86_CR0_TS) {
		clts();
		return 1;
	}

	return 0;
}

static inline void irq_ts_restore(int TS_state)
{
	if (TS_state)
		stts();
}

/*
 * These disable preemption on their own and are safe
 */
static inline void save_init_fpu(struct task_struct *tsk)
{
	WARN_ON_ONCE(!(task_thread_info(tsk)->status & TS_USEDFPU));
	preempt_disable();
	__save_init_fpu(tsk);
	stts();
	preempt_enable();
}

static inline void unlazy_fpu(struct task_struct *tsk)
{
	preempt_disable();
	__unlazy_fpu(tsk);
	preempt_enable();
}

static inline void clear_fpu(struct task_struct *tsk)
{
	preempt_disable();
	__clear_fpu(tsk);
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

static bool fpu_allocated(struct fpu *fpu)
{
	return fpu->state != NULL;
}

static inline int fpu_alloc(struct fpu *fpu)
{
	if (fpu_allocated(fpu))
		return 0;
	fpu->state = kmem_cache_alloc(task_xstate_cachep, GFP_KERNEL);
	if (!fpu->state)
		return -ENOMEM;
	WARN_ON((unsigned long)fpu->state & 15);
	return 0;
}

static inline void fpu_free(struct fpu *fpu)
{
	if (fpu->state) {
		kmem_cache_free(task_xstate_cachep, fpu->state);
		fpu->state = NULL;
	}
}

static inline void fpu_copy(struct fpu *dst, struct fpu *src)
{
	memcpy(dst->state, src->state, xstate_size);
}

extern void fpu_finit(struct fpu *fpu);

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_I387_H */
