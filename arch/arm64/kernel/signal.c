// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on arch/arm/kernel/signal.c
 *
 * Copyright (C) 1995-2009 Russell King
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/cache.h>
#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/irq-entry-common.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/freezer.h>
#include <linux/stddef.h>
#include <linux/uaccess.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include <linux/ratelimit.h>
#include <linux/rseq.h>
#include <linux/syscalls.h>
#include <linux/pkeys.h>

#include <asm/daifflags.h>
#include <asm/debug-monitors.h>
#include <asm/elf.h>
#include <asm/exception.h>
#include <asm/cacheflush.h>
#include <asm/gcs.h>
#include <asm/ucontext.h>
#include <asm/unistd.h>
#include <asm/fpsimd.h>
#include <asm/ptrace.h>
#include <asm/syscall.h>
#include <asm/signal32.h>
#include <asm/traps.h>
#include <asm/vdso.h>

#define GCS_SIGNAL_CAP(addr) (((unsigned long)addr) & GCS_CAP_ADDR_MASK)

/*
 * Do a signal return; undo the signal stack. These are aligned to 128-bit.
 */
struct rt_sigframe {
	struct siginfo info;
	struct ucontext uc;
};

struct rt_sigframe_user_layout {
	struct rt_sigframe __user *sigframe;
	struct frame_record __user *next_frame;

	unsigned long size;	/* size of allocated sigframe data */
	unsigned long limit;	/* largest allowed size */

	unsigned long fpsimd_offset;
	unsigned long esr_offset;
	unsigned long gcs_offset;
	unsigned long sve_offset;
	unsigned long tpidr2_offset;
	unsigned long za_offset;
	unsigned long zt_offset;
	unsigned long fpmr_offset;
	unsigned long poe_offset;
	unsigned long extra_offset;
	unsigned long end_offset;
};

/*
 * Holds any EL0-controlled state that influences unprivileged memory accesses.
 * This includes both accesses done in userspace and uaccess done in the kernel.
 *
 * This state needs to be carefully managed to ensure that it doesn't cause
 * uaccess to fail when setting up the signal frame, and the signal handler
 * itself also expects a well-defined state when entered.
 */
struct user_access_state {
	u64 por_el0;
};

#define TERMINATOR_SIZE round_up(sizeof(struct _aarch64_ctx), 16)
#define EXTRA_CONTEXT_SIZE round_up(sizeof(struct extra_context), 16)

/*
 * Save the user access state into ua_state and reset it to disable any
 * restrictions.
 */
static void save_reset_user_access_state(struct user_access_state *ua_state)
{
	if (system_supports_poe()) {
		u64 por_enable_all = 0;

		for (int pkey = 0; pkey < arch_max_pkey(); pkey++)
			por_enable_all |= POR_ELx_PERM_PREP(pkey, POE_RWX);

		ua_state->por_el0 = read_sysreg_s(SYS_POR_EL0);
		write_sysreg_s(por_enable_all, SYS_POR_EL0);
		/*
		 * No ISB required as we can tolerate spurious Overlay faults -
		 * the fault handler will check again based on the new value
		 * of POR_EL0.
		 */
	}
}

/*
 * Set the user access state for invoking the signal handler.
 *
 * No uaccess should be done after that function is called.
 */
static void set_handler_user_access_state(void)
{
	if (system_supports_poe())
		write_sysreg_s(POR_EL0_INIT, SYS_POR_EL0);
}

/*
 * Restore the user access state to the values saved in ua_state.
 *
 * No uaccess should be done after that function is called.
 */
static void restore_user_access_state(const struct user_access_state *ua_state)
{
	if (system_supports_poe())
		write_sysreg_s(ua_state->por_el0, SYS_POR_EL0);
}

static void init_user_layout(struct rt_sigframe_user_layout *user)
{
	const size_t reserved_size =
		sizeof(user->sigframe->uc.uc_mcontext.__reserved);

	memset(user, 0, sizeof(*user));
	user->size = offsetof(struct rt_sigframe, uc.uc_mcontext.__reserved);

	user->limit = user->size + reserved_size;

	user->limit -= TERMINATOR_SIZE;
	user->limit -= EXTRA_CONTEXT_SIZE;
	/* Reserve space for extension and terminator ^ */
}

static size_t sigframe_size(struct rt_sigframe_user_layout const *user)
{
	return round_up(max(user->size, sizeof(struct rt_sigframe)), 16);
}

/*
 * Sanity limit on the approximate maximum size of signal frame we'll
 * try to generate.  Stack alignment padding and the frame record are
 * not taken into account.  This limit is not a guarantee and is
 * NOT ABI.
 */
#define SIGFRAME_MAXSZ SZ_256K

static int __sigframe_alloc(struct rt_sigframe_user_layout *user,
			    unsigned long *offset, size_t size, bool extend)
{
	size_t padded_size = round_up(size, 16);

	if (padded_size > user->limit - user->size &&
	    !user->extra_offset &&
	    extend) {
		int ret;

		user->limit += EXTRA_CONTEXT_SIZE;
		ret = __sigframe_alloc(user, &user->extra_offset,
				       sizeof(struct extra_context), false);
		if (ret) {
			user->limit -= EXTRA_CONTEXT_SIZE;
			return ret;
		}

		/* Reserve space for the __reserved[] terminator */
		user->size += TERMINATOR_SIZE;

		/*
		 * Allow expansion up to SIGFRAME_MAXSZ, ensuring space for
		 * the terminator:
		 */
		user->limit = SIGFRAME_MAXSZ - TERMINATOR_SIZE;
	}

	/* Still not enough space?  Bad luck! */
	if (padded_size > user->limit - user->size)
		return -ENOMEM;

	*offset = user->size;
	user->size += padded_size;

	return 0;
}

/*
 * Allocate space for an optional record of <size> bytes in the user
 * signal frame.  The offset from the signal frame base address to the
 * allocated block is assigned to *offset.
 */
static int sigframe_alloc(struct rt_sigframe_user_layout *user,
			  unsigned long *offset, size_t size)
{
	return __sigframe_alloc(user, offset, size, true);
}

/* Allocate the null terminator record and prevent further allocations */
static int sigframe_alloc_end(struct rt_sigframe_user_layout *user)
{
	int ret;

	/* Un-reserve the space reserved for the terminator: */
	user->limit += TERMINATOR_SIZE;

	ret = sigframe_alloc(user, &user->end_offset,
			     sizeof(struct _aarch64_ctx));
	if (ret)
		return ret;

	/* Prevent further allocation: */
	user->limit = user->size;
	return 0;
}

