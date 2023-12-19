// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 1994 Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *  General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */
#include <asm/fpu/api.h>
#include <asm/fpu/regset.h>
#include <asm/fpu/sched.h>
#include <asm/fpu/signal.h>
#include <asm/fpu/types.h>
#include <asm/traps.h>
#include <asm/irq_regs.h>

#include <uapi/asm/kvm.h>

#include <linux/hardirq.h>
#include <linux/pkeys.h>
#include <linux/vmalloc.h>

#include "context.h"
#include "internal.h"
#include "legacy.h"
#include "xstate.h"

#define CREATE_TRACE_POINTS
#include <asm/trace/fpu.h>

#ifdef CONFIG_X86_64
DEFINE_STATIC_KEY_FALSE(__fpu_state_size_dynamic);
DEFINE_PER_CPU(u64, xfd_state);
#endif

/* The FPU state configuration data for kernel and user space */
struct fpu_state_config	fpu_kernel_cfg __ro_after_init;
struct fpu_state_config fpu_user_cfg __ro_after_init;

/*
 * Represents the initial FPU state. It's mostly (but not completely) zeroes,
 * depending on the FPU hardware format:
 */
struct fpstate init_fpstate __ro_after_init;

/* Track in-kernel FPU usage */
static DEFINE_PER_CPU(bool, in_kernel_fpu);

/*
 * Track which context is using the FPU on the CPU:
 */
DEFINE_PER_CPU(struct fpu *, fpu_fpregs_owner_ctx);

/*
 * Can we use the FPU in kernel mode with the
 * whole "kernel_fpu_begin/end()" sequence?
 */
bool irq_fpu_usable(void)
{
	if (WARN_ON_ONCE(in_nmi()))
		return false;

	/* In kernel FPU usage already active? */
	if (this_cpu_read(in_kernel_fpu))
		return false;

	/*
	 * When not in NMI or hard interrupt context, FPU can be used in:
	 *
	 * - Task context except from within fpregs_lock()'ed critical
	 *   regions.
	 *
	 * - Soft interrupt processing context which cannot happen
	 *   while in a fpregs_lock()'ed critical region.
	 */
	if (!in_hardirq())
		return true;

	/*
	 * In hard interrupt context it's safe when soft interrupts
	 * are enabled, which means the interrupt did not hit in
	 * a fpregs_lock()'ed critical region.
	 */
	return !softirq_count();
}
EXPORT_SYMBOL(irq_fpu_usable);

/*
 * Track AVX512 state use because it is known to slow the max clock
 * speed of the core.
 */
static void update_avx_timestamp(struct fpu *fpu)
{

#define AVX512_TRACKING_MASK	(XFEATURE_MASK_ZMM_Hi256 | XFEATURE_MASK_Hi16_ZMM)

	if (fpu->fpstate->regs.xsave.header.xfeatures & AVX512_TRACKING_MASK)
		fpu->avx512_timestamp = jiffies;
}

/*
 * Save the FPU register state in fpu->fpstate->regs. The register state is
 * preserved.
 *
 * Must be called with fpregs_lock() held.
 *
 * The legacy FNSAVE instruction clears all FPU state unconditionally, so
 * register state has to be reloaded. That might be a pointless exercise
 * when the FPU is going to be used by another task right after that. But
 * this only affects 20+ years old 32bit systems and avoids conditionals all
 * over the place.
 *
 * FXSAVE and all XSAVE variants preserve the FPU register state.
 */
void save_fpregs_to_fpstate(struct fpu *fpu)
{
	if (likely(use_xsave())) {
		os_xsave(fpu->fpstate);
		update_avx_timestamp(fpu);
		return;
	}

	if (likely(use_fxsr())) {
		fxsave(&fpu->fpstate->regs.fxsave);
		return;
	}

	/*
	 * Legacy FPU register saving, FNSAVE always clears FPU registers,
	 * so we have to reload them from the memory state.
	 */
	asm volatile("fnsave %[fp]; fwait" : [fp] "=m" (fpu->fpstate->regs.fsave));
	frstor(&fpu->fpstate->regs.fsave);
}

