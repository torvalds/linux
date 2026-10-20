// SPDX-License-Identifier: GPL-2.0
/*
 * FPU signal frame handling routines.
 */

#include <linux/compat.h>
#include <linux/cpu.h>
#include <linux/pagemap.h>

#include <asm/fpu/signal.h>
#include <asm/fpu/regset.h>
#include <asm/fpu/xstate.h>

#include <asm/sigframe.h>
#include <asm/trapnr.h>
#include <asm/trace/fpu.h>

#include "context.h"
#include "internal.h"
#include "legacy.h"
#include "xstate.h"

/*
 * Check for the presence of extended state information in the
 * user fpstate pointer in the sigcontext.
 */
static inline bool check_xstate_in_sigframe(struct fxregs_state __user *buf_fx,
					    struct _fpx_sw_bytes *fx_sw)
{
	struct fpstate *fpstate = x86_task_fpu(current)->fpstate;
	int min_xstate_size = sizeof(struct fxregs_state) +
			      sizeof(struct xstate_header);
	void __user *buf = buf_fx;
	unsigned int magic2;

	if (__copy_from_user(fx_sw, &buf_fx->sw_reserved[0], sizeof(*fx_sw)))
		return false;

	/* Check for the first magic field and other error scenarios. */
	if (fx_sw->magic1 != FP_XSTATE_MAGIC1 ||
	    fx_sw->xstate_size < min_xstate_size ||
	    fx_sw->xstate_size > fpstate->user_size ||
	    fx_sw->extended_size < fx_sw->xstate_size + FP_XSTATE_MAGIC2_SIZE)
		goto err_setfx;

	/*
	 * Check for the presence of second magic word at the end of memory
	 * layout. This detects the case where the user just copied the legacy
	 * fpstate layout with out copying the extended state information
	 * in the memory layout.
	 */
	if (__get_user(magic2, (__u32 __user *)(buf + fx_sw->xstate_size)))
		return false;
	if (unlikely(magic2 != FP_XSTATE_MAGIC2))
		goto err_setfx;

	if (fx_sw->xstate_size != fpstate->user_size ||
	    fx_sw->xfeatures != fpstate->user_xfeatures) {
		unsigned int xsize;
		u64 xfeatures;

		/* Calculate size of enabled features only. */
		xfeatures = fx_sw->xfeatures & fpstate->user_xfeatures;

		xsize = xstate_calculate_size(xfeatures, false);
		if (fx_sw->xstate_size < xsize)
			return false;

		fx_sw->xstate_size = xsize;
	}

	return true;
err_setfx:
	/*
	 * The fallback to FX-only state is used to preserve backward
	 * compatibility with user-space processes that are not aware of xsave
	 * states.
	 *
	 * In all other cases, returning false (to trigger SIGSEGV) is
	 * preferred to avoid silent user-space state corruption.
	 */
	trace_x86_fpu_xstate_check_failed(x86_task_fpu(current));

	/* Set the parameters for fx only state */
	fx_sw->magic1 = 0;
	fx_sw->xstate_size = sizeof(struct fxregs_state);
	fx_sw->xfeatures = XFEATURE_MASK_FPSSE;
	return true;
}

/*
 * Signal frame handlers.
 */
static inline bool save_fsave_header(struct task_struct *tsk, void __user *buf)
{
	if (use_fxsr()) {
		struct xregs_state *xsave = &x86_task_fpu(tsk)->fpstate->regs.xsave;
		struct user_i387_ia32_struct env;
		struct _fpstate_32 __user *fp = buf;

		fpregs_lock();
		if (!test_thread_flag(TIF_NEED_FPU_LOAD))
			fxsave(&x86_task_fpu(tsk)->fpstate->regs.fxsave);
		fpregs_unlock();

		convert_from_fxsr(&env, tsk);

		if (__copy_to_user(buf, &env, sizeof(env)) ||
		    __put_user(xsave->i387.swd, &fp->status) ||
		    __put_user(X86_FXSR_MAGIC, &fp->magic))
			return false;
	} else {
		struct fregs_state __user *fp = buf;
		u32 swd;

		if (__get_user(swd, &fp->swd) || __put_user(swd, &fp->status))
			return false;
	}

	return true;
}

