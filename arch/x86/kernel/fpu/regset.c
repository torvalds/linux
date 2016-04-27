/*
 * FPU register's regset abstraction, for ptrace, core dumps, etc.
 */
#include <asm/fpu/internal.h>
#include <asm/fpu/signal.h>
#include <asm/fpu/regset.h>

/*
 * The xstateregs_active() routine is the same as the regset_fpregs_active() routine,
 * as the "regset->n" for the xstate regset will be updated based on the feature
 * capabilities supported by the xsave.
 */
int regset_fpregs_active(struct task_struct *target, const struct user_regset *regset)
{
	struct fpu *target_fpu = &target->thread.fpu;

	return target_fpu->fpstate_active ? regset->n : 0;
}

int regset_xregset_fpregs_active(struct task_struct *target, const struct user_regset *regset)
{
	struct fpu *target_fpu = &target->thread.fpu;

	return (cpu_has_fxsr && target_fpu->fpstate_active) ? regset->n : 0;
}

int xfpregs_get(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		void *kbuf, void __user *ubuf)
{
	struct fpu *fpu = &target->thread.fpu;

	if (!cpu_has_fxsr)
		return -ENODEV;

	fpu__activate_fpstate_read(fpu);
	fpstate_sanitize_xstate(fpu);

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &fpu->state.fxsave, 0, -1);
}

int xfpregs_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf)
{
	struct fpu *fpu = &target->thread.fpu;
	int ret;

	if (!cpu_has_fxsr)
		return -ENODEV;

	fpu__activate_fpstate_write(fpu);
	fpstate_sanitize_xstate(fpu);

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &fpu->state.fxsave, 0, -1);

	/*
	 * mxcsr reserved bits must be masked to zero for security reasons.
	 */
	fpu->state.fxsave.mxcsr &= mxcsr_feature_mask;

	/*
	 * update the header bits in the xsave header, indicating the
	 * presence of FP and SSE state.
	 */
	if (cpu_has_xsave)
		fpu->state.xsave.header.xfeatures |= XFEATURE_MASK_FPSSE;

	return ret;
}

int xstateregs_get(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		void *kbuf, void __user *ubuf)
{
	struct fpu *fpu = &target->thread.fpu;
	struct xregs_state *xsave;
	int ret;

	if (!cpu_has_xsave)
		return -ENODEV;

	fpu__activate_fpstate_read(fpu);

	xsave = &fpu->state.xsave;

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
	struct xregs_state *xsave;
	int ret;

	if (!cpu_has_xsave)
		return -ENODEV;

	fpu__activate_fpstate_write(fpu);

	xsave = &fpu->state.xsave;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, xsave, 0, -1);
	/*
	 * mxcsr reserved bits must be masked to zero for security reasons.
	 */
	xsave->i387.mxcsr &= mxcsr_feature_mask;
	xsave->header.xfeatures &= xfeatures_mask;
	/*
	 * These bits must be zero.
	 */
	memset(&xsave->header.reserved, 0, 48);

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

static inline u32 twd_fxsr_to_i387(struct fxregs_state *fxsave)
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
	struct fxregs_state *fxsave = &tsk->thread.fpu.state.fxsave;
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
	struct fxregs_state *fxsave = &tsk->thread.fpu.state.fxsave;
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

	fpu__activate_fpstate_read(fpu);

	if (!static_cpu_has(X86_FEATURE_FPU))
		return fpregs_soft_get(target, regset, pos, count, kbuf, ubuf);

	if (!cpu_has_fxsr)
		return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					   &fpu->state.fsave, 0,
					   -1);

	fpstate_sanitize_xstate(fpu);

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

	fpu__activate_fpstate_write(fpu);
	fpstate_sanitize_xstate(fpu);

	if (!static_cpu_has(X86_FEATURE_FPU))
		return fpregs_soft_set(target, regset, pos, count, kbuf, ubuf);

	if (!cpu_has_fxsr)
		return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					  &fpu->state.fsave, 0,
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
		fpu->state.xsave.header.xfeatures |= XFEATURE_MASK_FP;
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
