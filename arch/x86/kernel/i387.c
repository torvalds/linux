/*
 *  Copyright (C) 1994 Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *  General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/regset.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/math_emu.h>
#include <asm/sigcontext.h>
#include <asm/user.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>

#ifdef CONFIG_X86_64

#include <asm/sigcontext32.h>
#include <asm/user32.h>

#else

#define	save_i387_ia32		save_i387
#define	restore_i387_ia32	restore_i387

#define _fpstate_ia32 		_fpstate
#define user_i387_ia32_struct	user_i387_struct
#define user32_fxsr_struct	user_fxsr_struct

#endif

#ifdef CONFIG_MATH_EMULATION
#define HAVE_HWFP (boot_cpu_data.hard_math)
#else
#define HAVE_HWFP 1
#endif

static unsigned int mxcsr_feature_mask __read_mostly = 0xffffffffu;

void mxcsr_feature_mask_init(void)
{
	unsigned long mask = 0;
	clts();
	if (cpu_has_fxsr) {
		memset(&current->thread.i387.fxsave, 0,
		       sizeof(struct i387_fxsave_struct));
		asm volatile("fxsave %0" : : "m" (current->thread.i387.fxsave));
		mask = current->thread.i387.fxsave.mxcsr_mask;
		if (mask == 0)
			mask = 0x0000ffbf;
	}
	mxcsr_feature_mask &= mask;
	stts();
}

#ifdef CONFIG_X86_64
/*
 * Called at bootup to set up the initial FPU state that is later cloned
 * into all processes.
 */
void __cpuinit fpu_init(void)
{
	unsigned long oldcr0 = read_cr0();
	extern void __bad_fxsave_alignment(void);

	if (offsetof(struct task_struct, thread.i387.fxsave) & 15)
		__bad_fxsave_alignment();
	set_in_cr4(X86_CR4_OSFXSR);
	set_in_cr4(X86_CR4_OSXMMEXCPT);

	write_cr0(oldcr0 & ~((1UL<<3)|(1UL<<2))); /* clear TS and EM */

	mxcsr_feature_mask_init();
	/* clean state in init */
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
void init_fpu(struct task_struct *tsk)
{
	if (tsk_used_math(tsk)) {
		if (tsk == current)
			unlazy_fpu(tsk);
		return;
	}

	if (cpu_has_fxsr) {
		memset(&tsk->thread.i387.fxsave, 0,
		       sizeof(struct i387_fxsave_struct));
		tsk->thread.i387.fxsave.cwd = 0x37f;
		if (cpu_has_xmm)
			tsk->thread.i387.fxsave.mxcsr = MXCSR_DEFAULT;
	} else {
		memset(&tsk->thread.i387.fsave, 0,
		       sizeof(struct i387_fsave_struct));
		tsk->thread.i387.fsave.cwd = 0xffff037fu;
		tsk->thread.i387.fsave.swd = 0xffff0000u;
		tsk->thread.i387.fsave.twd = 0xffffffffu;
		tsk->thread.i387.fsave.fos = 0xffff0000u;
	}
	/*
	 * Only the device not available exception or ptrace can call init_fpu.
	 */
	set_stopped_child_used_math(tsk);
}

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
	if (!cpu_has_fxsr)
		return -ENODEV;

	init_fpu(target);

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &target->thread.i387.fxsave, 0, -1);
}

int xfpregs_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf)
{
	int ret;

	if (!cpu_has_fxsr)
		return -ENODEV;

	init_fpu(target);
	set_stopped_child_used_math(target);

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &target->thread.i387.fxsave, 0, -1);

	/*
	 * mxcsr reserved bits must be masked to zero for security reasons.
	 */
	target->thread.i387.fxsave.mxcsr &= mxcsr_feature_mask;

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

static void convert_from_fxsr(struct user_i387_ia32_struct *env,
			      struct task_struct *tsk)
{
	struct i387_fxsave_struct *fxsave = &tsk->thread.i387.fxsave;
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
		asm("mov %%ds,%0" : "=r" (env->fos));
		asm("mov %%cs,%0" : "=r" (env->fcs));
	} else {
		struct pt_regs *regs = task_pt_regs(tsk);
		env->fos = 0xffff0000 | tsk->thread.ds;
		env->fcs = regs->cs;
	}
#else
	env->fip = fxsave->fip;
	env->fcs = fxsave->fcs;
	env->foo = fxsave->foo;
	env->fos = fxsave->fos;
#endif

	for (i = 0; i < 8; ++i)
		memcpy(&to[i], &from[i], sizeof(to[0]));
}

