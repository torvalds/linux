/*
 *  Copyright (C) 1994 Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *  General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */
#include <linux/module.h>
#include <linux/regset.h>
#include <linux/sched.h>

#include <asm/sigcontext.h>
#include <asm/processor.h>
#include <asm/math_emu.h>
#include <asm/uaccess.h>
#include <asm/ptrace.h>
#include <asm/i387.h>
#include <asm/user.h>

#ifdef CONFIG_X86_64
# include <asm/sigcontext32.h>
# include <asm/user32.h>
#else
# define save_i387_xstate_ia32		save_i387_xstate
# define restore_i387_xstate_ia32	restore_i387_xstate
# define _fpstate_ia32		_fpstate
# define _xstate_ia32		_xstate
# define sig_xstate_ia32_size   sig_xstate_size
# define fx_sw_reserved_ia32	fx_sw_reserved
# define user_i387_ia32_struct	user_i387_struct
# define user32_fxsr_struct	user_fxsr_struct
#endif

#ifdef CONFIG_MATH_EMULATION
# define HAVE_HWFP		(boot_cpu_data.hard_math)
#else
# define HAVE_HWFP		1
#endif

static unsigned int		mxcsr_feature_mask __read_mostly = 0xffffffffu;
unsigned int xstate_size;
unsigned int sig_xstate_ia32_size = sizeof(struct _fpstate_ia32);
static struct i387_fxsave_struct fx_scratch __cpuinitdata;

void __cpuinit mxcsr_feature_mask_init(void)
{
	unsigned long mask = 0;

	clts();
	if (cpu_has_fxsr) {
		memset(&fx_scratch, 0, sizeof(struct i387_fxsave_struct));
		asm volatile("fxsave %0" : : "m" (fx_scratch));
		mask = fx_scratch.mxcsr_mask;
		if (mask == 0)
			mask = 0x0000ffbf;
	}
	mxcsr_feature_mask &= mask;
	stts();
}

void __cpuinit init_thread_xstate(void)
{
	if (!HAVE_HWFP) {
		xstate_size = sizeof(struct i387_soft_struct);
		return;
	}

	if (cpu_has_xsave) {
		xsave_cntxt_init();
		return;
	}

	if (cpu_has_fxsr)
		xstate_size = sizeof(struct i387_fxsave_struct);
#ifdef CONFIG_X86_32
	else
		xstate_size = sizeof(struct i387_fsave_struct);
#endif
}

#ifdef CONFIG_X86_64
/*
 * Called at bootup to set up the initial FPU state that is later cloned
 * into all processes.
 */
void __cpuinit fpu_init(void)
{
	unsigned long oldcr0 = read_cr0();

	set_in_cr4(X86_CR4_OSFXSR);
	set_in_cr4(X86_CR4_OSXMMEXCPT);

	write_cr0(oldcr0 & ~(X86_CR0_TS|X86_CR0_EM)); /* clear TS and EM */

	/*
	 * Boot processor to setup the FP and extended state context info.
	 */
	if (!smp_processor_id())
		init_thread_xstate();
	xsave_init();

	mxcsr_feature_mask_init();
	/* clean state in init */
	if (cpu_has_xsave)
		current_thread_info()->status = TS_XSAVE;
	else
		current_thread_info()->status = 0;
	clear_used_math();
}
#endif	/* CONFIG_X86_64 */

/*
 * The _current_ task is using the FPU for the first time
 * so initialize it and set the mxcsr to its default
 * value at reset if we support XMM instructions and then
 * remeber the current task has used the FPU.
 */
int init_fpu(struct task_struct *tsk)
{
	if (tsk_used_math(tsk)) {
		if (HAVE_HWFP && tsk == current)
			unlazy_fpu(tsk);
		return 0;
	}

	/*
	 * Memory allocation at the first usage of the FPU and other state.
	 */
	if (!tsk->thread.xstate) {
		tsk->thread.xstate = kmem_cache_alloc(task_xstate_cachep,
						      GFP_KERNEL);
		if (!tsk->thread.xstate)
			return -ENOMEM;
	}

#ifdef CONFIG_X86_32
	if (!HAVE_HWFP) {
		memset(tsk->thread.xstate, 0, xstate_size);
		finit_task(tsk);
		set_stopped_child_used_math(tsk);
		return 0;
	}
#endif

	if (cpu_has_fxsr) {
		struct i387_fxsave_struct *fx = &tsk->thread.xstate->fxsave;

		memset(fx, 0, xstate_size);
		fx->cwd = 0x37f;
		if (cpu_has_xmm)
			fx->mxcsr = MXCSR_DEFAULT;
	} else {
		struct i387_fsave_struct *fp = &tsk->thread.xstate->fsave;
		memset(fp, 0, xstate_size);
		fp->cwd = 0xffff037fu;
		fp->swd = 0xffff0000u;
		fp->twd = 0xffffffffu;
		fp->fos = 0xffff0000u;
	}
	/*
	 * Only the device not available exception or ptrace can call init_fpu.
	 */
	set_stopped_child_used_math(tsk);
	return 0;
}