void restore_fpregs_from_fpstate(struct fpstate *fpstate, u64 mask)
{
	/*
	 * AMD K7/K8 and later CPUs up to Zen don't save/restore
	 * FDP/FIP/FOP unless an exception is pending. Clear the x87 state
	 * here by setting it to fixed values.  "m" is a random variable
	 * that should be in L1.
	 */
	if (unlikely(static_cpu_has_bug(X86_BUG_FXSAVE_LEAK))) {
		asm volatile(
			"fnclex\n\t"
			"emms\n\t"
			"fildl %P[addr]"	/* set F?P to defined value */
			: : [addr] "m" (fpstate));
	}

	if (use_xsave()) {
		/*
		 * Dynamically enabled features are enabled in XCR0, but
		 * usage requires also that the corresponding bits in XFD
		 * are cleared.  If the bits are set then using a related
		 * instruction will raise #NM. This allows to do the
		 * allocation of the larger FPU buffer lazy from #NM or if
		 * the task has no permission to kill it which would happen
		 * via #UD if the feature is disabled in XCR0.
		 *
		 * XFD state is following the same life time rules as
		 * XSTATE and to restore state correctly XFD has to be
		 * updated before XRSTORS otherwise the component would
		 * stay in or go into init state even if the bits are set
		 * in fpstate::regs::xsave::xfeatures.
		 */
		xfd_update_state(fpstate);

		/*
		 * Restoring state always needs to modify all features
		 * which are in @mask even if the current task cannot use
		 * extended features.
		 *
		 * So fpstate->xfeatures cannot be used here, because then
		 * a feature for which the task has no permission but was
		 * used by the previous task would not go into init state.
		 */
		mask = fpu_kernel_cfg.max_features & mask;

		os_xrstor(fpstate, mask);
	} else {
		if (use_fxsr())
			fxrstor(&fpstate->regs.fxsave);
		else
			frstor(&fpstate->regs.fsave);
	}
}

void fpu_reset_from_exception_fixup(void)
{
	restore_fpregs_from_fpstate(&init_fpstate, XFEATURE_MASK_FPSTATE);
}

#if IS_ENABLED(CONFIG_KVM)
static void __fpstate_reset(struct fpstate *fpstate, u64 xfd);

static void fpu_init_guest_permissions(struct fpu_guest *gfpu)
{
	struct fpu_state_perm *fpuperm;
	u64 perm;

	if (!IS_ENABLED(CONFIG_X86_64))
		return;

	spin_lock_irq(&current->sighand->siglock);
	fpuperm = &current->group_leader->thread.fpu.guest_perm;
	perm = fpuperm->__state_perm;

	/* First fpstate allocation locks down permissions. */
	WRITE_ONCE(fpuperm->__state_perm, perm | FPU_GUEST_PERM_LOCKED);

	spin_unlock_irq(&current->sighand->siglock);

	gfpu->perm = perm & ~FPU_GUEST_PERM_LOCKED;
}

bool fpu_alloc_guest_fpstate(struct fpu_guest *gfpu)
{
	struct fpstate *fpstate;
	unsigned int size;

	size = fpu_user_cfg.default_size + ALIGN(offsetof(struct fpstate, regs), 64);
	fpstate = vzalloc(size);
	if (!fpstate)
		return false;

	/* Leave xfd to 0 (the reset value defined by spec) */
	__fpstate_reset(fpstate, 0);
	fpstate_init_user(fpstate);
	fpstate->is_valloc	= true;
	fpstate->is_guest	= true;

	gfpu->fpstate		= fpstate;
	gfpu->xfeatures		= fpu_user_cfg.default_features;
	gfpu->perm		= fpu_user_cfg.default_features;

	/*
	 * KVM sets the FP+SSE bits in the XSAVE header when copying FPU state
	 * to userspace, even when XSAVE is unsupported, so that restoring FPU
	 * state on a different CPU that does support XSAVE can cleanly load
	 * the incoming state using its natural XSAVE.  In other words, KVM's
	 * uABI size may be larger than this host's default size.  Conversely,
	 * the default size should never be larger than KVM's base uABI size;
	 * all features that can expand the uABI size must be opt-in.
	 */
	gfpu->uabi_size		= sizeof(struct kvm_xsave);
	if (WARN_ON_ONCE(fpu_user_cfg.default_size > gfpu->uabi_size))
		gfpu->uabi_size = fpu_user_cfg.default_size;

	fpu_init_guest_permissions(gfpu);

	return true;
}
EXPORT_SYMBOL_GPL(fpu_alloc_guest_fpstate);