static void convert_to_fxsr(struct task_struct *tsk,
			    const struct user_i387_ia32_struct *env)

{
	struct i387_fxsave_struct *fxsave = &tsk->thread.i387.fxsave;
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

	if (!HAVE_HWFP)
		return fpregs_soft_get(target, regset, pos, count, kbuf, ubuf);

	init_fpu(target);

	if (!cpu_has_fxsr)
		return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					   &target->thread.i387.fsave, 0, -1);

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

	if (!HAVE_HWFP)
		return fpregs_soft_set(target, regset, pos, count, kbuf, ubuf);

	init_fpu(target);
	set_stopped_child_used_math(target);

	if (!cpu_has_fxsr)
		return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					  &target->thread.i387.fsave, 0, -1);

	if (pos > 0 || count < sizeof(env))
		convert_from_fxsr(&env, target);

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &env, 0, -1);
	if (!ret)
		convert_to_fxsr(target, &env);

	return ret;
}

/*
 * Signal frame handlers.
 */

static inline int save_i387_fsave(struct _fpstate_ia32 __user *buf)
{
	struct task_struct *tsk = current;

	unlazy_fpu(tsk);
	tsk->thread.i387.fsave.status = tsk->thread.i387.fsave.swd;
	if (__copy_to_user(buf, &tsk->thread.i387.fsave,
			   sizeof(struct i387_fsave_struct)))
		return -1;
	return 1;
}

static int save_i387_fxsave(struct _fpstate_ia32 __user *buf)
{
	struct task_struct *tsk = current;
	struct user_i387_ia32_struct env;
	int err = 0;

	unlazy_fpu(tsk);

	convert_from_fxsr(&env, tsk);
	if (__copy_to_user(buf, &env, sizeof(env)))
		return -1;

	err |= __put_user(tsk->thread.i387.fxsave.swd, &buf->status);
	err |= __put_user(X86_FXSR_MAGIC, &buf->magic);
	if (err)
		return -1;

	if (__copy_to_user(&buf->_fxsr_env[0], &tsk->thread.i387.fxsave,
			   sizeof(struct i387_fxsave_struct)))
		return -1;
	return 1;
}

int save_i387_ia32(struct _fpstate_ia32 __user *buf)
{
	if (!used_math())
		return 0;

	/* This will cause a "finit" to be triggered by the next
	 * attempted FPU operation by the 'current' process.
	 */
	clear_used_math();

	if (HAVE_HWFP) {
		if (cpu_has_fxsr) {
			return save_i387_fxsave(buf);
		} else {
			return save_i387_fsave(buf);
		}
	} else {
		return fpregs_soft_get(current, NULL,
				       0, sizeof(struct user_i387_ia32_struct),
				       NULL, buf) ? -1 : 1;
	}
}

static inline int restore_i387_fsave(struct _fpstate_ia32 __user *buf)
{
	struct task_struct *tsk = current;
	clear_fpu(tsk);
	return __copy_from_user(&tsk->thread.i387.fsave, buf,
				sizeof(struct i387_fsave_struct));
}

static int restore_i387_fxsave(struct _fpstate_ia32 __user *buf)
{
	int err;
	struct task_struct *tsk = current;
	struct user_i387_ia32_struct env;
	clear_fpu(tsk);
	err = __copy_from_user(&tsk->thread.i387.fxsave, &buf->_fxsr_env[0],
			       sizeof(struct i387_fxsave_struct));
	/* mxcsr reserved bits must be masked to zero for security reasons */
	tsk->thread.i387.fxsave.mxcsr &= mxcsr_feature_mask;
	if (err || __copy_from_user(&env, buf, sizeof(env)))
		return 1;
	convert_to_fxsr(tsk, &env);
	return 0;
}

int restore_i387_ia32(struct _fpstate_ia32 __user *buf)
{
	int err;

	if (HAVE_HWFP) {
		if (cpu_has_fxsr) {
			err = restore_i387_fxsave(buf);
		} else {
			err = restore_i387_fsave(buf);
		}
	} else {
		err = fpregs_soft_set(current, NULL,
				      0, sizeof(struct user_i387_ia32_struct),
				      NULL, buf) != 0;
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
	int fpvalid;
	struct task_struct *tsk = current;

	fpvalid = !!used_math();
	if (fpvalid)
		fpvalid = !fpregs_get(tsk, NULL,
				      0, sizeof(struct user_i387_ia32_struct),
				      fpu, NULL);

	return fpvalid;
}
EXPORT_SYMBOL(dump_fpu);

#endif	/* CONFIG_X86_32 || CONFIG_IA32_EMULATION */
