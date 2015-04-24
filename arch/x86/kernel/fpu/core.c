/*
 *  Copyright (C) 1994 Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *  General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */
#include <asm/fpu/internal.h>

/*
 * Track whether the kernel is using the FPU state
 * currently.
 *
 * This flag is used:
 *
 *   - by IRQ context code to potentially use the FPU
 *     if it's unused.
 *
 *   - to debug kernel_fpu_begin()/end() correctness
 */
static DEFINE_PER_CPU(bool, in_kernel_fpu);

/*
 * Track which context is using the FPU on the CPU:
 */
DEFINE_PER_CPU(struct fpu *, fpu_fpregs_owner_ctx);

static void kernel_fpu_disable(void)
{
	WARN_ON(this_cpu_read(in_kernel_fpu));
	this_cpu_write(in_kernel_fpu, true);
}

static void kernel_fpu_enable(void)
{
	WARN_ON_ONCE(!this_cpu_read(in_kernel_fpu));
	this_cpu_write(in_kernel_fpu, false);
}

static bool kernel_fpu_disabled(void)
{
	return this_cpu_read(in_kernel_fpu);
}

/*
 * Were we in an interrupt that interrupted kernel mode?
 *
 * On others, we can do a kernel_fpu_begin/end() pair *ONLY* if that
 * pair does nothing at all: the thread must not have fpu (so
 * that we don't try to save the FPU state), and TS must
 * be set (so that the clts/stts pair does nothing that is
 * visible in the interrupted kernel thread).
 *
 * Except for the eagerfpu case when we return true; in the likely case
 * the thread has FPU but we are not going to set/clear TS.
 */
static bool interrupted_kernel_fpu_idle(void)
{
	if (kernel_fpu_disabled())
		return false;

	if (use_eager_fpu())
		return true;

	return !current->thread.fpu.has_fpu && (read_cr0() & X86_CR0_TS);
}

/*
 * Were we in user mode (or vm86 mode) when we were
 * interrupted?
 *
 * Doing kernel_fpu_begin/end() is ok if we are running
 * in an interrupt context from user mode - we'll just
 * save the FPU state as required.
 */
static bool interrupted_user_mode(void)
{
	struct pt_regs *regs = get_irq_regs();
	return regs && user_mode(regs);
}

/*
 * Can we use the FPU in kernel mode with the
 * whole "kernel_fpu_begin/end()" sequence?
 *
 * It's always ok in process context (ie "not interrupt")
 * but it is sometimes ok even from an irq.
 */
bool irq_fpu_usable(void)
{
	return !in_interrupt() ||
		interrupted_user_mode() ||
		interrupted_kernel_fpu_idle();
}
EXPORT_SYMBOL(irq_fpu_usable);

void __kernel_fpu_begin(void)
{
	struct fpu *fpu = &current->thread.fpu;

	kernel_fpu_disable();

	if (fpu->has_fpu) {
		fpu_save_init(fpu);
	} else {
		this_cpu_write(fpu_fpregs_owner_ctx, NULL);
		if (!use_eager_fpu())
			clts();
	}
}
EXPORT_SYMBOL(__kernel_fpu_begin);

void __kernel_fpu_end(void)
{
	struct fpu *fpu = &current->thread.fpu;

	if (fpu->has_fpu) {
		if (WARN_ON(restore_fpu_checking(fpu)))
			fpu_reset_state(fpu);
	} else if (!use_eager_fpu()) {
		stts();
	}

	kernel_fpu_enable();
}
EXPORT_SYMBOL(__kernel_fpu_end);

static void __save_fpu(struct fpu *fpu)
{
	if (use_xsave()) {
		if (unlikely(system_state == SYSTEM_BOOTING))
			xsave_state_booting(&fpu->state->xsave);
		else
			xsave_state(&fpu->state->xsave);
	} else {
		fpu_fxsave(fpu);
	}
}

/*
 * Save the FPU state (initialize it if necessary):
 *
 * This only ever gets called for the current task.
 */
void fpu__save(struct fpu *fpu)
{
	WARN_ON(fpu != &current->thread.fpu);

	preempt_disable();
	if (fpu->has_fpu) {
		if (use_eager_fpu()) {
			__save_fpu(fpu);
		} else {
			fpu_save_init(fpu);
			__thread_fpu_end(fpu);
		}
	}
	preempt_enable();
}
EXPORT_SYMBOL_GPL(fpu__save);