static void __user *apply_user_offset(
	struct rt_sigframe_user_layout const *user, unsigned long offset)
{
	char __user *base = (char __user *)user->sigframe;

	return base + offset;
}

struct user_ctxs {
	struct fpsimd_context __user *fpsimd;
	u32 fpsimd_size;
	struct sve_context __user *sve;
	u32 sve_size;
	struct tpidr2_context __user *tpidr2;
	u32 tpidr2_size;
	struct za_context __user *za;
	u32 za_size;
	struct zt_context __user *zt;
	u32 zt_size;
	struct fpmr_context __user *fpmr;
	u32 fpmr_size;
	struct poe_context __user *poe;
	u32 poe_size;
	struct gcs_context __user *gcs;
	u32 gcs_size;
};

static int preserve_fpsimd_context(struct fpsimd_context __user *ctx)
{
	struct user_fpsimd_state const *fpsimd =
		&current->thread.uw.fpsimd_state;
	int err;

	fpsimd_sync_from_effective_state(current);

	/* copy the FP and status/control registers */
	err = __copy_to_user(ctx->vregs, fpsimd->vregs, sizeof(fpsimd->vregs));
	__put_user_error(fpsimd->fpsr, &ctx->fpsr, err);
	__put_user_error(fpsimd->fpcr, &ctx->fpcr, err);

	/* copy the magic/size information */
	__put_user_error(FPSIMD_MAGIC, &ctx->head.magic, err);
	__put_user_error(sizeof(struct fpsimd_context), &ctx->head.size, err);

	return err ? -EFAULT : 0;
}

static int read_fpsimd_context(struct user_fpsimd_state *fpsimd,
			       struct user_ctxs *user)
{
	int err;

	/* check the size information */
	if (user->fpsimd_size != sizeof(struct fpsimd_context))
		return -EINVAL;

	/* copy the FP and status/control registers */
	err = __copy_from_user(fpsimd->vregs, &(user->fpsimd->vregs),
			       sizeof(fpsimd->vregs));
	__get_user_error(fpsimd->fpsr, &(user->fpsimd->fpsr), err);
	__get_user_error(fpsimd->fpcr, &(user->fpsimd->fpcr), err);

	return err ? -EFAULT : 0;
}

static int restore_fpsimd_context(struct user_ctxs *user)
{
	struct user_fpsimd_state fpsimd;
	int err;

	err = read_fpsimd_context(&fpsimd, user);
	if (err)
		return err;

	clear_thread_flag(TIF_SVE);
	current->thread.svcr &= ~SVCR_SM_MASK;
	current->thread.fp_type = FP_STATE_FPSIMD;

	/* load the hardware registers from the fpsimd_state structure */
	fpsimd_update_current_state(&fpsimd);
	return 0;
}

static int preserve_fpmr_context(struct fpmr_context __user *ctx)
{
	int err = 0;

	__put_user_error(FPMR_MAGIC, &ctx->head.magic, err);
	__put_user_error(sizeof(*ctx), &ctx->head.size, err);
	__put_user_error(current->thread.uw.fpmr, &ctx->fpmr, err);

	return err;
}

static int restore_fpmr_context(struct user_ctxs *user)
{
	u64 fpmr;
	int err = 0;

	if (user->fpmr_size != sizeof(*user->fpmr))
		return -EINVAL;

	__get_user_error(fpmr, &user->fpmr->fpmr, err);
	if (!err)
		current->thread.uw.fpmr = fpmr;

	return err;
}

static int preserve_poe_context(struct poe_context __user *ctx,
				const struct user_access_state *ua_state)
{
	int err = 0;

	__put_user_error(POE_MAGIC, &ctx->head.magic, err);
	__put_user_error(sizeof(*ctx), &ctx->head.size, err);
	__put_user_error(ua_state->por_el0, &ctx->por_el0, err);

	return err;
}

static int restore_poe_context(struct user_ctxs *user,
			       struct user_access_state *ua_state)
{
	u64 por_el0;
	int err = 0;

	if (user->poe_size != sizeof(*user->poe))
		return -EINVAL;

	__get_user_error(por_el0, &(user->poe->por_el0), err);
	if (!err)
		ua_state->por_el0 = por_el0;

	return err;
}

#ifdef CONFIG_ARM64_SVE

static int preserve_sve_context(struct sve_context __user *ctx)
{
	int err = 0;
	u16 reserved[ARRAY_SIZE(ctx->__reserved)];
	u16 flags = 0;
	unsigned int vl = task_get_sve_vl(current);
	unsigned int vq = 0;

	if (thread_sm_enabled(&current->thread)) {
		vl = task_get_sme_vl(current);
		vq = sve_vq_from_vl(vl);
		flags |= SVE_SIG_FLAG_SM;
	} else if (current->thread.fp_type == FP_STATE_SVE) {
		vq = sve_vq_from_vl(vl);
	}

	memset(reserved, 0, sizeof(reserved));

	__put_user_error(SVE_MAGIC, &ctx->head.magic, err);
	__put_user_error(round_up(SVE_SIG_CONTEXT_SIZE(vq), 16),
			 &ctx->head.size, err);
	__put_user_error(vl, &ctx->vl, err);
	__put_user_error(flags, &ctx->flags, err);
	BUILD_BUG_ON(sizeof(ctx->__reserved) != sizeof(reserved));
	err |= __copy_to_user(&ctx->__reserved, reserved, sizeof(reserved));

	if (vq) {
		err |= __copy_to_user((char __user *)ctx + SVE_SIG_REGS_OFFSET,
				      current->thread.sve_state,
				      SVE_SIG_REGS_SIZE(vq));
	}

	return err ? -EFAULT : 0;
}