void fpu_free_guest_fpstate(struct fpu_guest *gfpu)
{
	struct fpstate *fps = gfpu->fpstate;

	if (!fps)
		return;

	if (WARN_ON_ONCE(!fps->is_valloc || !fps->is_guest || fps->in_use))
		return;

	gfpu->fpstate = NULL;
	vfree(fps);
}
EXPORT_SYMBOL_GPL(fpu_free_guest_fpstate);

/*
  * fpu_enable_guest_xfd_features - Check xfeatures against guest perm and enable
  * @guest_fpu:         Pointer to the guest FPU container
  * @xfeatures:         Features requested by guest CPUID
  *
  * Enable all dynamic xfeatures according to guest perm and requested CPUID.
  *
  * Return: 0 on success, error code otherwise
  */
int fpu_enable_guest_xfd_features(struct fpu_guest *guest_fpu, u64 xfeatures)
{
	lockdep_assert_preemption_enabled();

	/* Nothing to do if all requested features are already enabled. */
	xfeatures &= ~guest_fpu->xfeatures;
	if (!xfeatures)
		return 0;

	return __xfd_enable_feature(xfeatures, guest_fpu);
}
EXPORT_SYMBOL_GPL(fpu_enable_guest_xfd_features);

#ifdef CONFIG_X86_64
void fpu_update_guest_xfd(struct fpu_guest *guest_fpu, u64 xfd)
{
	fpregs_lock();
	guest_fpu->fpstate->xfd = xfd;
	if (guest_fpu->fpstate->in_use)
		xfd_update_state(guest_fpu->fpstate);
	fpregs_unlock();
}
EXPORT_SYMBOL_GPL(fpu_update_guest_xfd);

/**
 * fpu_sync_guest_vmexit_xfd_state - Synchronize XFD MSR and software state
 *
 * Must be invoked from KVM after a VMEXIT before enabling interrupts when
 * XFD write emulation is disabled. This is required because the guest can
 * freely modify XFD and the state at VMEXIT is not guaranteed to be the
 * same as the state on VMENTER. So software state has to be udpated before
 * any operation which depends on it can take place.
 *
 * Note: It can be invoked unconditionally even when write emulation is
 * enabled for the price of a then pointless MSR read.
 */
void fpu_sync_guest_vmexit_xfd_state(void)
{
	struct fpstate *fps = current->thread.fpu.fpstate;

	lockdep_assert_irqs_disabled();
	if (fpu_state_size_dynamic()) {
		rdmsrl(MSR_IA32_XFD, fps->xfd);
		__this_cpu_write(xfd_state, fps->xfd);
	}
}
EXPORT_SYMBOL_GPL(fpu_sync_guest_vmexit_xfd_state);
#endif /* CONFIG_X86_64 */

