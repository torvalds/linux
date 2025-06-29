// SPDX-License-Identifier: GPL-2.0
/*
 * FPU register's regset abstraction, for ptrace, core dumps, etc.
 */
#include <linux/sched/task_stack.h>
#include <linux/vmalloc.h>

#include <asm/fpu/api.h>
#include <asm/fpu/signal.h>
#include <asm/fpu/regset.h>
#include <asm/prctl.h>

#include "context.h"
#include "internal.h"
#include "legacy.h"
#include "xstate.h"

/*
 * The xstateregs_active() routine is the same as the regset_fpregs_active() routine,
 * as the "regset->n" for the xstate regset will be updated based on the feature
 * capabilities supported by the xsave.
 */
int regset_fpregs_active(struct task_struct *target, const struct user_regset *regset)
{
	return regset->n;
}

int regset_xregset_fpregs_active(struct task_struct *target, const struct user_regset *regset)
{
	if (boot_cpu_has(X86_FEATURE_FXSR))
		return regset->n;
	else
		return 0;
}

/*
 * The regset get() functions are invoked from:
 *
 *   - coredump to dump the current task's fpstate. If the current task
 *     owns the FPU then the memory state has to be synchronized and the
 *     FPU register state preserved. Otherwise fpstate is already in sync.
 *
 *   - ptrace to dump fpstate of a stopped task, in which case the registers
 *     have already been saved to fpstate on context switch.
 */
static void sync_fpstate(struct fpu *fpu)
{
	if (fpu == x86_task_fpu(current))
		fpu_sync_fpstate(fpu);
}

/*
 * Invalidate cached FPU registers before modifying the stopped target
 * task's fpstate.
 *
 * This forces the target task on resume to restore the FPU registers from
 * modified fpstate. Otherwise the task might skip the restore and operate
 * with the cached FPU registers which discards the modifications.
 */
static void fpu_force_restore(struct fpu *fpu)
{
	/*
	 * Only stopped child tasks can be used to modify the FPU
	 * state in the fpstate buffer:
	 */
	WARN_ON_FPU(fpu == x86_task_fpu(current));

	__fpu_invalidate_fpregs_state(fpu);
}

int xfpregs_get(struct task_struct *target, const struct user_regset *regset,
		struct membuf to)
{
	struct fpu *fpu = x86_task_fpu(target);

	if (!cpu_feature_enabled(X86_FEATURE_FXSR))
		return -ENODEV;

	sync_fpstate(fpu);

	if (!use_xsave()) {
		return membuf_write(&to, &fpu->fpstate->regs.fxsave,
				    sizeof(fpu->fpstate->regs.fxsave));
	}

	copy_xstate_to_uabi_buf(to, target, XSTATE_COPY_FX);
	return 0;
}

int xfpregs_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf)
{
	struct fpu *fpu = x86_task_fpu(target);
	struct fxregs_state newstate;
	int ret;

	if (!cpu_feature_enabled(X86_FEATURE_FXSR))
		return -ENODEV;

	/* No funny business with partial or oversized writes is permitted. */
	if (pos != 0 || count != sizeof(newstate))
		return -EINVAL;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &newstate, 0, -1);
	if (ret)
		return ret;

	/* Do not allow an invalid MXCSR value. */
	if (newstate.mxcsr & ~mxcsr_feature_mask)
		return -EINVAL;

	fpu_force_restore(fpu);

	/* Copy the state  */
	memcpy(&fpu->fpstate->regs.fxsave, &newstate, sizeof(newstate));

	/* Clear xmm8..15 for 32-bit callers */
	BUILD_BUG_ON(sizeof(fpu->__fpstate.regs.fxsave.xmm_space) != 16 * 16);
	if (in_ia32_syscall())
		memset(&fpu->fpstate->regs.fxsave.xmm_space[8*4], 0, 8 * 16);

	/* Mark FP and SSE as in use when XSAVE is enabled */
	if (use_xsave())
		fpu->fpstate->regs.xsave.header.xfeatures |= XFEATURE_MASK_FPSSE;

	return 0;
}

int xstateregs_get(struct task_struct *target, const struct user_regset *regset,
		struct membuf to)
{
	if (!cpu_feature_enabled(X86_FEATURE_XSAVE))
		return -ENODEV;

	sync_fpstate(x86_task_fpu(target));

	copy_xstate_to_uabi_buf(to, target, XSTATE_COPY_XSAVE);
	return 0;
}

int xstateregs_set(struct task_struct *target, const struct user_regset *regset,
		  unsigned int pos, unsigned int count,
		  const void *kbuf, const void __user *ubuf)
{
	struct fpu *fpu = x86_task_fpu(target);
	struct xregs_state *tmpbuf = NULL;
	int ret;

	if (!cpu_feature_enabled(X86_FEATURE_XSAVE))
		return -ENODEV;

	/*
	 * A whole standard-format XSAVE buffer is needed:
	 */
	if (pos != 0 || count != fpu_user_cfg.max_size)
		return -EFAULT;

	if (!kbuf) {
		tmpbuf = vmalloc(count);
		if (!tmpbuf)
			return -ENOMEM;

		if (copy_from_user(tmpbuf, ubuf, count)) {
			ret = -EFAULT;
			goto out;
		}
	}

	fpu_force_restore(fpu);
	ret = copy_uabi_from_kernel_to_xstate(fpu->fpstate, kbuf ?: tmpbuf, &target->thread.pkru);

out:
	vfree(tmpbuf);
	return ret;
}

