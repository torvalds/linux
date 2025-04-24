/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __X86_KERNEL_FPU_XSTATE_H
#define __X86_KERNEL_FPU_XSTATE_H

#include <asm/cpufeature.h>
#include <asm/fpu/xstate.h>
#include <asm/fpu/xcr.h>

#ifdef CONFIG_X86_64
DECLARE_PER_CPU(u64, xfd_state);
#endif

static inline void xstate_init_xcomp_bv(struct xregs_state *xsave, u64 mask)
{
	/*
	 * XRSTORS requires these bits set in xcomp_bv, or it will
	 * trigger #GP:
	 */
	if (cpu_feature_enabled(X86_FEATURE_XCOMPACTED))
		xsave->header.xcomp_bv = mask | XCOMP_BV_COMPACTED_FORMAT;
}

static inline u64 xstate_get_group_perm(bool guest)
{
	struct fpu *fpu = &current->group_leader->thread.fpu;
	struct fpu_state_perm *perm;

	/* Pairs with WRITE_ONCE() in xstate_request_perm() */
	perm = guest ? &fpu->guest_perm : &fpu->perm;
	return READ_ONCE(perm->__state_perm);
}

static inline u64 xstate_get_host_group_perm(void)
{
	return xstate_get_group_perm(false);
}

enum xstate_copy_mode {
	XSTATE_COPY_FP,
	XSTATE_COPY_FX,
	XSTATE_COPY_XSAVE,
};

struct membuf;
extern void __copy_xstate_to_uabi_buf(struct membuf to, struct fpstate *fpstate,
				      u64 xfeatures, u32 pkru_val,
				      enum xstate_copy_mode copy_mode);
extern void copy_xstate_to_uabi_buf(struct membuf to, struct task_struct *tsk,
				    enum xstate_copy_mode mode);
extern int copy_uabi_from_kernel_to_xstate(struct fpstate *fpstate, const void *kbuf, u32 *pkru);
extern int copy_sigframe_from_user_to_xstate(struct task_struct *tsk, const void __user *ubuf);


extern void fpu__init_cpu_xstate(void);
extern void fpu__init_system_xstate(unsigned int legacy_size);

extern void __user *get_xsave_addr_user(struct xregs_state __user *xsave, int xfeature_nr);

static inline u64 xfeatures_mask_supervisor(void)
{
	return fpu_kernel_cfg.max_features & XFEATURE_MASK_SUPERVISOR_SUPPORTED;
}

static inline u64 xfeatures_mask_independent(void)
{
	if (!cpu_feature_enabled(X86_FEATURE_ARCH_LBR))
		return fpu_kernel_cfg.independent_features & ~XFEATURE_MASK_LBR;

	return fpu_kernel_cfg.independent_features;
}

/*
 * Update the value of PKRU register that was already pushed onto the signal frame.
 */
static inline int update_pkru_in_sigframe(struct xregs_state __user *buf, u64 mask, u32 pkru)
{
	u64 xstate_bv;
	int err;

	if (unlikely(!cpu_feature_enabled(X86_FEATURE_OSPKE)))
		return 0;

	/* Mark PKRU as in-use so that it is restored correctly. */
	xstate_bv = (mask & xfeatures_in_use()) | XFEATURE_MASK_PKRU;

	err =  __put_user(xstate_bv, &buf->header.xfeatures);
	if (err)
		return err;

	/* Update PKRU value in the userspace xsave buffer. */
	return __put_user(pkru, (unsigned int __user *)get_xsave_addr_user(buf, XFEATURE_PKRU));
}

/* XSAVE/XRSTOR wrapper functions */

#ifdef CONFIG_X86_64
#define REX_SUFFIX	"64"
#else
#define REX_SUFFIX
#endif

#define XSAVE		"xsave" REX_SUFFIX " %[xa]"
#define XSAVEOPT	"xsaveopt" REX_SUFFIX " %[xa]"
#define XSAVEC		"xsavec" REX_SUFFIX " %[xa]"
#define XSAVES		"xsaves" REX_SUFFIX " %[xa]"
#define XRSTOR		"xrstor" REX_SUFFIX " %[xa]"
#define XRSTORS		"xrstors" REX_SUFFIX " %[xa]"