int fpu_swap_kvm_fpstate(struct fpu_guest *guest_fpu, bool enter_guest)
{
	struct fpstate *guest_fps = guest_fpu->fpstate;
	struct fpu *fpu = &current->thread.fpu;
	struct fpstate *cur_fps = fpu->fpstate;

	fpregs_lock();
	if (!cur_fps->is_confidential && !test_thread_flag(TIF_NEED_FPU_LOAD))
		save_fpregs_to_fpstate(fpu);

	/* Swap fpstate */
	if (enter_guest) {
		fpu->__task_fpstate = cur_fps;
		fpu->fpstate = guest_fps;
		guest_fps->in_use = true;
	} else {
		guest_fps->in_use = false;
		fpu->fpstate = fpu->__task_fpstate;
		fpu->__task_fpstate = NULL;
	}

	cur_fps = fpu->fpstate;

	if (!cur_fps->is_confidential) {
		/* Includes XFD update */
		restore_fpregs_from_fpstate(cur_fps, XFEATURE_MASK_FPSTATE);
	} else {
		/*
		 * XSTATE is restored by firmware from encrypted
		 * memory. Make sure XFD state is correct while
		 * running with guest fpstate
		 */
		xfd_update_state(cur_fps);
	}

	fpregs_mark_activate();
	fpregs_unlock();
	return 0;
}
EXPORT_SYMBOL_GPL(fpu_swap_kvm_fpstate);

void fpu_copy_guest_fpstate_to_uabi(struct fpu_guest *gfpu, void *buf,
				    unsigned int size, u64 xfeatures, u32 pkru)
{
	struct fpstate *kstate = gfpu->fpstate;
	union fpregs_state *ustate = buf;
	struct membuf mb = { .p = buf, .left = size };

	if (cpu_feature_enabled(X86_FEATURE_XSAVE)) {
		__copy_xstate_to_uabi_buf(mb, kstate, xfeatures, pkru,
					  XSTATE_COPY_XSAVE);
	} else {
		memcpy(&ustate->fxsave, &kstate->regs.fxsave,
		       sizeof(ustate->fxsave));
		/* Make it restorable on a XSAVE enabled host */
		ustate->xsave.header.xfeatures = XFEATURE_MASK_FPSSE;
	}
}
EXPORT_SYMBOL_GPL(fpu_copy_guest_fpstate_to_uabi);

int fpu_copy_uabi_to_guest_fpstate(struct fpu_guest *gfpu, const void *buf,
				   u64 xcr0, u32 *vpkru)
{
	struct fpstate *kstate = gfpu->fpstate;
	const union fpregs_state *ustate = buf;

	if (!cpu_feature_enabled(X86_FEATURE_XSAVE)) {
		if (ustate->xsave.header.xfeatures & ~XFEATURE_MASK_FPSSE)
			return -EINVAL;
		if (ustate->fxsave.mxcsr & ~mxcsr_feature_mask)
			return -EINVAL;
		memcpy(&kstate->regs.fxsave, &ustate->fxsave, sizeof(ustate->fxsave));
		return 0;
	}

	if (ustate->xsave.header.xfeatures & ~xcr0)
		return -EINVAL;

	/*
	 * Nullify @vpkru to preserve its current value if PKRU's bit isn't set
	 * in the header.  KVM's odd ABI is to leave PKRU untouched in this
	 * case (all other components are eventually re-initialized).
	 */
	if (!(ustate->xsave.header.xfeatures & XFEATURE_MASK_PKRU))
		vpkru = NULL;

	return copy_uabi_from_kernel_to_xstate(kstate, ustate, vpkru);
}
EXPORT_SYMBOL_GPL(fpu_copy_uabi_to_guest_fpstate);
#endif /* CONFIG_KVM */

void kernel_fpu_begin_mask(unsigned int kfpu_mask)
{
	preempt_disable();

	WARN_ON_FPU(!irq_fpu_usable());
	WARN_ON_FPU(this_cpu_read(in_kernel_fpu));

	this_cpu_write(in_kernel_fpu, true);

	if (!(current->flags & (PF_KTHREAD | PF_USER_WORKER)) &&
	    !test_thread_flag(TIF_NEED_FPU_LOAD)) {
		set_thread_flag(TIF_NEED_FPU_LOAD);
		save_fpregs_to_fpstate(&current->thread.fpu);
	}
	__cpu_invalidate_fpregs_state();

	/* Put sane initial values into the control registers. */
	if (likely(kfpu_mask & KFPU_MXCSR) && boot_cpu_has(X86_FEATURE_XMM))
		ldmxcsr(MXCSR_DEFAULT);

	if (unlikely(kfpu_mask & KFPU_387) && boot_cpu_has(X86_FEATURE_FPU))
		asm volatile ("fninit");
}
EXPORT_SYMBOL_GPL(kernel_fpu_begin_mask);

