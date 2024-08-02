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
static inline bool check_xstate_in_sigframe(struct fxregs_state __user *fxbuf,
					    struct _fpx_sw_bytes *fx_sw)
{
	int min_xstate_size = sizeof(struct fxregs_state) +
			      sizeof(struct xstate_header);
	void __user *fpstate = fxbuf;
	unsigned int magic2;

	if (__copy_from_user(fx_sw, &fxbuf->sw_reserved[0], sizeof(*fx_sw)))
		return false;

	/* Check for the first magic field and other error scenarios. */
	if (fx_sw->magic1 != FP_XSTATE_MAGIC1 ||
	    fx_sw->xstate_size < min_xstate_size ||
	    fx_sw->xstate_size > current->thread.fpu.fpstate->user_size ||
	    fx_sw->xstate_size > fx_sw->extended_size)
		goto setfx;

	/*
	 * Check for the presence of second magic word at the end of memory
	 * layout. This detects the case where the user just copied the legacy
	 * fpstate layout with out copying the extended state information
	 * in the memory layout.
	 */
	if (__get_user(magic2, (__u32 __user *)(fpstate + fx_sw->xstate_size)))
		return false;

	if (likely(magic2 == FP_XSTATE_MAGIC2))
		return true;
setfx:
	trace_x86_fpu_xstate_check_failed(&current->thread.fpu);

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
		struct xregs_state *xsave = &tsk->thread.fpu.fpstate->regs.xsave;
		struct user_i387_ia32_struct env;
		struct _fpstate_32 __user *fp = buf;

		fpregs_lock();
		if (!test_thread_flag(TIF_NEED_FPU_LOAD))
			fxsave(&tsk->thread.fpu.fpstate->regs.fxsave);
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
	u32 xfeatures;
	int err;

	/* Setup the bytes not touched by the [f]xsave and reserved for SW. */
	save_sw_bytes(&sw_bytes, ia32_frame, fpstate);
	err = __copy_to_user(&x->i387.sw_reserved, &sw_bytes, sizeof(sw_bytes));

	if (!use_xsave())
		return !err;

	err |= __put_user(FP_XSTATE_MAGIC2,
			  (__u32 __user *)(buf + fpstate->user_size));

	/*
	 * Read the xfeatures which we copied (directly from the cpu or
	 * from the state in task struct) to the user buffers.
	 */
	err |= __get_user(xfeatures, (__u32 __user *)&x->header.xfeatures);

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
	xfeatures |= XFEATURE_MASK_FPSSE;

	err |= __put_user(xfeatures, (__u32 __user *)&x->header.xfeatures);

	return !err;
}

static inline int copy_fpregs_to_sigframe(struct xregs_state __user *buf, u32 pkru)
{
	if (use_xsave())
		return xsave_to_user_sigframe(buf);
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
	struct fpstate *fpstate = tsk->thread.fpu.fpstate;
	bool ia32_fxstate = (buf != buf_fx);
	int ret;

	ia32_fxstate &= (IS_ENABLED(CONFIG_X86_32) ||
			 IS_ENABLED(CONFIG_IA32_EMULATION));

	if (!static_cpu_has(X86_FEATURE_FPU)) {
		struct user_i387_ia32_struct fp;

		fpregs_soft_get(current, NULL, (struct membuf){.p = &fp,
						.left = sizeof(fp)});
		return !copy_to_user(buf, &fp, sizeof(fp));
	}

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

static int __restore_fpregs_from_user(void __user *buf, u64 ufeatures,
				      u64 xrestore, bool fx_only)
{
	if (use_xsave()) {
		u64 init_bv = ufeatures & ~xrestore;
		int ret;

		if (likely(!fx_only))
			ret = xrstor_from_user_sigframe(buf, xrestore);
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
static bool restore_fpregs_from_user(void __user *buf, u64 xrestore, bool fx_only)
{
	struct fpu *fpu = &current->thread.fpu;
	int ret;

	/* Restore enabled features only. */
	xrestore &= fpu->fpstate->user_xfeatures;
retry:
	fpregs_lock();
	/* Ensure that XFD is up to date */
	xfd_update_state(fpu->fpstate);
	pagefault_disable();
	ret = __restore_fpregs_from_user(buf, fpu->fpstate->user_xfeatures,
					 xrestore, fx_only);
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

		if (!fault_in_readable(buf, fpu->fpstate->user_size))
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

static bool __fpu_restore_sig(void __user *buf, void __user *buf_fx,
			      bool ia32_fxstate)
{
	struct task_struct *tsk = current;
	struct fpu *fpu = &tsk->thread.fpu;
	struct user_i387_ia32_struct env;
	bool success, fx_only = false;
	union fpregs_state *fpregs;
	u64 user_xfeatures = 0;

	if (use_xsave()) {
		struct _fpx_sw_bytes fx_sw_user;

		if (!check_xstate_in_sigframe(buf_fx, &fx_sw_user))
			return false;

		fx_only = !fx_sw_user.magic1;
		user_xfeatures = fx_sw_user.xfeatures;
	} else {
		user_xfeatures = XFEATURE_MASK_FPSSE;
	}

	if (likely(!ia32_fxstate)) {
		/* Restore the FPU registers directly from user memory. */
		return restore_fpregs_from_user(buf_fx, user_xfeatures, fx_only);
	}

	/*
	 * Copy the legacy state because the FP portion of the FX frame has
	 * to be ignored for histerical raisins. The legacy state is folded
	 * in once the larger state has been copied.
	 */
	if (__copy_from_user(&env, buf, sizeof(env)))
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
		u64 mask = user_xfeatures | xfeatures_mask_supervisor();

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
	struct fpu *fpu = &current->thread.fpu;
	void __user *buf_fx = buf;
	bool ia32_fxstate = false;
	bool success = false;
	unsigned int size;

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

	if (!IS_ENABLED(CONFIG_X86_64) && !cpu_feature_enabled(X86_FEATURE_FPU)) {
		success = !fpregs_soft_set(current, NULL, 0,
					   sizeof(struct user_i387_ia32_struct),
					   NULL, buf);
	} else {
		success = __fpu_restore_sig(buf, buf_fx, ia32_fxstate);
	}

out:
	if (unlikely(!success))
		fpu__clear_user_states(fpu);
	return success;
}

unsigned long
fpu__alloc_mathframe(unsigned long sp, int ia32_frame,
		     unsigned long *buf_fx, unsigned long *size)
{
	unsigned long frame_size = xstate_sigframe_size(current->thread.fpu.fpstate);

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