/*
 * Prepare the SW reserved portion of the fxsave memory layout, indicating
 * the presence of the extended state information in the memory layout
 * pointed to by the fpstate pointer in the sigcontext.
 * This is saved when ever the FP and extended state context is
 * saved on the user stack during the signal handler delivery to the user.
 */
static inline void save_sw_bytes(struct _fpx_sw_bytes *sw_bytes, bool ia32_frame,
				 struct fpstate *fpstate)
{
	sw_bytes->magic1 = FP_XSTATE_MAGIC1;
	sw_bytes->extended_size = fpstate->user_size + FP_XSTATE_MAGIC2_SIZE;
	sw_bytes->xfeatures = fpstate->user_xfeatures;
	sw_bytes->xstate_size = fpstate->user_size;

	if (ia32_frame)
		sw_bytes->extended_size += sizeof(struct fregs_state);
}

static inline bool save_xstate_epilog(void __user *buf, int ia32_frame,
				      struct fpstate *fpstate)
{
	struct xregs_state __user *x = buf;
	struct _fpx_sw_bytes sw_bytes = {};
	int err;

	/* Setup the bytes not touched by the [f]xsave and reserved for SW. */
	save_sw_bytes(&sw_bytes, ia32_frame, fpstate);
	err = __copy_to_user(&x->i387.sw_reserved, &sw_bytes, sizeof(sw_bytes));

	if (!use_xsave())
		return !err;

	err |= __put_user(FP_XSTATE_MAGIC2,
			  (__u32 __user *)(buf + fpstate->user_size));

	/*
	 * For legacy compatible, we always set FP/SSE bits in the bit
	 * vector while saving the state to the user context. This will
	 * enable us capturing any changes(during sigreturn) to
	 * the FP/SSE bits by the legacy applications which don't touch
	 * xfeatures in the xsave header.
	 *
	 * xsave aware apps can change the xfeatures in the xsave
	 * header as well as change any contents in the memory layout.
	 * xrestore as part of sigreturn will capture all the changes.
	 */
	err |= set_xfeature_in_sigframe(x, XFEATURE_MASK_FPSSE);

	return !err;
}

static inline int copy_fpregs_to_sigframe(struct xregs_state __user *buf, u32 pkru)
{
	if (use_xsave())
		return xsave_to_user_sigframe(buf, pkru);

	if (use_fxsr())
		return fxsave_to_user_sigframe((struct fxregs_state __user *) buf);
	else
		return fnsave_to_user_sigframe((struct fregs_state __user *) buf);
}

/*
 * Save the fpu, extended register state to the user signal frame.
 *
 * 'buf_fx' is the 64-byte aligned pointer at which the [f|fx|x]save
 *  state is copied.
 *  'buf' points to the 'buf_fx' or to the fsave header followed by 'buf_fx'.
 *
 *	buf == buf_fx for 64-bit frames and 32-bit fsave frame.
 *	buf != buf_fx for 32-bit frames with fxstate.
 *
 * Save it directly to the user frame with disabled page fault handler. If
 * that faults, try to clear the frame which handles the page fault.
 *
 * If this is a 32-bit frame with fxstate, put a fsave header before
 * the aligned state at 'buf_fx'.
 *
 * For [f]xsave state, update the SW reserved fields in the [f]xsave frame
 * indicating the absence/presence of the extended state to the user.
 */