void kernel_fpu_end(void)
{
	WARN_ON_FPU(!this_cpu_read(in_kernel_fpu));

	this_cpu_write(in_kernel_fpu, false);
	preempt_enable();
}
EXPORT_SYMBOL_GPL(kernel_fpu_end);

/*
 * Sync the FPU register state to current's memory register state when the
 * current task owns the FPU. The hardware register state is preserved.
 */
void fpu_sync_fpstate(struct fpu *fpu)
{
	WARN_ON_FPU(fpu != &current->thread.fpu);

	fpregs_lock();
	trace_x86_fpu_before_save(fpu);

	if (!test_thread_flag(TIF_NEED_FPU_LOAD))
		save_fpregs_to_fpstate(fpu);

	trace_x86_fpu_after_save(fpu);
	fpregs_unlock();
}

static inline unsigned int init_fpstate_copy_size(void)
{
	if (!use_xsave())
		return fpu_kernel_cfg.default_size;

	/* XSAVE(S) just needs the legacy and the xstate header part */
	return sizeof(init_fpstate.regs.xsave);
}

static inline void fpstate_init_fxstate(struct fpstate *fpstate)
{
	fpstate->regs.fxsave.cwd = 0x37f;
	fpstate->regs.fxsave.mxcsr = MXCSR_DEFAULT;
}

/*
 * Legacy x87 fpstate state init:
 */
static inline void fpstate_init_fstate(struct fpstate *fpstate)
{
	fpstate->regs.fsave.cwd = 0xffff037fu;
	fpstate->regs.fsave.swd = 0xffff0000u;
	fpstate->regs.fsave.twd = 0xffffffffu;
	fpstate->regs.fsave.fos = 0xffff0000u;
}

/*
 * Used in two places:
 * 1) Early boot to setup init_fpstate for non XSAVE systems
 * 2) fpu_init_fpstate_user() which is invoked from KVM
 */
void fpstate_init_user(struct fpstate *fpstate)
{
	if (!cpu_feature_enabled(X86_FEATURE_FPU)) {
		fpstate_init_soft(&fpstate->regs.soft);
		return;
	}

	xstate_init_xcomp_bv(&fpstate->regs.xsave, fpstate->xfeatures);

	if (cpu_feature_enabled(X86_FEATURE_FXSR))
		fpstate_init_fxstate(fpstate);
	else
		fpstate_init_fstate(fpstate);
}

static void __fpstate_reset(struct fpstate *fpstate, u64 xfd)
{
	/* Initialize sizes and feature masks */
	fpstate->size		= fpu_kernel_cfg.default_size;
	fpstate->user_size	= fpu_user_cfg.default_size;
	fpstate->xfeatures	= fpu_kernel_cfg.default_features;
	fpstate->user_xfeatures	= fpu_user_cfg.default_features;
	fpstate->xfd		= xfd;
}

void fpstate_reset(struct fpu *fpu)
{
	/* Set the fpstate pointer to the default fpstate */
	fpu->fpstate = &fpu->__fpstate;
	__fpstate_reset(fpu->fpstate, init_fpstate.xfd);

	/* Initialize the permission related info in fpu */
	fpu->perm.__state_perm		= fpu_kernel_cfg.default_features;
	fpu->perm.__state_size		= fpu_kernel_cfg.default_size;
	fpu->perm.__user_state_size	= fpu_user_cfg.default_size;
	/* Same defaults for guests */
	fpu->guest_perm = fpu->perm;
}

static inline void fpu_inherit_perms(struct fpu *dst_fpu)
{
	if (fpu_state_size_dynamic()) {
		struct fpu *src_fpu = &current->group_leader->thread.fpu;

		spin_lock_irq(&current->sighand->siglock);
		/* Fork also inherits the permissions of the parent */
		dst_fpu->perm = src_fpu->perm;
		dst_fpu->guest_perm = src_fpu->guest_perm;
		spin_unlock_irq(&current->sighand->siglock);
	}
}