void fpstate_init(struct fpu *fpu)
{
	if (!cpu_has_fpu) {
		finit_soft_fpu(&fpu->state->soft);
		return;
	}

	memset(fpu->state, 0, xstate_size);

	if (cpu_has_fxsr) {
		fx_finit(&fpu->state->fxsave);
	} else {
		struct i387_fsave_struct *fp = &fpu->state->fsave;
		fp->cwd = 0xffff037fu;
		fp->swd = 0xffff0000u;
		fp->twd = 0xffffffffu;
		fp->fos = 0xffff0000u;
	}
}
EXPORT_SYMBOL_GPL(fpstate_init);

/*
 * FPU state allocation:
 */
static struct kmem_cache *task_xstate_cachep;

void fpstate_cache_init(void)
{
	task_xstate_cachep =
		kmem_cache_create("task_xstate", xstate_size,
				  __alignof__(union thread_xstate),
				  SLAB_PANIC | SLAB_NOTRACK, NULL);
	setup_xstate_comp();
}

int fpstate_alloc(struct fpu *fpu)
{
	if (fpu->state)
		return 0;

	fpu->state = kmem_cache_alloc(task_xstate_cachep, GFP_KERNEL);
	if (!fpu->state)
		return -ENOMEM;

	/* The CPU requires the FPU state to be aligned to 16 byte boundaries: */
	WARN_ON((unsigned long)fpu->state & 15);

	return 0;
}
EXPORT_SYMBOL_GPL(fpstate_alloc);

void fpstate_free(struct fpu *fpu)
{
	if (fpu->state) {
		kmem_cache_free(task_xstate_cachep, fpu->state);
		fpu->state = NULL;
	}
}
EXPORT_SYMBOL_GPL(fpstate_free);

/*
 * Copy the current task's FPU state to a new task's FPU context.
 *
 * In the 'eager' case we just save to the destination context.
 *
 * In the 'lazy' case we save to the source context, mark the FPU lazy
 * via stts() and copy the source context into the destination context.
 */
static void fpu_copy(struct fpu *dst_fpu, struct fpu *src_fpu)
{
	WARN_ON(src_fpu != &current->thread.fpu);

	if (use_eager_fpu()) {
		memset(&dst_fpu->state->xsave, 0, xstate_size);
		__save_fpu(dst_fpu);
	} else {
		fpu__save(src_fpu);
		memcpy(dst_fpu->state, src_fpu->state, xstate_size);
	}
}

int fpu__copy(struct fpu *dst_fpu, struct fpu *src_fpu)
{
	dst_fpu->counter = 0;
	dst_fpu->has_fpu = 0;
	dst_fpu->state = NULL;
	dst_fpu->last_cpu = -1;

	if (src_fpu->fpstate_active) {
		int err = fpstate_alloc(dst_fpu);

		if (err)
			return err;
		fpu_copy(dst_fpu, src_fpu);
	}
	return 0;
}

/*
 * Allocate the backing store for the current task's FPU registers
 * and initialize the registers themselves as well.
 *
 * Can fail.
 */