static int restore_sve_fpsimd_context(struct user_ctxs *user)
{
	int err = 0;
	unsigned int vl, vq;
	struct user_fpsimd_state fpsimd;
	u16 user_vl, flags;
	bool sm;

	if (user->sve_size < sizeof(*user->sve))
		return -EINVAL;

	__get_user_error(user_vl, &(user->sve->vl), err);
	__get_user_error(flags, &(user->sve->flags), err);
	if (err)
		return err;

	sm = flags & SVE_SIG_FLAG_SM;
	if (sm) {
		if (!system_supports_sme())
			return -EINVAL;

		vl = task_get_sme_vl(current);
	} else {
		/*
		 * A SME only system use SVE for streaming mode so can
		 * have a SVE formatted context with a zero VL and no
		 * payload data.
		 */
		if (!system_supports_sve() && !system_supports_sme())
			return -EINVAL;

		vl = task_get_sve_vl(current);
	}

	if (user_vl != vl)
		return -EINVAL;

	/*
	 * Non-streaming SVE state may be preserved without an SVE payload, in
	 * which case the SVE context only has a header with VL==0, and all
	 * state can be restored from the FPSIMD context.
	 *
	 * Streaming SVE state is always preserved with an SVE payload. For
	 * consistency and robustness, reject restoring streaming SVE state
	 * without an SVE payload.
	 */
	if (!sm && user->sve_size == sizeof(*user->sve))
		return restore_fpsimd_context(user);

	vq = sve_vq_from_vl(vl);

	if (user->sve_size < SVE_SIG_CONTEXT_SIZE(vq))
		return -EINVAL;

	sve_alloc(current, true);
	if (!current->thread.sve_state) {
		clear_thread_flag(TIF_SVE);
		return -ENOMEM;
	}

	err = __copy_from_user(current->thread.sve_state,
			       (char __user const *)user->sve +
					SVE_SIG_REGS_OFFSET,
			       SVE_SIG_REGS_SIZE(vq));
	if (err)
		return -EFAULT;

	if (flags & SVE_SIG_FLAG_SM)
		current->thread.svcr |= SVCR_SM_MASK;
	else
		set_thread_flag(TIF_SVE);
	current->thread.fp_type = FP_STATE_SVE;

	err = read_fpsimd_context(&fpsimd, user);
	if (err)
		return err;

	/* Merge the FPSIMD registers into the SVE state */
	fpsimd_update_current_state(&fpsimd);

	return 0;
}

#else /* ! CONFIG_ARM64_SVE */

static int restore_sve_fpsimd_context(struct user_ctxs *user)
{
	WARN_ON_ONCE(1);
	return -EINVAL;
}

/* Turn any non-optimised out attempts to use this into a link error: */
extern int preserve_sve_context(void __user *ctx);

#endif /* ! CONFIG_ARM64_SVE */

#ifdef CONFIG_ARM64_SME

static int preserve_tpidr2_context(struct tpidr2_context __user *ctx)
{
	u64 tpidr2_el0 = read_sysreg_s(SYS_TPIDR2_EL0);
	int err = 0;

	__put_user_error(TPIDR2_MAGIC, &ctx->head.magic, err);
	__put_user_error(sizeof(*ctx), &ctx->head.size, err);
	__put_user_error(tpidr2_el0, &ctx->tpidr2, err);

	return err;
}

static int restore_tpidr2_context(struct user_ctxs *user)
{
	u64 tpidr2_el0;
	int err = 0;

	if (user->tpidr2_size != sizeof(*user->tpidr2))
		return -EINVAL;

	__get_user_error(tpidr2_el0, &user->tpidr2->tpidr2, err);
	if (!err)
		write_sysreg_s(tpidr2_el0, SYS_TPIDR2_EL0);

	return err;
}

static int preserve_za_context(struct za_context __user *ctx)
{
	int err = 0;
	u16 reserved[ARRAY_SIZE(ctx->__reserved)];
	unsigned int vl = task_get_sme_vl(current);
	unsigned int vq;

	if (thread_za_enabled(&current->thread))
		vq = sve_vq_from_vl(vl);
	else
		vq = 0;

	memset(reserved, 0, sizeof(reserved));

	__put_user_error(ZA_MAGIC, &ctx->head.magic, err);
	__put_user_error(round_up(ZA_SIG_CONTEXT_SIZE(vq), 16),
			 &ctx->head.size, err);
	__put_user_error(vl, &ctx->vl, err);
	BUILD_BUG_ON(sizeof(ctx->__reserved) != sizeof(reserved));
	err |= __copy_to_user(&ctx->__reserved, reserved, sizeof(reserved));

	if (vq) {
		err |= __copy_to_user((char __user *)ctx + ZA_SIG_REGS_OFFSET,
				      current->thread.sme_state,
				      ZA_SIG_REGS_SIZE(vq));
	}

	return err ? -EFAULT : 0;
}

static int restore_za_context(struct user_ctxs *user)
{
	int err = 0;
	unsigned int vq;
	u16 user_vl;

	if (user->za_size < sizeof(*user->za))
		return -EINVAL;

	__get_user_error(user_vl, &(user->za->vl), err);
	if (err)
		return err;

	if (user_vl != task_get_sme_vl(current))
		return -EINVAL;

	if (user->za_size == sizeof(*user->za)) {
		current->thread.svcr &= ~SVCR_ZA_MASK;
		return 0;
	}

	vq = sve_vq_from_vl(user_vl);

	if (user->za_size < ZA_SIG_CONTEXT_SIZE(vq))
		return -EINVAL;

	sme_alloc(current, true);
	if (!current->thread.sme_state) {
		current->thread.svcr &= ~SVCR_ZA_MASK;
		clear_thread_flag(TIF_SME);
		return -ENOMEM;
	}

	err = __copy_from_user(current->thread.sme_state,
			       (char __user const *)user->za +
					ZA_SIG_REGS_OFFSET,
			       ZA_SIG_REGS_SIZE(vq));
	if (err)
		return -EFAULT;

	set_thread_flag(TIF_SME);
	current->thread.svcr |= SVCR_ZA_MASK;

	return 0;
}

static int preserve_zt_context(struct zt_context __user *ctx)
{
	int err = 0;
	u16 reserved[ARRAY_SIZE(ctx->__reserved)];

	if (WARN_ON(!thread_za_enabled(&current->thread)))
		return -EINVAL;

	memset(reserved, 0, sizeof(reserved));

	__put_user_error(ZT_MAGIC, &ctx->head.magic, err);
	__put_user_error(round_up(ZT_SIG_CONTEXT_SIZE(1), 16),
			 &ctx->head.size, err);
	__put_user_error(1, &ctx->nregs, err);
	BUILD_BUG_ON(sizeof(ctx->__reserved) != sizeof(reserved));
	err |= __copy_to_user(&ctx->__reserved, reserved, sizeof(reserved));

	err |= __copy_to_user((char __user *)ctx + ZT_SIG_REGS_OFFSET,
			      thread_zt_state(&current->thread),
			      ZT_SIG_REGS_SIZE(1));

	return err ? -EFAULT : 0;
}