/* A passed ssp of zero will not cause any update */
static int update_fpu_shstk(struct task_struct *dst, unsigned long ssp)
{
#ifdef CONFIG_X86_USER_SHADOW_STACK
	struct cet_user_state *xstate;

	/* If ssp update is not needed. */
	if (!ssp)
		return 0;

	xstate = get_xsave_addr(&dst->thread.fpu.fpstate->regs.xsave,
				XFEATURE_CET_USER);

	/*
	 * If there is a non-zero ssp, then 'dst' must be configured with a shadow
	 * stack and the fpu state should be up to date since it was just copied
	 * from the parent in fpu_clone(). So there must be a valid non-init CET
	 * state location in the buffer.
	 */
	if (WARN_ON_ONCE(!xstate))
		return 1;

	xstate->user_ssp = (u64)ssp;
#endif
	return 0;
}

/* Clone current's FPU state on fork */
int fpu_clone(struct task_struct *dst, unsigned long clone_flags, bool minimal,
	      unsigned long ssp)
{
	struct fpu *src_fpu = &current->thread.fpu;
	struct fpu *dst_fpu = &dst->thread.fpu;

	/* The new task's FPU state cannot be valid in the hardware. */
	dst_fpu->last_cpu = -1;

	fpstate_reset(dst_fpu);

	if (!cpu_feature_enabled(X86_FEATURE_FPU))
		return 0;

	/*
	 * Enforce reload for user space tasks and prevent kernel threads
	 * from trying to save the FPU registers on context switch.
	 */
	set_tsk_thread_flag(dst, TIF_NEED_FPU_LOAD);

	/*
	 * No FPU state inheritance for kernel threads and IO
	 * worker threads.
	 */
	if (minimal) {
		/* Clear out the minimal state */
		memcpy(&dst_fpu->fpstate->regs, &init_fpstate.regs,
		       init_fpstate_copy_size());
		return 0;
	}

	/*
	 * If a new feature is added, ensure all dynamic features are
	 * caller-saved from here!
	 */
	BUILD_BUG_ON(XFEATURE_MASK_USER_DYNAMIC != XFEATURE_MASK_XTILE_DATA);

	/*
	 * Save the default portion of the current FPU state into the
	 * clone. Assume all dynamic features to be defined as caller-
	 * saved, which enables skipping both the expansion of fpstate
	 * and the copying of any dynamic state.
	 *
	 * Do not use memcpy() when TIF_NEED_FPU_LOAD is set because
	 * copying is not valid when current uses non-default states.
	 */
	fpregs_lock();
	if (test_thread_flag(TIF_NEED_FPU_LOAD))
		fpregs_restore_userregs();
	save_fpregs_to_fpstate(dst_fpu);
	fpregs_unlock();
	if (!(clone_flags & CLONE_THREAD))
		fpu_inherit_perms(dst_fpu);

	/*
	 * Children never inherit PASID state.
	 * Force it to have its init value:
	 */
	if (use_xsave())
		dst_fpu->fpstate->regs.xsave.header.xfeatures &= ~XFEATURE_MASK_PASID;

	/*
	 * Update shadow stack pointer, in case it changed during clone.
	 */
	if (update_fpu_shstk(dst, ssp))
		return 1;

	trace_x86_fpu_copy_src(src_fpu);
	trace_x86_fpu_copy_dst(dst_fpu);

	return 0;
}

/*
 * Whitelist the FPU register state embedded into task_struct for hardened
 * usercopy.
 */
void fpu_thread_struct_whitelist(unsigned long *offset, unsigned long *size)
{
	*offset = offsetof(struct thread_struct, fpu.__fpstate.regs);
	*size = fpu_kernel_cfg.default_size;
}

/*
 * Drops current FPU state: deactivates the fpregs and
 * the fpstate. NOTE: it still leaves previous contents
 * in the fpregs in the eager-FPU case.
 *
 * This function can be used in cases where we know that
 * a state-restore is coming: either an explicit one,
 * or a reschedule.
 */