int fpstate_alloc_init(struct fpu *fpu)
{
	int ret;

	if (WARN_ON_ONCE(fpu != &current->thread.fpu))
		return -EINVAL;
	if (WARN_ON_ONCE(fpu->fpstate_active))
		return -EINVAL;

	/*
	 * Memory allocation at the first usage of the FPU and other state.
	 */
	ret = fpstate_alloc(fpu);
	if (ret)
		return ret;

	fpstate_init(fpu);

	/* Safe to do for the current task: */
	fpu->fpstate_active = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(fpstate_alloc_init);

/*
 * This function is called before we modify a stopped child's
 * FPU state context.
 *
 * If the child has not used the FPU before then initialize its
 * FPU context.
 *
 * If the child has used the FPU before then unlazy it.
 *
 * [ After this function call, after the context is modified and
 *   the child task is woken up, the child task will restore
 *   the modified FPU state from the modified context. If we
 *   didn't clear its lazy status here then the lazy in-registers
 *   state pending on its former CPU could be restored, losing
 *   the modifications. ]
 *
 * This function is also called before we read a stopped child's
 * FPU state - to make sure it's modified.
 *
 * TODO: A future optimization would be to skip the unlazying in
 *       the read-only case, it's not strictly necessary for
 *       read-only access to the context.
 */
static int fpu__unlazy_stopped(struct fpu *child_fpu)
{
	int ret;

	if (WARN_ON_ONCE(child_fpu == &current->thread.fpu))
		return -EINVAL;

	if (child_fpu->fpstate_active) {
		child_fpu->last_cpu = -1;
		return 0;
	}

	/*
	 * Memory allocation at the first usage of the FPU and other state.
	 */
	ret = fpstate_alloc(child_fpu);
	if (ret)
		return ret;

	fpstate_init(child_fpu);

	/* Safe to do for stopped child tasks: */
	child_fpu->fpstate_active = 1;

	return 0;
}

/*
 * 'fpu__restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 *
 * Careful.. There are problems with IBM-designed IRQ13 behaviour.
 * Don't touch unless you *really* know how it works.
 *
 * Must be called with kernel preemption disabled (eg with local
 * local interrupts as in the case of do_device_not_available).
 */
void fpu__restore(void)
{
	struct task_struct *tsk = current;
	struct fpu *fpu = &tsk->thread.fpu;

	if (!fpu->fpstate_active) {
		local_irq_enable();
		/*
		 * does a slab alloc which can sleep
		 */
		if (fpstate_alloc_init(fpu)) {
			/*
			 * ran out of memory!
			 */
			do_group_exit(SIGKILL);
			return;
		}
		local_irq_disable();
	}

	/* Avoid __kernel_fpu_begin() right after __thread_fpu_begin() */
	kernel_fpu_disable();
	__thread_fpu_begin(fpu);
	if (unlikely(restore_fpu_checking(fpu))) {
		fpu_reset_state(fpu);
		force_sig_info(SIGSEGV, SEND_SIG_PRIV, tsk);
	} else {
		tsk->thread.fpu.counter++;
	}
	kernel_fpu_enable();
}
EXPORT_SYMBOL_GPL(fpu__restore);

void fpu__clear(struct task_struct *tsk)
{
	struct fpu *fpu = &tsk->thread.fpu;

	WARN_ON_ONCE(tsk != current); /* Almost certainly an anomaly */

	if (!use_eager_fpu()) {
		/* FPU state will be reallocated lazily at the first use. */
		drop_fpu(fpu);
		fpstate_free(fpu);
	} else {
		if (!fpu->fpstate_active) {
			/* kthread execs. TODO: cleanup this horror. */
			if (WARN_ON(fpstate_alloc_init(fpu)))
				force_sig(SIGKILL, tsk);
			user_fpu_begin();
		}
		restore_init_xstate();
	}
}

/*
 * The xstateregs_active() routine is the same as the fpregs_active() routine,
 * as the "regset->n" for the xstate regset will be updated based on the feature
 * capabilites supported by the xsave.
 */
int fpregs_active(struct task_struct *target, const struct user_regset *regset)
{
	struct fpu *target_fpu = &target->thread.fpu;

	return target_fpu->fpstate_active ? regset->n : 0;
}

int xfpregs_active(struct task_struct *target, const struct user_regset *regset)
{
	struct fpu *target_fpu = &target->thread.fpu;

	return (cpu_has_fxsr && target_fpu->fpstate_active) ? regset->n : 0;
}

int xfpregs_get(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		void *kbuf, void __user *ubuf)
{
	struct fpu *fpu = &target->thread.fpu;
	int ret;

	if (!cpu_has_fxsr)
		return -ENODEV;

	ret = fpu__unlazy_stopped(fpu);
	if (ret)
		return ret;

	sanitize_i387_state(target);

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &fpu->state->fxsave, 0, -1);
}

int xfpregs_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf)
{
	struct fpu *fpu = &target->thread.fpu;
	int ret;

	if (!cpu_has_fxsr)
		return -ENODEV;

	ret = fpu__unlazy_stopped(fpu);
	if (ret)
		return ret;

	sanitize_i387_state(target);

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &fpu->state->fxsave, 0, -1);

	/*
	 * mxcsr reserved bits must be masked to zero for security reasons.
	 */
	fpu->state->fxsave.mxcsr &= mxcsr_feature_mask;

	/*
	 * update the header bits in the xsave header, indicating the
	 * presence of FP and SSE state.
	 */
	if (cpu_has_xsave)
		fpu->state->xsave.xsave_hdr.xstate_bv |= XSTATE_FPSSE;

	return ret;
}

int xstateregs_get(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		void *kbuf, void __user *ubuf)
{
	struct fpu *fpu = &target->thread.fpu;
	struct xsave_struct *xsave;
	int ret;

	if (!cpu_has_xsave)
		return -ENODEV;

	ret = fpu__unlazy_stopped(fpu);
	if (ret)
		return ret;

	xsave = &fpu->state->xsave;

	/*
	 * Copy the 48bytes defined by the software first into the xstate
	 * memory layout in the thread struct, so that we can copy the entire
	 * xstateregs to the user using one user_regset_copyout().
	 */
	memcpy(&xsave->i387.sw_reserved,
		xstate_fx_sw_bytes, sizeof(xstate_fx_sw_bytes));
	/*
	 * Copy the xstate memory layout.
	 */
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, xsave, 0, -1);
	return ret;
}