/*
 * The xstateregs_active() routine is the same as the fpregs_active() routine,
 * as the "regset->n" for the xstate regset will be updated based on the feature
 * capabilites supported by the xsave.
 */
int fpregs_active(struct task_struct *target, const struct user_regset *regset)
{
	return tsk_used_math(target) ? regset->n : 0;
}

int xfpregs_active(struct task_struct *target, const struct user_regset *regset)
{
	return (cpu_has_fxsr && tsk_used_math(target)) ? regset->n : 0;
}

int xfpregs_get(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		void *kbuf, void __user *ubuf)
{
	int ret;

	if (!cpu_has_fxsr)
		return -ENODEV;

	ret = init_fpu(target);
	if (ret)
		return ret;

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &target->thread.xstate->fxsave, 0, -1);
}

int xfpregs_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf)
{
	int ret;

	if (!cpu_has_fxsr)
		return -ENODEV;

	ret = init_fpu(target);
	if (ret)
		return ret;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &target->thread.xstate->fxsave, 0, -1);

	/*
	 * mxcsr reserved bits must be masked to zero for security reasons.
	 */
	target->thread.xstate->fxsave.mxcsr &= mxcsr_feature_mask;

	/*
	 * update the header bits in the xsave header, indicating the
	 * presence of FP and SSE state.
	 */
	if (cpu_has_xsave)
		target->thread.xstate->xsave.xsave_hdr.xstate_bv |= XSTATE_FPSSE;

	return ret;
}

int xstateregs_get(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		void *kbuf, void __user *ubuf)
{
	int ret;

	if (!cpu_has_xsave)
		return -ENODEV;

	ret = init_fpu(target);
	if (ret)
		return ret;

	/*
	 * Copy the 48bytes defined by the software first into the xstate
	 * memory layout in the thread struct, so that we can copy the entire
	 * xstateregs to the user using one user_regset_copyout().
	 */
	memcpy(&target->thread.xstate->fxsave.sw_reserved,
	       xstate_fx_sw_bytes, sizeof(xstate_fx_sw_bytes));

	/*
	 * Copy the xstate memory layout.
	 */
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &target->thread.xstate->xsave, 0, -1);
	return ret;
}

int xstateregs_set(struct task_struct *target, const struct user_regset *regset,
		  unsigned int pos, unsigned int count,
		  const void *kbuf, const void __user *ubuf)
{
	int ret;
	struct xsave_hdr_struct *xsave_hdr;

	if (!cpu_has_xsave)
		return -ENODEV;

	ret = init_fpu(target);
	if (ret)
		return ret;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &target->thread.xstate->xsave, 0, -1);

	/*
	 * mxcsr reserved bits must be masked to zero for security reasons.
	 */
	target->thread.xstate->fxsave.mxcsr &= mxcsr_feature_mask;

	xsave_hdr = &target->thread.xstate->xsave.xsave_hdr;

	xsave_hdr->xstate_bv &= pcntxt_mask;
	/*
	 * These bits must be zero.
	 */
	xsave_hdr->reserved1[0] = xsave_hdr->reserved1[1] = 0;

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

#define FPREG_ADDR(f, n)	((void *)&(f)->st_space + (n) * 16);
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

static void
convert_from_fxsr(struct user_i387_ia32_struct *env, struct task_struct *tsk)
{
	struct i387_fxsave_struct *fxsave = &tsk->thread.xstate->fxsave;
	struct _fpreg *to = (struct _fpreg *) &env->st_space[0];
	struct _fpxreg *from = (struct _fpxreg *) &fxsave->st_space[0];
	int i;

	env->cwd = fxsave->cwd | 0xffff0000u;
	env->swd = fxsave->swd | 0xffff0000u;
	env->twd = twd_fxsr_to_i387(fxsave);

#ifdef CONFIG_X86_64
	env->fip = fxsave->rip;
	env->foo = fxsave->rdp;
	if (tsk == current) {
		/*
		 * should be actually ds/cs at fpu exception time, but
		 * that information is not available in 64bit mode.
		 */
		asm("mov %%ds, %[fos]" : [fos] "=r" (env->fos));
		asm("mov %%cs, %[fcs]" : [fcs] "=r" (env->fcs));
	} else {
		struct pt_regs *regs = task_pt_regs(tsk);

		env->fos = 0xffff0000 | tsk->thread.ds;
		env->fcs = regs->cs;
	}
#else
	env->fip = fxsave->fip;
	env->fcs = (u16) fxsave->fcs | ((u32) fxsave->fop << 16);
	env->foo = fxsave->foo;
	env->fos = fxsave->fos;
#endif

	for (i = 0; i < 8; ++i)
		memcpy(&to[i], &from[i], sizeof(to[0]));
}