bool copy_fpstate_to_sigframe(void __user *buf, void __user *buf_fx, int size, u32 pkru)
{
	struct task_struct *tsk = current;
	struct fpstate *fpstate = x86_task_fpu(tsk)->fpstate;
	bool ia32_fxstate = (buf != buf_fx);
	int ret;

	ia32_fxstate &= (IS_ENABLED(CONFIG_X86_32) ||
			 IS_ENABLED(CONFIG_IA32_EMULATION));

	if (!access_ok(buf, size))
		return false;

	if (use_xsave()) {
		struct xregs_state __user *xbuf = buf_fx;

		/*
		 * Clear the xsave header first, so that reserved fields are
		 * initialized to zero.
		 */
		if (__clear_user(&xbuf->header, sizeof(xbuf->header)))
			return false;
	}
retry:
	/*
	 * Load the FPU registers if they are not valid for the current task.
	 * With a valid FPU state we can attempt to save the state directly to
	 * userland's stack frame which will likely succeed. If it does not,
	 * resolve the fault in the user memory and try again.
	 */
	fpregs_lock();
	if (test_thread_flag(TIF_NEED_FPU_LOAD))
		fpregs_restore_userregs();

	pagefault_disable();
	ret = copy_fpregs_to_sigframe(buf_fx, pkru);
	pagefault_enable();
	fpregs_unlock();

	if (ret) {
		if (!__clear_user(buf_fx, fpstate->user_size))
			goto retry;
		return false;
	}

	/* Save the fsave header for the 32-bit frames. */
	if ((ia32_fxstate || !use_fxsr()) && !save_fsave_header(tsk, buf))
		return false;

	if (use_fxsr() && !save_xstate_epilog(buf_fx, ia32_fxstate, fpstate))
		return false;

	return true;
}

static int __restore_fpregs_from_user(void __user *buf, u64 task_xfeatures,
				      u64 xrestore_mask, bool fx_only)
{
	if (use_xsave()) {
		u64 init_bv;
		int ret;

		/* Restore enabled features only. */
		xrestore_mask &= task_xfeatures;
		init_bv = task_xfeatures & ~xrestore_mask;
		if (likely(!fx_only))
			ret = xrstor_from_user_sigframe(buf, xrestore_mask);
		else
			ret = fxrstor_from_user_sigframe(buf);

		if (!ret && unlikely(init_bv))
			os_xrstor(&init_fpstate, init_bv);
		return ret;
	} else if (use_fxsr()) {
		return fxrstor_from_user_sigframe(buf);
	} else {
		return frstor_from_user_sigframe(buf);
	}
}

/*
 * Attempt to restore the FPU registers directly from user memory.
 * Pagefaults are handled and any errors returned are fatal.
 */
static bool restore_fpregs_from_user(void __user *buf, u64 xrestore_mask,
				     bool fx_only, size_t xstate_size)
{
	struct fpu *fpu = x86_task_fpu(current);
	int ret;

retry:
	fpregs_lock();
	/* Ensure that XFD is up to date */
	xfd_update_state(fpu->fpstate);
	pagefault_disable();
	ret = __restore_fpregs_from_user(buf, fpu->fpstate->user_xfeatures,
					 xrestore_mask, fx_only);
	pagefault_enable();

	if (unlikely(ret)) {
		/*
		 * The above did an FPU restore operation, restricted to
		 * the user portion of the registers, and failed, but the
		 * microcode might have modified the FPU registers
		 * nevertheless.
		 *
		 * If the FPU registers do not belong to current, then
		 * invalidate the FPU register state otherwise the task
		 * might preempt current and return to user space with
		 * corrupted FPU registers.
		 */
		if (test_thread_flag(TIF_NEED_FPU_LOAD))
			__cpu_invalidate_fpregs_state();
		fpregs_unlock();

		/* Try to handle #PF, but anything else is fatal. */
		if (ret != X86_TRAP_PF)
			return false;

		if (!fault_in_readable(buf, xstate_size))
			goto retry;
		return false;
	}

	/*
	 * Restore supervisor states: previous context switch etc has done
	 * XSAVES and saved the supervisor states in the kernel buffer from
	 * which they can be restored now.
	 *
	 * It would be optimal to handle this with a single XRSTORS, but
	 * this does not work because the rest of the FPU registers have
	 * been restored from a user buffer directly.
	 */
	if (test_thread_flag(TIF_NEED_FPU_LOAD) && xfeatures_mask_supervisor())
		os_xrstor_supervisor(fpu->fpstate);

	fpregs_mark_activate();
	fpregs_unlock();
	return true;
}