int xstateregs_set(struct task_struct *target, const struct user_regset *regset,
		  unsigned int pos, unsigned int count,
		  const void *kbuf, const void __user *ubuf)
{
	struct fpu *fpu = &target->thread.fpu;
	struct xsave_struct *xsave;
	int ret;

	if (!cpu_has_xsave)
		return -ENODEV;

	ret = fpu__unlazy_stopped(fpu);
	if (ret)
		return ret;

	xsave = &fpu->state->xsave;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, xsave, 0, -1);
	/*
	 * mxcsr reserved bits must be masked to zero for security reasons.
	 */
	xsave->i387.mxcsr &= mxcsr_feature_mask;
	xsave->xsave_hdr.xstate_bv &= xfeatures_mask;
	/*
	 * These bits must be zero.
	 */
	memset(&xsave->xsave_hdr.reserved, 0, 48);

	return ret;
}

#if defined CONFIG_X86_32 || defined CONFIG_IA32_EMULATION

/*
 * FPU tag word conversions.
 */

static inline unsigned short twd_i387_to_fxsr(unsigned short twd)
{
	unsigned int tmp; /* to avoid 16 bit prefixes in the code */

	/* Transform each pair of bits into 01 (valid) or 00 (empty) */
	tmp = ~twd;
	tmp = (tmp | (tmp>>1)) & 0x5555; /* 0V0V0V0V0V0V0V0V */
	/* and move the valid bits to the lower byte. */
	tmp = (tmp | (tmp >> 1)) & 0x3333; /* 00VV00VV00VV00VV */
	tmp = (tmp | (tmp >> 2)) & 0x0f0f; /* 0000VVVV0000VVVV */
	tmp = (tmp | (tmp >> 4)) & 0x00ff; /* 00000000VVVVVVVV */

	return tmp;
}

#define FPREG_ADDR(f, n)	((void *)&(f)->st_space + (n) * 16)
#define FP_EXP_TAG_VALID	0
#define FP_EXP_TAG_ZERO		1
#define FP_EXP_TAG_SPECIAL	2
#define FP_EXP_TAG_EMPTY	3

static inline u32 twd_fxsr_to_i387(struct i387_fxsave_struct *fxsave)
{
	struct _fpxreg *st;
	u32 tos = (fxsave->swd >> 11) & 7;
	u32 twd = (unsigned long) fxsave->twd;
	u32 tag;
	u32 ret = 0xffff0000u;
	int i;

	for (i = 0; i < 8; i++, twd >>= 1) {
		if (twd & 0x1) {
			st = FPREG_ADDR(fxsave, (i - tos) & 7);

			switch (st->exponent & 0x7fff) {
			case 0x7fff:
				tag = FP_EXP_TAG_SPECIAL;
				break;
			case 0x0000:
				if (!st->significand[0] &&
				    !st->significand[1] &&
				    !st->significand[2] &&
				    !st->significand[3])
					tag = FP_EXP_TAG_ZERO;
				else
					tag = FP_EXP_TAG_SPECIAL;
				break;
			default:
				if (st->significand[3] & 0x8000)
					tag = FP_EXP_TAG_VALID;
				else
					tag = FP_EXP_TAG_SPECIAL;
				break;
			}
		} else {
			tag = FP_EXP_TAG_EMPTY;
		}
		ret |= tag << (2 * i);
	}
	return ret;
}

/*
 * FXSR floating point environment conversions.
 */

void
convert_from_fxsr(struct user_i387_ia32_struct *env, struct task_struct *tsk)
{
	struct i387_fxsave_struct *fxsave = &tsk->thread.fpu.state->fxsave;
	struct _fpreg *to = (struct _fpreg *) &env->st_space[0];
	struct _fpxreg *from = (struct _fpxreg *) &fxsave->st_space[0];
	int i;

	env->cwd = fxsave->cwd | 0xffff0000u;
	env->swd = fxsave->swd | 0xffff0000u;
	env->twd = twd_fxsr_to_i387(fxsave);

#ifdef CONFIG_X86_64
	env->fip = fxsave->rip;
	env->foo = fxsave->rdp;
	/*
	 * should be actually ds/cs at fpu exception time, but
	 * that information is not available in 64bit mode.
	 */
	env->fcs = task_pt_regs(tsk)->cs;
	if (tsk == current) {
		savesegment(ds, env->fos);
	} else {
		env->fos = tsk->thread.ds;
	}
	env->fos |= 0xffff0000;
#else
	env->fip = fxsave->fip;
	env->fcs = (u16) fxsave->fcs | ((u32) fxsave->fop << 16);
	env->foo = fxsave->foo;
	env->fos = fxsave->fos;
#endif

	for (i = 0; i < 8; ++i)
		memcpy(&to[i], &from[i], sizeof(to[0]));
}