static int restore_zt_context(struct user_ctxs *user)
{
	int err;
	u16 nregs;

	/* ZA must be restored first for this check to be valid */
	if (!thread_za_enabled(&current->thread))
		return -EINVAL;

	if (user->zt_size != ZT_SIG_CONTEXT_SIZE(1))
		return -EINVAL;

	if (__copy_from_user(&nregs, &(user->zt->nregs), sizeof(nregs)))
		return -EFAULT;

	if (nregs != 1)
		return -EINVAL;

	err = __copy_from_user(thread_zt_state(&current->thread),
			       (char __user const *)user->zt +
					ZT_SIG_REGS_OFFSET,
			       ZT_SIG_REGS_SIZE(1));
	if (err)
		return -EFAULT;

	return 0;
}

#else /* ! CONFIG_ARM64_SME */

/* Turn any non-optimised out attempts to use these into a link error: */
extern int preserve_tpidr2_context(void __user *ctx);
extern int restore_tpidr2_context(struct user_ctxs *user);
extern int preserve_za_context(void __user *ctx);
extern int restore_za_context(struct user_ctxs *user);
extern int preserve_zt_context(void __user *ctx);
extern int restore_zt_context(struct user_ctxs *user);

#endif /* ! CONFIG_ARM64_SME */

#ifdef CONFIG_ARM64_GCS

static int preserve_gcs_context(struct gcs_context __user *ctx)
{
	int err = 0;
	u64 gcspr = read_sysreg_s(SYS_GCSPR_EL0);

	/*
	 * If GCS is enabled we will add a cap token to the frame,
	 * include it in the GCSPR_EL0 we report to support stack
	 * switching via sigreturn if GCS is enabled.  We do not allow
	 * enabling via sigreturn so the token is only relevant for
	 * threads with GCS enabled.
	 */
	if (task_gcs_el0_enabled(current))
		gcspr -= 8;

	__put_user_error(GCS_MAGIC, &ctx->head.magic, err);
	__put_user_error(sizeof(*ctx), &ctx->head.size, err);
	__put_user_error(gcspr, &ctx->gcspr, err);
	__put_user_error(0, &ctx->reserved, err);
	__put_user_error(current->thread.gcs_el0_mode,
			 &ctx->features_enabled, err);

	return err;
}

static int restore_gcs_context(struct user_ctxs *user)
{
	u64 gcspr, enabled;
	int err = 0;

	if (user->gcs_size != sizeof(*user->gcs))
		return -EINVAL;

	__get_user_error(gcspr, &user->gcs->gcspr, err);
	__get_user_error(enabled, &user->gcs->features_enabled, err);
	if (err)
		return err;

	/* Don't allow unknown modes */
	if (enabled & ~PR_SHADOW_STACK_SUPPORTED_STATUS_MASK)
		return -EINVAL;

	err = gcs_check_locked(current, enabled);
	if (err != 0)
		return err;

	/* Don't allow enabling */
	if (!task_gcs_el0_enabled(current) &&
	    (enabled & PR_SHADOW_STACK_ENABLE))
		return -EINVAL;

	/* If we are disabling disable everything */
	if (!(enabled & PR_SHADOW_STACK_ENABLE))
		enabled = 0;

	current->thread.gcs_el0_mode = enabled;

	/*
	 * We let userspace set GCSPR_EL0 to anything here, we will
	 * validate later in gcs_restore_signal().
	 */
	write_sysreg_s(gcspr, SYS_GCSPR_EL0);

	return 0;
}

#else /* ! CONFIG_ARM64_GCS */

/* Turn any non-optimised out attempts to use these into a link error: */
extern int preserve_gcs_context(void __user *ctx);
extern int restore_gcs_context(struct user_ctxs *user);

#endif /* ! CONFIG_ARM64_GCS */

static int parse_user_sigframe(struct user_ctxs *user,
			       struct rt_sigframe __user *sf)
{
	struct sigcontext __user *const sc = &sf->uc.uc_mcontext;
	struct _aarch64_ctx __user *head;
	char __user *base = (char __user *)&sc->__reserved;
	size_t offset = 0;
	size_t limit = sizeof(sc->__reserved);
	bool have_extra_context = false;
	char const __user *const sfp = (char const __user *)sf;

	user->fpsimd = NULL;
	user->sve = NULL;
	user->tpidr2 = NULL;
	user->za = NULL;
	user->zt = NULL;
	user->fpmr = NULL;
	user->poe = NULL;
	user->gcs = NULL;

	if (!IS_ALIGNED((unsigned long)base, 16))
		goto invalid;

	while (1) {
		int err = 0;
		u32 magic, size;
		char const __user *userp;
		struct extra_context const __user *extra;
		u64 extra_datap;
		u32 extra_size;
		struct _aarch64_ctx const __user *end;
		u32 end_magic, end_size;

		if (limit - offset < sizeof(*head))
			goto invalid;

		if (!IS_ALIGNED(offset, 16))
			goto invalid;

		head = (struct _aarch64_ctx __user *)(base + offset);
		__get_user_error(magic, &head->magic, err);
		__get_user_error(size, &head->size, err);
		if (err)
			return err;

		if (limit - offset < size)
			goto invalid;

		switch (magic) {
		case 0:
			if (size)
				goto invalid;

			goto done;

		case FPSIMD_MAGIC:
			if (!system_supports_fpsimd())
				goto invalid;
			if (user->fpsimd)
				goto invalid;

			user->fpsimd = (struct fpsimd_context __user *)head;
			user->fpsimd_size = size;
			break;

		case ESR_MAGIC:
			/* ignore */
			break;

		case POE_MAGIC:
			if (!system_supports_poe())
				goto invalid;

			if (user->poe)
				goto invalid;

			user->poe = (struct poe_context __user *)head;
			user->poe_size = size;
			break;

		case SVE_MAGIC:
			if (!system_supports_sve() && !system_supports_sme())
				goto invalid;

			if (user->sve)
				goto invalid;

			user->sve = (struct sve_context __user *)head;
			user->sve_size = size;
			break;

		case TPIDR2_MAGIC:
			if (!system_supports_tpidr2())
				goto invalid;

			if (user->tpidr2)
				goto invalid;

			user->tpidr2 = (struct tpidr2_context __user *)head;
			user->tpidr2_size = size;
			break;

		case ZA_MAGIC:
			if (!system_supports_sme())
				goto invalid;

			if (user->za)
				goto invalid;

			user->za = (struct za_context __user *)head;
			user->za_size = size;
			break;

		case ZT_MAGIC:
			if (!system_supports_sme2())
				goto invalid;

			if (user->zt)
				goto invalid;

			user->zt = (struct zt_context __user *)head;
			user->zt_size = size;
			break;

		case FPMR_MAGIC:
			if (!system_supports_fpmr())
				goto invalid;

			if (user->fpmr)
				goto invalid;

			user->fpmr = (struct fpmr_context __user *)head;
			user->fpmr_size = size;
			break;

		case GCS_MAGIC:
			if (!system_supports_gcs())
				goto invalid;

			if (user->gcs)
				goto invalid;

			user->gcs = (struct gcs_context __user *)head;
			user->gcs_size = size;
			break;

		case EXTRA_MAGIC:
			if (have_extra_context)
				goto invalid;

			if (size < sizeof(*extra))
				goto invalid;

			userp = (char const __user *)head;

			extra = (struct extra_context const __user *)userp;
			userp += size;

			__get_user_error(extra_datap, &extra->datap, err);
			__get_user_error(extra_size, &extra->size, err);
			if (err)
				return err;

			/* Check for the dummy terminator in __reserved[]: */

			if (limit - offset - size < TERMINATOR_SIZE)
				goto invalid;

			end = (struct _aarch64_ctx const __user *)userp;
			userp += TERMINATOR_SIZE;

			__get_user_error(end_magic, &end->magic, err);
			__get_user_error(end_size, &end->size, err);
			if (err)
				return err;

			if (end_magic || end_size)
				goto invalid;

			/* Prevent looping/repeated parsing of extra_context */
			have_extra_context = true;

			base = (__force void __user *)extra_datap;
			if (!IS_ALIGNED((unsigned long)base, 16))
				goto invalid;

			if (!IS_ALIGNED(extra_size, 16))
				goto invalid;

			if (base != userp)
				goto invalid;

			/* Reject "unreasonably large" frames: */
			if (extra_size > sfp + SIGFRAME_MAXSZ - userp)
				goto invalid;

			/*
			 * Ignore trailing terminator in __reserved[]
			 * and start parsing extra data:
			 */
			offset = 0;
			limit = extra_size;

			if (!access_ok(base, limit))
				goto invalid;

			continue;

		default:
			goto invalid;
		}

		if (size < sizeof(*head))
			goto invalid;

		if (limit - offset < size)
			goto invalid;

		offset += size;
	}

done:
	return 0;

invalid:
	return -EINVAL;
}