/*
 * Restore FPU state from a signal frame when a legacy 32-bit FP frame
 * (buf_f) is present.
 *
 * The legacy FP frame duplicates the FP state portion of the FX/XSAVE
 * frame (buf_fx). For backward compatibility, the legacy FP frame is
 * treated as the source of truth, and its state is folded into the
 * FX/XSAVE state before restoring the registers.
 */
static bool restore_from_ia32_fxstate(void __user *buf_f, void __user *buf_fx,
				      u64 xrestore_mask, bool fx_only)
{
	struct task_struct *tsk = current;
	struct fpu *fpu = x86_task_fpu(tsk);
	struct user_i387_ia32_struct env;
	union fpregs_state *fpregs;
	bool success;

	if (!IS_ENABLED(CONFIG_X86_32) && !IS_ENABLED(CONFIG_IA32_EMULATION))
		return false;

	/*
	 * Copy the legacy state because the FP portion of the FX frame has
	 * to be ignored for histerical raisins. The legacy state is folded
	 * in once the larger state has been copied.
	 */
	if (__copy_from_user(&env, buf_f, sizeof(env)))
		return false;

	/*
	 * By setting TIF_NEED_FPU_LOAD it is ensured that our xstate is
	 * not modified on context switch and that the xstate is considered
	 * to be loaded again on return to userland (overriding last_cpu avoids
	 * the optimisation).
	 */
	fpregs_lock();
	if (!test_thread_flag(TIF_NEED_FPU_LOAD)) {
		/*
		 * If supervisor states are available then save the
		 * hardware state in current's fpstate so that the
		 * supervisor state is preserved. Save the full state for
		 * simplicity. There is no point in optimizing this by only
		 * saving the supervisor states and then shuffle them to
		 * the right place in memory. It's ia32 mode. Shrug.
		 */
		if (xfeatures_mask_supervisor())
			os_xsave(fpu->fpstate);
		set_thread_flag(TIF_NEED_FPU_LOAD);
	}
	__fpu_invalidate_fpregs_state(fpu);
	__cpu_invalidate_fpregs_state();
	fpregs_unlock();

	fpregs = &fpu->fpstate->regs;
	if (use_xsave() && !fx_only) {
		if (copy_sigframe_from_user_to_xstate(tsk, buf_fx))
			return false;
	} else {
		if (__copy_from_user(&fpregs->fxsave, buf_fx,
				     sizeof(fpregs->fxsave)))
			return false;

		if (IS_ENABLED(CONFIG_X86_64)) {
			/* Reject invalid MXCSR values. */
			if (fpregs->fxsave.mxcsr & ~mxcsr_feature_mask)
				return false;
		} else {
			/* Mask invalid bits out for historical reasons (broken hardware). */
			fpregs->fxsave.mxcsr &= mxcsr_feature_mask;
		}

		/* Enforce XFEATURE_MASK_FPSSE when XSAVE is enabled */
		if (use_xsave())
			fpregs->xsave.header.xfeatures |= XFEATURE_MASK_FPSSE;
	}

	/* Fold the legacy FP storage */
	convert_to_fxsr(&fpregs->fxsave, &env);

	fpregs_lock();
	if (use_xsave()) {
		/*
		 * Remove all UABI feature bits not set in user_xfeatures
		 * from the memory xstate header which makes the full
		 * restore below bring them into init state. This works for
		 * fx_only mode as well because that has only FP and SSE
		 * set in user_xfeatures.
		 *
		 * Preserve supervisor states!
		 */
		u64 mask = xrestore_mask | xfeatures_mask_supervisor();

		fpregs->xsave.header.xfeatures &= mask;
		success = !os_xrstor_safe(fpu->fpstate,
					  fpu_kernel_cfg.max_features);
	} else {
		success = !fxrstor_safe(&fpregs->fxsave);
	}

	if (likely(success))
		fpregs_mark_activate();

	fpregs_unlock();
	return success;
}