/*
 * After this @err contains 0 on success or the trap number when the
 * operation raises an exception.
 *
 * The [xa] input parameter below represents the struct xregs_state pointer
 * and the asm symbolic name for the argument used in the XSAVE/XRSTOR insns
 * above.
 */
#define XSTATE_OP(op, st, lmask, hmask, err)				\
	asm volatile("1:" op "\n\t"					\
		     "xor %[err], %[err]\n"				\
		     "2:\n"						\
		     _ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_FAULT_MCE_SAFE)	\
		     : [err] "=a" (err)					\
		     : [xa] "m" (*(st)), "a" (lmask), "d" (hmask)	\
		     : "memory")

/*
 * If XSAVES is enabled, it replaces XSAVEC because it supports supervisor
 * states in addition to XSAVEC.
 *
 * Otherwise if XSAVEC is enabled, it replaces XSAVEOPT because it supports
 * compacted storage format in addition to XSAVEOPT.
 *
 * Otherwise, if XSAVEOPT is enabled, XSAVEOPT replaces XSAVE because XSAVEOPT
 * supports modified optimization which is not supported by XSAVE.
 *
 * Use XSAVE as a fallback.
 */
#define XSTATE_XSAVE(st, lmask, hmask, err)				\
	asm volatile("1: " ALTERNATIVE_3(XSAVE,				\
				   XSAVEOPT, X86_FEATURE_XSAVEOPT,	\
				   XSAVEC,   X86_FEATURE_XSAVEC,	\
				   XSAVES,   X86_FEATURE_XSAVES)	\
		     "\n\t"						\
		     "xor %[err], %[err]\n"				\
		     "3:\n"						\
		     _ASM_EXTABLE_TYPE_REG(1b, 3b, EX_TYPE_EFAULT_REG, %[err]) \
		     : [err] "=r" (err)					\
		     : [xa] "m" (*(st)), "a" (lmask), "d" (hmask)	\
		     : "memory")

/*
 * Use XRSTORS to restore context if it is enabled. XRSTORS supports compact
 * XSAVE area format.
 */
#define XSTATE_XRESTORE(st, lmask, hmask)				\
	asm volatile("1: " ALTERNATIVE(XRSTOR,				\
				 XRSTORS, X86_FEATURE_XSAVES)		\
		     "\n"						\
		     "3:\n"						\
		     _ASM_EXTABLE_TYPE(1b, 3b, EX_TYPE_FPU_RESTORE)	\
		     :							\
		     : [xa] "m" (*(st)), "a" (lmask), "d" (hmask)	\
		     : "memory")

#if defined(CONFIG_X86_64) && defined(CONFIG_X86_DEBUG_FPU)
extern void xfd_validate_state(struct fpstate *fpstate, u64 mask, bool rstor);
#else
static inline void xfd_validate_state(struct fpstate *fpstate, u64 mask, bool rstor) { }
#endif

#ifdef CONFIG_X86_64
static inline void xfd_set_state(u64 xfd)
{
	wrmsrl(MSR_IA32_XFD, xfd);
	__this_cpu_write(xfd_state, xfd);
}

static inline void xfd_update_state(struct fpstate *fpstate)
{
	if (fpu_state_size_dynamic()) {
		u64 xfd = fpstate->xfd;

		if (__this_cpu_read(xfd_state) != xfd)
			xfd_set_state(xfd);
	}
}

extern int __xfd_enable_feature(u64 which, struct fpu_guest *guest_fpu);
#else
static inline void xfd_set_state(u64 xfd) { }

static inline void xfd_update_state(struct fpstate *fpstate) { }

static inline int __xfd_enable_feature(u64 which, struct fpu_guest *guest_fpu) {
	return -EPERM;
}
#endif

/*
 * Save processor xstate to xsave area.
 *
 * Uses either XSAVE or XSAVEOPT or XSAVES depending on the CPU features
 * and command line options. The choice is permanent until the next reboot.
 */
static inline void os_xsave(struct fpstate *fpstate)
{
	u64 mask = fpstate->xfeatures;
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err;

	WARN_ON_FPU(!alternatives_patched);
	xfd_validate_state(fpstate, mask, false);

	XSTATE_XSAVE(&fpstate->regs.xsave, lmask, hmask, err);

	/* We should never fault when copying to a kernel buffer: */
	WARN_ON_FPU(err);
}