static int restore_sigframe(struct pt_regs *regs,
			    struct rt_sigframe __user *sf,
			    struct user_access_state *ua_state)
{
	sigset_t set;
	int i, err;
	struct user_ctxs user;

	err = __copy_from_user(&set, &sf->uc.uc_sigmask, sizeof(set));
	if (err == 0)
		set_current_blocked(&set);

	for (i = 0; i < 31; i++)
		__get_user_error(regs->regs[i], &sf->uc.uc_mcontext.regs[i],
				 err);
	__get_user_error(regs->sp, &sf->uc.uc_mcontext.sp, err);
	__get_user_error(regs->pc, &sf->uc.uc_mcontext.pc, err);
	__get_user_error(regs->pstate, &sf->uc.uc_mcontext.pstate, err);

	/*
	 * Avoid sys_rt_sigreturn() restarting.
	 */
	forget_syscall(regs);

	fpsimd_save_and_flush_current_state();

	err |= !valid_user_regs(&regs->user_regs, current);
	if (err == 0)
		err = parse_user_sigframe(&user, sf);

	if (err == 0 && system_supports_fpsimd()) {
		if (!user.fpsimd)
			return -EINVAL;

		if (user.sve)
			err = restore_sve_fpsimd_context(&user);
		else
			err = restore_fpsimd_context(&user);
	}

	if (err == 0 && system_supports_gcs() && user.gcs)
		err = restore_gcs_context(&user);

	if (err == 0 && system_supports_tpidr2() && user.tpidr2)
		err = restore_tpidr2_context(&user);

	if (err == 0 && system_supports_fpmr() && user.fpmr)
		err = restore_fpmr_context(&user);

	if (err == 0 && system_supports_sme() && user.za)
		err = restore_za_context(&user);

	if (err == 0 && system_supports_sme2() && user.zt)
		err = restore_zt_context(&user);

	if (err == 0 && system_supports_poe() && user.poe)
		err = restore_poe_context(&user, ua_state);

	return err;
}

#ifdef CONFIG_ARM64_GCS
static int gcs_restore_signal(void)
{
	u64 gcspr_el0, cap;
	int ret;

	if (!system_supports_gcs())
		return 0;

	if (!(current->thread.gcs_el0_mode & PR_SHADOW_STACK_ENABLE))
		return 0;

	gcspr_el0 = read_sysreg_s(SYS_GCSPR_EL0);

	/*
	 * Ensure that any changes to the GCS done via GCS operations
	 * are visible to the normal reads we do to validate the
	 * token.
	 */
	gcsb_dsync();

	/*
	 * GCSPR_EL0 should be pointing at a capped GCS, read the cap.
	 * We don't enforce that this is in a GCS page, if it is not
	 * then faults will be generated on GCS operations - the main
	 * concern is to protect GCS pages.
	 */
	ret = copy_from_user(&cap, (unsigned long __user *)gcspr_el0,
			     sizeof(cap));
	if (ret)
		return -EFAULT;

	/*
	 * Check that the cap is the actual GCS before replacing it.
	 */
	if (cap != GCS_SIGNAL_CAP(gcspr_el0))
		return -EINVAL;

	/* Invalidate the token to prevent reuse */
	put_user_gcs(0, (unsigned long __user *)gcspr_el0, &ret);
	if (ret != 0)
		return -EFAULT;

	write_sysreg_s(gcspr_el0 + 8, SYS_GCSPR_EL0);

	return 0;
}

#else
static int gcs_restore_signal(void) { return 0; }
#endif

SYSCALL_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe __user *frame;
	struct user_access_state ua_state;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	/*
	 * Since we stacked the signal on a 128-bit boundary, then 'sp' should
	 * be word aligned here.
	 */
	if (regs->sp & 15)
		goto badframe;

	frame = (struct rt_sigframe __user *)regs->sp;

	if (!access_ok(frame, sizeof (*frame)))
		goto badframe;

	if (restore_sigframe(regs, frame, &ua_state))
		goto badframe;

	if (gcs_restore_signal())
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	restore_user_access_state(&ua_state);

	return regs->regs[0];

badframe:
	arm64_notify_segfault(regs->sp);
	return 0;
}

/*
 * Determine the layout of optional records in the signal frame
 *
 * add_all: if true, lays out the biggest possible signal frame for
 *	this task; otherwise, generates a layout for the current state
 *	of the task.
 */