static void convert_to_fxsr(struct task_struct *tsk,
			    const struct user_i387_ia32_struct *env)

{
	struct i387_fxsave_struct *fxsave = &tsk->thread.xstate->fxsave;
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
	struct user_i387_ia32_struct env;
	int ret;

	ret = init_fpu(target);
	if (ret)
		return ret;

	if (!HAVE_HWFP)
		return fpregs_soft_get(target, regset, pos, count, kbuf, ubuf);

	if (!cpu_has_fxsr) {
		return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					   &target->thread.xstate->fsave, 0,
					   -1);
	}

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
	struct user_i387_ia32_struct env;
	int ret;

	ret = init_fpu(target);
	if (ret)
		return ret;

	if (!HAVE_HWFP)
		return fpregs_soft_set(target, regset, pos, count, kbuf, ubuf);

	if (!cpu_has_fxsr) {
		return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					  &target->thread.xstate->fsave, 0, -1);
	}

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
		target->thread.xstate->xsave.xsave_hdr.xstate_bv |= XSTATE_FP;
	return ret;
}

/*
 * Signal frame handlers.
 */

static inline int save_i387_fsave(struct _fpstate_ia32 __user *buf)
{
	struct task_struct *tsk = current;
	struct i387_fsave_struct *fp = &tsk->thread.xstate->fsave;

	fp->status = fp->swd;
	if (__copy_to_user(buf, fp, sizeof(struct i387_fsave_struct)))
		return -1;
	return 1;
}

static int save_i387_fxsave(struct _fpstate_ia32 __user *buf)
{
	struct task_struct *tsk = current;
	struct i387_fxsave_struct *fx = &tsk->thread.xstate->fxsave;
	struct user_i387_ia32_struct env;
	int err = 0;

	convert_from_fxsr(&env, tsk);
	if (__copy_to_user(buf, &env, sizeof(env)))
		return -1;

	err |= __put_user(fx->swd, &buf->status);
	err |= __put_user(X86_FXSR_MAGIC, &buf->magic);
	if (err)
		return -1;

	if (__copy_to_user(&buf->_fxsr_env[0], fx, xstate_size))
		return -1;
	return 1;
}

static int save_i387_xsave(void __user *buf)
{
	struct task_struct *tsk = current;
	struct _fpstate_ia32 __user *fx = buf;
	int err = 0;

	/*
	 * For legacy compatible, we always set FP/SSE bits in the bit
	 * vector while saving the state to the user context.
	 * This will enable us capturing any changes(during sigreturn) to
	 * the FP/SSE bits by the legacy applications which don't touch
	 * xstate_bv in the xsave header.
	 *
	 * xsave aware applications can change the xstate_bv in the xsave
	 * header as well as change any contents in the memory layout.
	 * xrestore as part of sigreturn will capture all the changes.
	 */
	tsk->thread.xstate->xsave.xsave_hdr.xstate_bv |= XSTATE_FPSSE;

	if (save_i387_fxsave(fx) < 0)
		return -1;

	err = __copy_to_user(&fx->sw_reserved, &fx_sw_reserved_ia32,
			     sizeof(struct _fpx_sw_bytes));
	err |= __put_user(FP_XSTATE_MAGIC2,
			  (__u32 __user *) (buf + sig_xstate_ia32_size
					    - FP_XSTATE_MAGIC2_SIZE));
	if (err)
		return -1;

	return 1;
}

int save_i387_xstate_ia32(void __user *buf)
{
	struct _fpstate_ia32 __user *fp = (struct _fpstate_ia32 __user *) buf;
	struct task_struct *tsk = current;

	if (!used_math())
		return 0;

	if (!access_ok(VERIFY_WRITE, buf, sig_xstate_ia32_size))
		return -EACCES;
	/*
	 * This will cause a "finit" to be triggered by the next
	 * attempted FPU operation by the 'current' process.
	 */
	clear_used_math();

	if (!HAVE_HWFP) {
		return fpregs_soft_get(current, NULL,
				       0, sizeof(struct user_i387_ia32_struct),
				       NULL, fp) ? -1 : 1;
	}

	unlazy_fpu(tsk);

	if (cpu_has_xsave)
		return save_i387_xsave(fp);
	if (cpu_has_fxsr)
		return save_i387_fxsave(fp);
	else
		return save_i387_fsave(fp);
}