void fpu__drop(struct fpu *fpu)
{
	preempt_disable();

	if (fpu == &current->thread.fpu) {
		/* Ignore delayed exceptions from user space */
		asm volatile("1: fwait\n"
			     "2:\n"
			     _ASM_EXTABLE(1b, 2b));
		fpregs_deactivate(fpu);
	}

	trace_x86_fpu_dropped(fpu);

	preempt_enable();
}

/*
 * Clear FPU registers by setting them up from the init fpstate.
 * Caller must do fpregs_[un]lock() around it.
 */
static inline void restore_fpregs_from_init_fpstate(u64 features_mask)
{
	if (use_xsave())
		os_xrstor(&init_fpstate, features_mask);
	else if (use_fxsr())
		fxrstor(&init_fpstate.regs.fxsave);
	else
		frstor(&init_fpstate.regs.fsave);

	pkru_write_default();
}

/*
 * Reset current->fpu memory state to the init values.
 */
static void fpu_reset_fpregs(void)
{
	struct fpu *fpu = &current->thread.fpu;

	fpregs_lock();
	__fpu_invalidate_fpregs_state(fpu);
	/*
	 * This does not change the actual hardware registers. It just
	 * resets the memory image and sets TIF_NEED_FPU_LOAD so a
	 * subsequent return to usermode will reload the registers from the
	 * task's memory image.
	 *
	 * Do not use fpstate_init() here. Just copy init_fpstate which has
	 * the correct content already except for PKRU.
	 *
	 * PKRU handling does not rely on the xstate when restoring for
	 * user space as PKRU is eagerly written in switch_to() and
	 * flush_thread().
	 */
	memcpy(&fpu->fpstate->regs, &init_fpstate.regs, init_fpstate_copy_size());
	set_thread_flag(TIF_NEED_FPU_LOAD);
	fpregs_unlock();
}

/*
 * Reset current's user FPU states to the init states.  current's
 * supervisor states, if any, are not modified by this function.  The
 * caller guarantees that the XSTATE header in memory is intact.
 */
void fpu__clear_user_states(struct fpu *fpu)
{
	WARN_ON_FPU(fpu != &current->thread.fpu);

	fpregs_lock();
	if (!cpu_feature_enabled(X86_FEATURE_FPU)) {
		fpu_reset_fpregs();
		fpregs_unlock();
		return;
	}

	/*
	 * Ensure that current's supervisor states are loaded into their
	 * corresponding registers.
	 */
	if (xfeatures_mask_supervisor() &&
	    !fpregs_state_valid(fpu, smp_processor_id()))
		os_xrstor_supervisor(fpu->fpstate);

	/* Reset user states in registers. */
	restore_fpregs_from_init_fpstate(XFEATURE_MASK_USER_RESTORE);

	/*
	 * Now all FPU registers have their desired values.  Inform the FPU
	 * state machine that current's FPU registers are in the hardware
	 * registers. The memory image does not need to be updated because
	 * any operation relying on it has to save the registers first when
	 * current's FPU is marked active.
	 */
	fpregs_mark_activate();
	fpregs_unlock();
}

void fpu_flush_thread(void)
{
	fpstate_reset(&current->thread.fpu);
	fpu_reset_fpregs();
}
/*
 * Load FPU context before returning to userspace.
 */
void switch_fpu_return(void)
{
	if (!static_cpu_has(X86_FEATURE_FPU))
		return;

	fpregs_restore_userregs();
}
EXPORT_SYMBOL_GPL(switch_fpu_return);

void fpregs_lock_and_load(void)
{
	/*
	 * fpregs_lock() only disables preemption (mostly). So modifying state
	 * in an interrupt could screw up some in progress fpregs operation.
	 * Warn about it.
	 */
	WARN_ON_ONCE(!irq_fpu_usable());
	WARN_ON_ONCE(current->flags & PF_KTHREAD);

	fpregs_lock();

	fpregs_assert_state_consistent();

	if (test_thread_flag(TIF_NEED_FPU_LOAD))
		fpregs_restore_userregs();
}