static int setup_sigframe_layout(struct rt_sigframe_user_layout *user,
				 bool add_all)
{
	int err;

	if (system_supports_fpsimd()) {
		err = sigframe_alloc(user, &user->fpsimd_offset,
				     sizeof(struct fpsimd_context));
		if (err)
			return err;
	}

	/* fault information, if valid */
	if (add_all || current->thread.fault_code) {
		err = sigframe_alloc(user, &user->esr_offset,
				     sizeof(struct esr_context));
		if (err)
			return err;
	}

#ifdef CONFIG_ARM64_GCS
	if (system_supports_gcs() && (add_all || current->thread.gcspr_el0)) {
		err = sigframe_alloc(user, &user->gcs_offset,
				     sizeof(struct gcs_context));
		if (err)
			return err;
	}
#endif

	if (system_supports_sve() || system_supports_sme()) {
		unsigned int vq = 0;

		if (add_all || current->thread.fp_type == FP_STATE_SVE ||
		    thread_sm_enabled(&current->thread)) {
			int vl = max(sve_max_vl(), sme_max_vl());

			if (!add_all)
				vl = thread_get_cur_vl(&current->thread);

			vq = sve_vq_from_vl(vl);
		}

		err = sigframe_alloc(user, &user->sve_offset,
				     SVE_SIG_CONTEXT_SIZE(vq));
		if (err)
			return err;
	}

	if (system_supports_tpidr2()) {
		err = sigframe_alloc(user, &user->tpidr2_offset,
				     sizeof(struct tpidr2_context));
		if (err)
			return err;
	}

	if (system_supports_sme()) {
		unsigned int vl;
		unsigned int vq = 0;

		if (add_all)
			vl = sme_max_vl();
		else
			vl = task_get_sme_vl(current);

		if (thread_za_enabled(&current->thread))
			vq = sve_vq_from_vl(vl);

		err = sigframe_alloc(user, &user->za_offset,
				     ZA_SIG_CONTEXT_SIZE(vq));
		if (err)
			return err;
	}

	if (system_supports_sme2()) {
		if (add_all || thread_za_enabled(&current->thread)) {
			err = sigframe_alloc(user, &user->zt_offset,
					     ZT_SIG_CONTEXT_SIZE(1));
			if (err)
				return err;
		}
	}

	if (system_supports_fpmr()) {
		err = sigframe_alloc(user, &user->fpmr_offset,
				     sizeof(struct fpmr_context));
		if (err)
			return err;
	}

	if (system_supports_poe()) {
		err = sigframe_alloc(user, &user->poe_offset,
				     sizeof(struct poe_context));
		if (err)
			return err;
	}

	return sigframe_alloc_end(user);
}

static int setup_sigframe(struct rt_sigframe_user_layout *user,
			  struct pt_regs *regs, sigset_t *set,
			  const struct user_access_state *ua_state)
{
	int i, err = 0;
	struct rt_sigframe __user *sf = user->sigframe;

	/* set up the stack frame for unwinding */
	__put_user_error(regs->regs[29], &user->next_frame->fp, err);
	__put_user_error(regs->regs[30], &user->next_frame->lr, err);

	for (i = 0; i < 31; i++)
		__put_user_error(regs->regs[i], &sf->uc.uc_mcontext.regs[i],
				 err);
	__put_user_error(regs->sp, &sf->uc.uc_mcontext.sp, err);
	__put_user_error(regs->pc, &sf->uc.uc_mcontext.pc, err);
	__put_user_error(regs->pstate, &sf->uc.uc_mcontext.pstate, err);

	__put_user_error(current->thread.fault_address, &sf->uc.uc_mcontext.fault_address, err);

	err |= __copy_to_user(&sf->uc.uc_sigmask, set, sizeof(*set));

	if (err == 0 && system_supports_fpsimd()) {
		struct fpsimd_context __user *fpsimd_ctx =
			apply_user_offset(user, user->fpsimd_offset);
		err |= preserve_fpsimd_context(fpsimd_ctx);
	}

	/* fault information, if valid */
	if (err == 0 && user->esr_offset) {
		struct esr_context __user *esr_ctx =
			apply_user_offset(user, user->esr_offset);

		__put_user_error(ESR_MAGIC, &esr_ctx->head.magic, err);
		__put_user_error(sizeof(*esr_ctx), &esr_ctx->head.size, err);
		__put_user_error(current->thread.fault_code, &esr_ctx->esr, err);
	}

	if (system_supports_gcs() && err == 0 && user->gcs_offset) {
		struct gcs_context __user *gcs_ctx =
			apply_user_offset(user, user->gcs_offset);
		err |= preserve_gcs_context(gcs_ctx);
	}

	/* Scalable Vector Extension state (including streaming), if present */
	if ((system_supports_sve() || system_supports_sme()) &&
	    err == 0 && user->sve_offset) {
		struct sve_context __user *sve_ctx =
			apply_user_offset(user, user->sve_offset);
		err |= preserve_sve_context(sve_ctx);
	}

	/* TPIDR2 if supported */
	if (system_supports_tpidr2() && err == 0) {
		struct tpidr2_context __user *tpidr2_ctx =
			apply_user_offset(user, user->tpidr2_offset);
		err |= preserve_tpidr2_context(tpidr2_ctx);
	}

	/* FPMR if supported */
	if (system_supports_fpmr() && err == 0) {
		struct fpmr_context __user *fpmr_ctx =
			apply_user_offset(user, user->fpmr_offset);
		err |= preserve_fpmr_context(fpmr_ctx);
	}

	if (system_supports_poe() && err == 0) {
		struct poe_context __user *poe_ctx =
			apply_user_offset(user, user->poe_offset);

		err |= preserve_poe_context(poe_ctx, ua_state);
	}

	/* ZA state if present */
	if (system_supports_sme() && err == 0 && user->za_offset) {
		struct za_context __user *za_ctx =
			apply_user_offset(user, user->za_offset);
		err |= preserve_za_context(za_ctx);
	}

	/* ZT state if present */
	if (system_supports_sme2() && err == 0 && user->zt_offset) {
		struct zt_context __user *zt_ctx =
			apply_user_offset(user, user->zt_offset);
		err |= preserve_zt_context(zt_ctx);
	}

	if (err == 0 && user->extra_offset) {
		char __user *sfp = (char __user *)user->sigframe;
		char __user *userp =
			apply_user_offset(user, user->extra_offset);

		struct extra_context __user *extra;
		struct _aarch64_ctx __user *end;
		u64 extra_datap;
		u32 extra_size;

		extra = (struct extra_context __user *)userp;
		userp += EXTRA_CONTEXT_SIZE;

		end = (struct _aarch64_ctx __user *)userp;
		userp += TERMINATOR_SIZE;

		/*
		 * extra_datap is just written to the signal frame.
		 * The value gets cast back to a void __user *
		 * during sigreturn.
		 */
		extra_datap = (__force u64)userp;
		extra_size = sfp + round_up(user->size, 16) - userp;

		__put_user_error(EXTRA_MAGIC, &extra->head.magic, err);
		__put_user_error(EXTRA_CONTEXT_SIZE, &extra->head.size, err);
		__put_user_error(extra_datap, &extra->datap, err);
		__put_user_error(extra_size, &extra->size, err);

		/* Add the terminator */
		__put_user_error(0, &end->magic, err);
		__put_user_error(0, &end->size, err);
	}

	/* set the "end" magic */
	if (err == 0) {
		struct _aarch64_ctx __user *end =
			apply_user_offset(user, user->end_offset);

		__put_user_error(0, &end->magic, err);
		__put_user_error(0, &end->size, err);
	}

	return err;
}