#ifdef CONFIG_X86_USER_SHADOW_STACK
int ssp_active(struct task_struct *target, const struct user_regset *regset)
{
	if (target->thread.features & ARCH_SHSTK_SHSTK)
		return regset->n;

	return 0;
}

int ssp_get(struct task_struct *target, const struct user_regset *regset,
	    struct membuf to)
{
	struct fpu *fpu = x86_task_fpu(target);
	struct cet_user_state *cetregs;

	if (!cpu_feature_enabled(X86_FEATURE_USER_SHSTK) ||
	    !ssp_active(target, regset))
		return -ENODEV;

	sync_fpstate(fpu);
	cetregs = get_xsave_addr(&fpu->fpstate->regs.xsave, XFEATURE_CET_USER);
	if (WARN_ON(!cetregs)) {
		/*
		 * This shouldn't ever be NULL because shadow stack was
		 * verified to be enabled above. This means
		 * MSR_IA32_U_CET.CET_SHSTK_EN should be 1 and so
		 * XFEATURE_CET_USER should not be in the init state.
		 */
		return -ENODEV;
	}

	return membuf_write(&to, (unsigned long *)&cetregs->user_ssp,
			    sizeof(cetregs->user_ssp));
}

int ssp_set(struct task_struct *target, const struct user_regset *regset,
	    unsigned int pos, unsigned int count,
	    const void *kbuf, const void __user *ubuf)
{
	struct fpu *fpu = x86_task_fpu(target);
	struct xregs_state *xsave = &fpu->fpstate->regs.xsave;
	struct cet_user_state *cetregs;
	unsigned long user_ssp;
	int r;

	if (!cpu_feature_enabled(X86_FEATURE_USER_SHSTK) ||
	    !ssp_active(target, regset))
		return -ENODEV;

	if (pos != 0 || count != sizeof(user_ssp))
		return -EINVAL;

	r = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &user_ssp, 0, -1);
	if (r)
		return r;

	/*
	 * Some kernel instructions (IRET, etc) can cause exceptions in the case
	 * of disallowed CET register values. Just prevent invalid values.
	 */
	if (user_ssp >= TASK_SIZE_MAX || !IS_ALIGNED(user_ssp, 8))
		return -EINVAL;

	fpu_force_restore(fpu);

	cetregs = get_xsave_addr(xsave, XFEATURE_CET_USER);
	if (WARN_ON(!cetregs)) {
		/*
		 * This shouldn't ever be NULL because shadow stack was
		 * verified to be enabled above. This means
		 * MSR_IA32_U_CET.CET_SHSTK_EN should be 1 and so
		 * XFEATURE_CET_USER should not be in the init state.
		 */
		return -ENODEV;
	}

	cetregs->user_ssp = user_ssp;
	return 0;
}
#endif /* CONFIG_X86_USER_SHADOW_STACK */

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

static void __convert_from_fxsr(struct user_i387_ia32_struct *env,
				struct task_struct *tsk,
				struct fxregs_state *fxsave)
{
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

void
convert_from_fxsr(struct user_i387_ia32_struct *env, struct task_struct *tsk)
{
	__convert_from_fxsr(env, tsk, &x86_task_fpu(tsk)->fpstate->regs.fxsave);
}

void convert_to_fxsr(struct fxregs_state *fxsave,
		     const struct user_i387_ia32_struct *env)

{
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
	       struct membuf to)
{
	struct fpu *fpu = x86_task_fpu(target);
	struct user_i387_ia32_struct env;
	struct fxregs_state fxsave, *fx;

	sync_fpstate(fpu);

	if (!cpu_feature_enabled(X86_FEATURE_FPU))
		return fpregs_soft_get(target, regset, to);

	if (!cpu_feature_enabled(X86_FEATURE_FXSR)) {
		return membuf_write(&to, &fpu->fpstate->regs.fsave,
				    sizeof(struct fregs_state));
	}

	if (use_xsave()) {
		struct membuf mb = { .p = &fxsave, .left = sizeof(fxsave) };

		/* Handle init state optimized xstate correctly */
		copy_xstate_to_uabi_buf(mb, target, XSTATE_COPY_FP);
		fx = &fxsave;
	} else {
		fx = &fpu->fpstate->regs.fxsave;
	}

	__convert_from_fxsr(&env, target, fx);
	return membuf_write(&to, &env, sizeof(env));
}

int fpregs_set(struct task_struct *target, const struct user_regset *regset,
	       unsigned int pos, unsigned int count,
	       const void *kbuf, const void __user *ubuf)
{
	struct fpu *fpu = x86_task_fpu(target);
	struct user_i387_ia32_struct env;
	int ret;

	/* No funny business with partial or oversized writes is permitted. */
	if (pos != 0 || count != sizeof(struct user_i387_ia32_struct))
		return -EINVAL;

	if (!cpu_feature_enabled(X86_FEATURE_FPU))
		return fpregs_soft_set(target, regset, pos, count, kbuf, ubuf);

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, &env, 0, -1);
	if (ret)
		return ret;

	fpu_force_restore(fpu);

	if (cpu_feature_enabled(X86_FEATURE_FXSR))
		convert_to_fxsr(&fpu->fpstate->regs.fxsave, &env);
	else
		memcpy(&fpu->fpstate->regs.fsave, &env, sizeof(env));

	/*
	 * Update the header bit in the xsave header, indicating the
	 * presence of FP.
	 */
	if (cpu_feature_enabled(X86_FEATURE_XSAVE))
		fpu->fpstate->regs.xsave.header.xfeatures |= XFEATURE_MASK_FP;

	return 0;
}

#endif	/* CONFIG_X86_32 || CONFIG_IA32_EMULATION */