static inline unsigned int xstate_sigframe_size(struct fpstate *fpstate)
{
	unsigned int size = fpstate->user_size;

	return use_xsave() ? size + FP_XSTATE_MAGIC2_SIZE : size;
}

/*
 * Restore FPU state from a sigframe:
 */
bool fpu__restore_sig(void __user *buf, int ia32_frame)
{
	struct fpu *fpu = x86_task_fpu(current);
	bool success = false, fx_only = false;
	bool ia32_fxstate = false;
	void __user *buf_fx = buf;
	unsigned int size;
	u64 xrestore_mask;

	if (unlikely(!buf)) {
		fpu__clear_user_states(fpu);
		return true;
	}

	size = xstate_sigframe_size(fpu->fpstate);

	ia32_frame &= (IS_ENABLED(CONFIG_X86_32) ||
		       IS_ENABLED(CONFIG_IA32_EMULATION));

	/*
	 * Only FXSR enabled systems need the FX state quirk.
	 * FRSTOR does not need it and can use the fast path.
	 */
	if (ia32_frame && use_fxsr()) {
		buf_fx = buf + sizeof(struct fregs_state);
		size += sizeof(struct fregs_state);
		ia32_fxstate = true;
	}

	if (!access_ok(buf, size))
		goto out;

	if (use_xsave()) {
		struct _fpx_sw_bytes fx_sw_user;

		if (!check_xstate_in_sigframe(buf_fx, &fx_sw_user))
			goto out;

		fx_only = !fx_sw_user.magic1;
		xrestore_mask = fx_sw_user.xfeatures;
		size = fx_sw_user.xstate_size;
	} else {
		xrestore_mask = XFEATURE_MASK_FPSSE;
		size = fpu->fpstate->user_size;
	}

	if (ia32_fxstate)
		success = restore_from_ia32_fxstate(buf, buf_fx, xrestore_mask, fx_only);
	else
		success = restore_fpregs_from_user(buf_fx, xrestore_mask, fx_only, size);
out:
	if (unlikely(!success))
		fpu__clear_user_states(fpu);
	return success;
}

unsigned long
fpu__alloc_mathframe(unsigned long sp, int ia32_frame,
		     unsigned long *buf_fx, unsigned long *size)
{
	unsigned long frame_size = xstate_sigframe_size(x86_task_fpu(current)->fpstate);

	*buf_fx = sp = round_down(sp - frame_size, 64);
	if (ia32_frame && use_fxsr()) {
		frame_size += sizeof(struct fregs_state);
		sp -= sizeof(struct fregs_state);
	}

	*size = frame_size;

	return sp;
}

unsigned long __init fpu__get_fpstate_size(void)
{
	unsigned long ret = fpu_user_cfg.max_size;

	if (use_xsave())
		ret += FP_XSTATE_MAGIC2_SIZE;

	/*
	 * This space is needed on (most) 32-bit kernels, or when a 32-bit
	 * app is running on a 64-bit kernel. To keep things simple, just
	 * assume the worst case and always include space for 'freg_state',
	 * even for 64-bit apps on 64-bit kernels. This wastes a bit of
	 * space, but keeps the code simple.
	 */
	if ((IS_ENABLED(CONFIG_IA32_EMULATION) ||
	     IS_ENABLED(CONFIG_X86_32)) && use_fxsr())
		ret += sizeof(struct fregs_state);

	return ret;
}