static int get_sigframe(struct rt_sigframe_user_layout *user,
			 struct ksignal *ksig, struct pt_regs *regs)
{
	unsigned long sp, sp_top;
	int err;

	init_user_layout(user);
	err = setup_sigframe_layout(user, false);
	if (err)
		return err;

	sp = sp_top = sigsp(regs->sp, ksig);

	sp = round_down(sp - sizeof(struct frame_record), 16);
	user->next_frame = (struct frame_record __user *)sp;

	sp = round_down(sp, 16) - sigframe_size(user);
	user->sigframe = (struct rt_sigframe __user *)sp;

	/*
	 * Check that we can actually write to the signal frame.
	 */
	if (!access_ok(user->sigframe, sp_top - sp))
		return -EFAULT;

	return 0;
}

#ifdef CONFIG_ARM64_GCS

static int gcs_signal_entry(__sigrestore_t sigtramp, struct ksignal *ksig)
{
	u64 gcspr_el0;
	int ret = 0;

	if (!system_supports_gcs())
		return 0;

	if (!task_gcs_el0_enabled(current))
		return 0;

	/*
	 * We are entering a signal handler, current register state is
	 * active.
	 */
	gcspr_el0 = read_sysreg_s(SYS_GCSPR_EL0);

	/*
	 * Push a cap and the GCS entry for the trampoline onto the GCS.
	 */
	put_user_gcs((unsigned long)sigtramp,
		     (unsigned long __user *)(gcspr_el0 - 16), &ret);
	put_user_gcs(GCS_SIGNAL_CAP(gcspr_el0 - 8),
		     (unsigned long __user *)(gcspr_el0 - 8), &ret);
	if (ret != 0)
		return ret;

	gcspr_el0 -= 16;
	write_sysreg_s(gcspr_el0, SYS_GCSPR_EL0);

	return 0;
}
#else

static int gcs_signal_entry(__sigrestore_t sigtramp, struct ksignal *ksig)
{
	return 0;
}

#endif

static int setup_return(struct pt_regs *regs, struct ksignal *ksig,
			 struct rt_sigframe_user_layout *user, int usig)
{
	__sigrestore_t sigtramp;
	int err;

	if (ksig->ka.sa.sa_flags & SA_RESTORER)
		sigtramp = ksig->ka.sa.sa_restorer;
	else
		sigtramp = VDSO_SYMBOL(current->mm->context.vdso, sigtramp);

	err = gcs_signal_entry(sigtramp, ksig);
	if (err)
		return err;

	/*
	 * We must not fail from this point onwards. We are going to update
	 * registers, including SP, in order to invoke the signal handler. If
	 * we failed and attempted to deliver a nested SIGSEGV to a handler
	 * after that point, the subsequent sigreturn would end up restoring
	 * the (partial) state for the original signal handler.
	 */

	regs->regs[0] = usig;
	if (ksig->ka.sa.sa_flags & SA_SIGINFO) {
		regs->regs[1] = (unsigned long)&user->sigframe->info;
		regs->regs[2] = (unsigned long)&user->sigframe->uc;
	}
	regs->sp = (unsigned long)user->sigframe;
	regs->regs[29] = (unsigned long)&user->next_frame->fp;
	regs->regs[30] = (unsigned long)sigtramp;
	regs->pc = (unsigned long)ksig->ka.sa.sa_handler;

	/*
	 * Signal delivery is a (wacky) indirect function call in
	 * userspace, so simulate the same setting of BTYPE as a BLR
	 * <register containing the signal handler entry point>.
	 * Signal delivery to a location in a PROT_BTI guarded page
	 * that is not a function entry point will now trigger a
	 * SIGILL in userspace.
	 *
	 * If the signal handler entry point is not in a PROT_BTI
	 * guarded page, this is harmless.
	 */
	if (system_supports_bti()) {
		regs->pstate &= ~PSR_BTYPE_MASK;
		regs->pstate |= PSR_BTYPE_C;
	}

	/* TCO (Tag Check Override) always cleared for signal handlers */
	regs->pstate &= ~PSR_TCO_BIT;

	/* Signal handlers are invoked with ZA and streaming mode disabled */
	if (system_supports_sme()) {
		task_smstop_sm(current);
		current->thread.svcr &= ~SVCR_ZA_MASK;
		write_sysreg_s(0, SYS_TPIDR2_EL0);
	}

	return 0;
}

static int setup_rt_frame(int usig, struct ksignal *ksig, sigset_t *set,
			  struct pt_regs *regs)
{
	struct rt_sigframe_user_layout user;
	struct rt_sigframe __user *frame;
	struct user_access_state ua_state;
	int err = 0;

	fpsimd_save_and_flush_current_state();

	if (get_sigframe(&user, ksig, regs))
		return 1;

	save_reset_user_access_state(&ua_state);
	frame = user.sigframe;

	__put_user_error(0, &frame->uc.uc_flags, err);
	__put_user_error(NULL, &frame->uc.uc_link, err);

	err |= __save_altstack(&frame->uc.uc_stack, regs->sp);
	err |= setup_sigframe(&user, regs, set, &ua_state);
	if (ksig->ka.sa.sa_flags & SA_SIGINFO)
		err |= copy_siginfo_to_user(&frame->info, &ksig->info);

	if (err == 0)
		err = setup_return(regs, ksig, &user, usig);

	/*
	 * We must not fail if setup_return() succeeded - see comment at the
	 * beginning of setup_return().
	 */

	if (err == 0)
		set_handler_user_access_state();
	else
		restore_user_access_state(&ua_state);