#ifdef CONFIG_X86_DEBUG_FPU
/*
 * If current FPU state according to its tracking (loaded FPU context on this
 * CPU) is not valid then we must have TIF_NEED_FPU_LOAD set so the context is
 * loaded on return to userland.
 */
void fpregs_assert_state_consistent(void)
{
	struct fpu *fpu = &current->thread.fpu;

	if (test_thread_flag(TIF_NEED_FPU_LOAD))
		return;

	WARN_ON_FPU(!fpregs_state_valid(fpu, smp_processor_id()));
}
EXPORT_SYMBOL_GPL(fpregs_assert_state_consistent);
#endif

void fpregs_mark_activate(void)
{
	struct fpu *fpu = &current->thread.fpu;

	fpregs_activate(fpu);
	fpu->last_cpu = smp_processor_id();
	clear_thread_flag(TIF_NEED_FPU_LOAD);
}

/*
 * x87 math exception handling:
 */

int fpu__exception_code(struct fpu *fpu, int trap_nr)
{
	int err;

	if (trap_nr == X86_TRAP_MF) {
		unsigned short cwd, swd;
		/*
		 * (~cwd & swd) will mask out exceptions that are not set to unmasked
		 * status.  0x3f is the exception bits in these regs, 0x200 is the
		 * C1 reg you need in case of a stack fault, 0x040 is the stack
		 * fault bit.  We should only be taking one exception at a time,
		 * so if this combination doesn't produce any single exception,
		 * then we have a bad program that isn't synchronizing its FPU usage
		 * and it will suffer the consequences since we won't be able to
		 * fully reproduce the context of the exception.
		 */
		if (boot_cpu_has(X86_FEATURE_FXSR)) {
			cwd = fpu->fpstate->regs.fxsave.cwd;
			swd = fpu->fpstate->regs.fxsave.swd;
		} else {
			cwd = (unsigned short)fpu->fpstate->regs.fsave.cwd;
			swd = (unsigned short)fpu->fpstate->regs.fsave.swd;
		}

		err = swd & ~cwd;
	} else {
		/*
		 * The SIMD FPU exceptions are handled a little differently, as there
		 * is only a single status/control register.  Thus, to determine which
		 * unmasked exception was caught we must mask the exception mask bits
		 * at 0x1f80, and then use these to mask the exception bits at 0x3f.
		 */
		unsigned short mxcsr = MXCSR_DEFAULT;

		if (boot_cpu_has(X86_FEATURE_XMM))
			mxcsr = fpu->fpstate->regs.fxsave.mxcsr;

		err = ~(mxcsr >> 7) & mxcsr;
	}

	if (err & 0x001) {	/* Invalid op */
		/*
		 * swd & 0x240 == 0x040: Stack Underflow
		 * swd & 0x240 == 0x240: Stack Overflow
		 * User must clear the SF bit (0x40) if set
		 */
		return FPE_FLTINV;
	} else if (err & 0x004) { /* Divide by Zero */
		return FPE_FLTDIV;
	} else if (err & 0x008) { /* Overflow */
		return FPE_FLTOVF;
	} else if (err & 0x012) { /* Denormal, Underflow */
		return FPE_FLTUND;
	} else if (err & 0x020) { /* Precision */
		return FPE_FLTRES;
	}

	/*
	 * If we're using IRQ 13, or supposedly even some trap
	 * X86_TRAP_MF implementations, it's possible
	 * we get a spurious trap, which is not an error.
	 */
	return 0;
}

/*
 * Initialize register state that may prevent from entering low-power idle.
 * This function will be invoked from the cpuidle driver only when needed.
 */
noinstr void fpu_idle_fpregs(void)
{
	/* Note: AMX_TILE being enabled implies XGETBV1 support */
	if (cpu_feature_enabled(X86_FEATURE_AMX_TILE) &&
	    (xfeatures_in_use() & XFEATURE_MASK_XTILE)) {
		tile_release();
		__this_cpu_write(fpu_fpregs_owner_ctx, NULL);
	}
}