/*
 * Restore processor xstate from xsave area.
 *
 * Uses XRSTORS when XSAVES is used, XRSTOR otherwise.
 */
static inline void os_xrstor(struct fpstate *fpstate, u64 mask)
{
	u32 lmask = mask;
	u32 hmask = mask >> 32;

	xfd_validate_state(fpstate, mask, true);
	XSTATE_XRESTORE(&fpstate->regs.xsave, lmask, hmask);
}

/* Restore of supervisor state. Does not require XFD */
static inline void os_xrstor_supervisor(struct fpstate *fpstate)
{
	u64 mask = xfeatures_mask_supervisor();
	u32 lmask = mask;
	u32 hmask = mask >> 32;

	XSTATE_XRESTORE(&fpstate->regs.xsave, lmask, hmask);
}

/*
 * XSAVE itself always writes all requested xfeatures.  Removing features
 * from the request bitmap reduces the features which are written.
 * Generate a mask of features which must be written to a sigframe.  The
 * unset features can be optimized away and not written.
 *
 * This optimization is user-visible.  Only use for states where
 * uninitialized sigframe contents are tolerable, like dynamic features.
 *
 * Users of buffers produced with this optimization must check XSTATE_BV
 * to determine which features have been optimized out.
 */
static inline u64 xfeatures_need_sigframe_write(void)
{
	u64 xfeaures_to_write;

	/* In-use features must be written: */
	xfeaures_to_write = xfeatures_in_use();

	/* Also write all non-optimizable sigframe features: */
	xfeaures_to_write |= XFEATURE_MASK_USER_SUPPORTED &
			     ~XFEATURE_MASK_SIGFRAME_INITOPT;

	return xfeaures_to_write;
}

/*
 * Save xstate to user space xsave area.
 *
 * We don't use modified optimization because xrstor/xrstors might track
 * a different application.
 *
 * We don't use compacted format xsave area for backward compatibility for
 * old applications which don't understand the compacted format of the
 * xsave area.
 *
 * The caller has to zero buf::header before calling this because XSAVE*
 * does not touch the reserved fields in the header.
 */
static inline int xsave_to_user_sigframe(struct xregs_state __user *buf, u32 pkru)
{
	/*
	 * Include the features which are not xsaved/rstored by the kernel
	 * internally, e.g. PKRU. That's user space ABI and also required
	 * to allow the signal handler to modify PKRU.
	 */
	struct fpstate *fpstate = current->thread.fpu.fpstate;
	u64 mask = fpstate->user_xfeatures;
	u32 lmask;
	u32 hmask;
	int err;

	/* Optimize away writing unnecessary xfeatures: */
	if (fpu_state_size_dynamic())
		mask &= xfeatures_need_sigframe_write();

	lmask = mask;
	hmask = mask >> 32;
	xfd_validate_state(fpstate, mask, false);

	stac();
	XSTATE_OP(XSAVE, buf, lmask, hmask, err);
	clac();

	if (!err)
		err = update_pkru_in_sigframe(buf, mask, pkru);

	return err;
}

/*
 * Restore xstate from user space xsave area.
 */
static inline int xrstor_from_user_sigframe(struct xregs_state __user *buf, u64 mask)
{
	struct xregs_state *xstate = ((__force struct xregs_state *)buf);
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err;

	xfd_validate_state(current->thread.fpu.fpstate, mask, true);

	stac();
	XSTATE_OP(XRSTOR, xstate, lmask, hmask, err);
	clac();

	return err;
}

/*
 * Restore xstate from kernel space xsave area, return an error code instead of
 * an exception.
 */
static inline int os_xrstor_safe(struct fpstate *fpstate, u64 mask)
{
	struct xregs_state *xstate = &fpstate->regs.xsave;
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err;

	/* Ensure that XFD is up to date */
	xfd_update_state(fpstate);

	if (cpu_feature_enabled(X86_FEATURE_XSAVES))
		XSTATE_OP(XRSTORS, xstate, lmask, hmask, err);
	else
		XSTATE_OP(XRSTOR, xstate, lmask, hmask, err);

	return err;
}


#endif