	return err;
}

static void setup_restart_syscall(struct pt_regs *regs)
{
	if (is_compat_task())
		compat_setup_restart_syscall(regs);
	else
		regs->regs[8] = __NR_restart_syscall;
}

/*
 * OK, we're invoking a handler
 */
static void handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
	int usig = ksig->sig;
	int ret;

	rseq_signal_deliver(ksig, regs);

	/*
	 * Set up the stack frame
	 */
	if (is_compat_task()) {
		if (ksig->ka.sa.sa_flags & SA_SIGINFO)
			ret = compat_setup_rt_frame(usig, ksig, oldset, regs);
		else
			ret = compat_setup_frame(usig, ksig, oldset, regs);
	} else {
		ret = setup_rt_frame(usig, ksig, oldset, regs);
	}

	/*
	 * Check that the resulting registers are actually sane.
	 */
	ret |= !valid_user_regs(&regs->user_regs, current);

	/* Step into the signal handler if we are stepping */
	signal_setup_done(ret, ksig, test_thread_flag(TIF_SINGLESTEP));
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals that
 * the kernel can handle, and then we build all the user-level signal handling
 * stack-frames in one go after that.
 */
void arch_do_signal_or_restart(struct pt_regs *regs)
{
	unsigned long continue_addr = 0, restart_addr = 0;
	int retval = 0;
	struct ksignal ksig;
	bool syscall = in_syscall(regs);

	/*
	 * If we were from a system call, check for system call restarting...
	 */
	if (syscall) {
		continue_addr = regs->pc;
		restart_addr = continue_addr - (compat_thumb_mode(regs) ? 2 : 4);
		retval = regs->regs[0];

		/*
		 * Avoid additional syscall restarting via ret_to_user.
		 */
		forget_syscall(regs);

		/*
		 * Prepare for system call restart. We do this here so that a
		 * debugger will see the already changed PC.
		 */
		switch (retval) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
		case -ERESTART_RESTARTBLOCK:
			regs->regs[0] = regs->orig_x0;
			regs->pc = restart_addr;
			break;
		}
	}

	/*
	 * Get the signal to deliver. When running under ptrace, at this point
	 * the debugger may change all of our registers.
	 */
	if (get_signal(&ksig)) {
		/*
		 * Depending on the signal settings, we may need to revert the
		 * decision to restart the system call, but skip this if a
		 * debugger has chosen to restart at a different PC.
		 */
		if (regs->pc == restart_addr &&
		    (retval == -ERESTARTNOHAND ||
		     retval == -ERESTART_RESTARTBLOCK ||
		     (retval == -ERESTARTSYS &&
		      !(ksig.ka.sa.sa_flags & SA_RESTART)))) {
			syscall_set_return_value(current, regs, -EINTR, 0);
			regs->pc = continue_addr;
		}

		handle_signal(&ksig, regs);
		return;
	}

	/*
	 * Handle restarting a different system call. As above, if a debugger
	 * has chosen to restart at a different PC, ignore the restart.
	 */
	if (syscall && regs->pc == restart_addr) {
		if (retval == -ERESTART_RESTARTBLOCK)
			setup_restart_syscall(regs);
		user_rewind_single_step(current);
	}

	restore_saved_sigmask();
}

unsigned long __ro_after_init signal_minsigstksz;

/*
 * Determine the stack space required for guaranteed signal devliery.
 * This function is used to populate AT_MINSIGSTKSZ at process startup.
 * cpufeatures setup is assumed to be complete.
 */
void __init minsigstksz_setup(void)
{
	struct rt_sigframe_user_layout user;

	init_user_layout(&user);

	/*
	 * If this fails, SIGFRAME_MAXSZ needs to be enlarged.  It won't
	 * be big enough, but it's our best guess:
	 */
	if (WARN_ON(setup_sigframe_layout(&user, true)))
		return;

	signal_minsigstksz = sigframe_size(&user) +
		round_up(sizeof(struct frame_record), 16) +
		16; /* max alignment padding */
}

/*
 * Compile-time assertions for siginfo_t offsets. Check NSIG* as well, as
 * changes likely come with new fields that should be added below.
 */
static_assert(NSIGILL	== 11);
static_assert(NSIGFPE	== 15);
static_assert(NSIGSEGV	== 10);
static_assert(NSIGBUS	== 5);
static_assert(NSIGTRAP	== 6);
static_assert(NSIGCHLD	== 6);
static_assert(NSIGSYS	== 2);
static_assert(sizeof(siginfo_t) == 128);
static_assert(__alignof__(siginfo_t) == 8);
static_assert(offsetof(siginfo_t, si_signo)	== 0x00);
static_assert(offsetof(siginfo_t, si_errno)	== 0x04);
static_assert(offsetof(siginfo_t, si_code)	== 0x08);
static_assert(offsetof(siginfo_t, si_pid)	== 0x10);
static_assert(offsetof(siginfo_t, si_uid)	== 0x14);
static_assert(offsetof(siginfo_t, si_tid)	== 0x10);
static_assert(offsetof(siginfo_t, si_overrun)	== 0x14);
static_assert(offsetof(siginfo_t, si_status)	== 0x18);
static_assert(offsetof(siginfo_t, si_utime)	== 0x20);
static_assert(offsetof(siginfo_t, si_stime)	== 0x28);
static_assert(offsetof(siginfo_t, si_value)	== 0x18);
static_assert(offsetof(siginfo_t, si_int)	== 0x18);
static_assert(offsetof(siginfo_t, si_ptr)	== 0x18);
static_assert(offsetof(siginfo_t, si_addr)	== 0x10);
static_assert(offsetof(siginfo_t, si_addr_lsb)	== 0x18);
static_assert(offsetof(siginfo_t, si_lower)	== 0x20);
static_assert(offsetof(siginfo_t, si_upper)	== 0x28);
static_assert(offsetof(siginfo_t, si_pkey)	== 0x20);
static_assert(offsetof(siginfo_t, si_perf_data)	== 0x18);
static_assert(offsetof(siginfo_t, si_perf_type)	== 0x20);
static_assert(offsetof(siginfo_t, si_perf_flags) == 0x24);
static_assert(offsetof(siginfo_t, si_band)	== 0x10);
static_assert(offsetof(siginfo_t, si_fd)	== 0x18);
static_assert(offsetof(siginfo_t, si_call_addr)	== 0x10);
static_assert(offsetof(siginfo_t, si_syscall)	== 0x18);
static_assert(offsetof(siginfo_t, si_arch)	== 0x1c);