void convert_to_fxsr(struct task_struct *tsk,
		     const struct user_i387_ia32_struct *env)

{
	struct i387_fxsave_struct *fxsave = &tsk->thread.fpu.state->fxsave;
	struct _fpreg *from = (struct _fpreg *) &env->st_space[0];
	struct _fpxreg *to = (struct _fpxreg *) &fxsave->st_space[0];
	int i;

	fxsave->cwd = env->cwd;
	fxsave->swd = env->swd;
	fxsave->twd = twd_i387_to_fxsr(env->twd);
	fxsave->fop = (u16) ((u32) env->fcs >> 16);
#ifdef CONFIG_X86_64
	fxsave->rip = env->fip;
	fxsave->rdp = env->foo;
	/* cs and ds ignored */
#else
	fxsave->fip = env->fip;
	fxsave->fcs = (env->fcs & 0xffff);
	fxsave->foo = env->foo;
	fxsave->fos = env->fos;
#endif

	for (i = 0; i < 8; ++i)
		memcpy(&to[i], &from[i], sizeof(from[0]));
}

int fpregs_get(struct task_struct *target, const struct user_regset *regset,
	       unsigned int pos, unsigned int count,
	       void *kbuf, void __user *ubuf)
{
	struct fpu *fpu = &target->thread.fpu;
	struct user_i387_ia32_struct env;
	int ret;

	ret = fpu__unlazy_stopped(fpu);
	if (ret)
		return ret;

	if (!static_cpu_has(X86_FEATURE_FPU))
		return fpregs_soft_get(target, regset, pos, count, kbuf, ubuf);

	if (!cpu_has_fxsr)
		return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					   &fpu->state->fsave, 0,
					   -1);

	sanitize_i387_state(target);

	if (kbuf && pos == 0 && count == sizeof(env)) {
		convert_from_fxsr(kbuf, target);
		return 0;
	}

	convert_from_fxsr(&env, target);

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, &env, 0, -1);
}

int fpregs_set(struct task_struct *target, const struct user_regset *regset,
	       unsigned int pos, unsigned int count,
	       const void *kbuf, const void __user *ubuf)
{
	struct fpu *fpu = &target->thread.fpu;
	struct user_i387_ia32_struct env;
	int ret;

	ret = fpu__unlazy_stopped(fpu);
	if (ret)
		return ret;

	sanitize_i387_state(target);

	if (!static_cpu_has(X86_FEATURE_FPU))
		return fpregs_soft_set(target, regset, pos, count, kbuf, ubuf);

	if (!cpu_has_fxsr)
		return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					  &fpu->state->fsave, 0,
					  -1);

	if (pos > 0 || count < sizeof(env))
		convert_from_fxsr(&env, target);

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &env, 0, -1);
	if (!ret)
		convert_to_fxsr(target, &env);

	/*
	 * update the header bit in the xsave header, indicating the
	 * presence of FP.
	 */
	if (cpu_has_xsave)
		fpu->state->xsave.xsave_hdr.xstate_bv |= XSTATE_FP;
	return ret;
}

/*
 * FPU state for core dumps.
 * This is only used for a.out dumps now.
 * It is declared generically using elf_fpregset_t (which is
 * struct user_i387_struct) but is in fact only used for 32-bit
 * dumps, so on 64-bit it is really struct user_i387_ia32_struct.
 */
int dump_fpu(struct pt_regs *regs, struct user_i387_struct *ufpu)
{
	struct task_struct *tsk = current;
	struct fpu *fpu = &tsk->thread.fpu;
	int fpvalid;

	fpvalid = fpu->fpstate_active;
	if (fpvalid)
		fpvalid = !fpregs_get(tsk, NULL,
				      0, sizeof(struct user_i387_ia32_struct),
				      ufpu, NULL);

	return fpvalid;
}
EXPORT_SYMBOL(dump_fpu);

#endif	/* CONFIG_X86_32 || CONFIG_IA32_EMULATION */