static inline int restore_i387_fsave(struct _fpstate_ia32 __user *buf)
{
	struct task_struct *tsk = current;

	return __copy_from_user(&tsk->thread.xstate->fsave, buf,
				sizeof(struct i387_fsave_struct));
}

static int restore_i387_fxsave(struct _fpstate_ia32 __user *buf,
			       unsigned int size)
{
	struct task_struct *tsk = current;
	struct user_i387_ia32_struct env;
	int err;

	err = __copy_from_user(&tsk->thread.xstate->fxsave, &buf->_fxsr_env[0],
			       size);
	/* mxcsr reserved bits must be masked to zero for security reasons */
	tsk->thread.xstate->fxsave.mxcsr &= mxcsr_feature_mask;
	if (err || __copy_from_user(&env, buf, sizeof(env)))
		return 1;
	convert_to_fxsr(tsk, &env);

	return 0;
}

static int restore_i387_xsave(void __user *buf)
{
	struct _fpx_sw_bytes fx_sw_user;
	struct _fpstate_ia32 __user *fx_user =
			((struct _fpstate_ia32 __user *) buf);
	struct i387_fxsave_struct __user *fx =
		(struct i387_fxsave_struct __user *) &fx_user->_fxsr_env[0];
	struct xsave_hdr_struct *xsave_hdr =
				&current->thread.xstate->xsave.xsave_hdr;
	u64 mask;
	int err;

	if (check_for_xstate(fx, buf, &fx_sw_user))
		goto fx_only;

	mask = fx_sw_user.xstate_bv;

	err = restore_i387_fxsave(buf, fx_sw_user.xstate_size);

	xsave_hdr->xstate_bv &= pcntxt_mask;
	/*
	 * These bits must be zero.
	 */
	xsave_hdr->reserved1[0] = xsave_hdr->reserved1[1] = 0;

	/*
	 * Init the state that is not present in the memory layout
	 * and enabled by the OS.
	 */
	mask = ~(pcntxt_mask & ~mask);
	xsave_hdr->xstate_bv &= mask;

	return err;
fx_only:
	/*
	 * Couldn't find the extended state information in the memory
	 * layout. Restore the FP/SSE and init the other extended state
	 * enabled by the OS.
	 */
	xsave_hdr->xstate_bv = XSTATE_FPSSE;
	return restore_i387_fxsave(buf, sizeof(struct i387_fxsave_struct));
}

int restore_i387_xstate_ia32(void __user *buf)
{
	int err;
	struct task_struct *tsk = current;
	struct _fpstate_ia32 __user *fp = (struct _fpstate_ia32 __user *) buf;

	if (HAVE_HWFP)
		clear_fpu(tsk);

	if (!buf) {
		if (used_math()) {
			clear_fpu(tsk);
			clear_used_math();
		}

		return 0;
	} else
		if (!access_ok(VERIFY_READ, buf, sig_xstate_ia32_size))
			return -EACCES;

	if (!used_math()) {
		err = init_fpu(tsk);
		if (err)
			return err;
	}

	if (HAVE_HWFP) {
		if (cpu_has_xsave)
			err = restore_i387_xsave(buf);
		else if (cpu_has_fxsr)
			err = restore_i387_fxsave(fp, sizeof(struct
							   i387_fxsave_struct));
		else
			err = restore_i387_fsave(fp);
	} else {
		err = fpregs_soft_set(current, NULL,
				      0, sizeof(struct user_i387_ia32_struct),
				      NULL, fp) != 0;
	}
	set_used_math();

	return err;
}

/*
 * FPU state for core dumps.
 * This is only used for a.out dumps now.
 * It is declared generically using elf_fpregset_t (which is
 * struct user_i387_struct) but is in fact only used for 32-bit
 * dumps, so on 64-bit it is really struct user_i387_ia32_struct.
 */
int dump_fpu(struct pt_regs *regs, struct user_i387_struct *fpu)
{
	struct task_struct *tsk = current;
	int fpvalid;

	fpvalid = !!used_math();
	if (fpvalid)
		fpvalid = !fpregs_get(tsk, NULL,
				      0, sizeof(struct user_i387_ia32_struct),
				      fpu, NULL);

	return fpvalid;
}
EXPORT_SYMBOL(dump_fpu);

#endif	/* CONFIG_X86_32 || CONFIG_IA32_EMULATION */
