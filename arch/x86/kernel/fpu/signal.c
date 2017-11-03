// SPDX-License-Identifier: GPL-2.0
/*
 * FPU signal frame handling routines.
 */

#include <linux/compat.h>
#include <linux/cpu.h>

#include <asm/fpu/internal.h>
#include <asm/fpu/signal.h>
#include <asm/fpu/regset.h>
#include <asm/fpu/xstate.h>

#include <asm/sigframe.h>
#include <asm/trace/fpu.h>

static struct _fpx_sw_bytes fx_sw_reserved, fx_sw_reserved_ia32;

/*
 * Check for the presence of extended state information in the
 * user fpstate pointer in the sigcontext.
 */
static inline int check_for_xstate(struct fxregs_state __user *buf,
				   void __user *fpstate,
				   struct _fpx_sw_bytes *fx_sw)
{
	int min_xstate_size = sizeof(struct fxregs_state) +
			      sizeof(struct xstate_header);
	unsigned int magic2;

	if (__copy_from_user(fx_sw, &buf->sw_reserved[0], sizeof(*fx_sw)))
		return -1;

	/* Check for the first magic field and other error scenarios. */
	if (fx_sw->magic1 != FP_XSTATE_MAGIC1 ||
	    fx_sw->xstate_size < min_xstate_size ||
	    fx_sw->xstate_size > fpu_user_xstate_size ||
	    fx_sw->xstate_size > fx_sw->extended_size)
		return -1;

	/*
	 * Check for the presence of second magic word at the end of memory
	 * layout. This detects the case where the user just copied the legacy
	 * fpstate layout with out copying the extended state information
	 * in the memory layout.
	 */
	if (__get_user(magic2, (__u32 __user *)(fpstate + fx_sw->xstate_size))
	    || magic2 != FP_XSTATE_MAGIC2)
		return -1;

	return 0;
}

/*
 * Signal frame handlers.
 */
static inline int save_fsave_header(struct task_struct *tsk, void __user *buf)
{
	if (use_fxsr()) {
		struct xregs_state *xsave = &tsk->thread.fpu.state.xsave;
		struct user_i387_ia32_struct env;
		struct _fpstate_32 __user *fp = buf;

		convert_from_fxsr(&env, tsk);

		if (__copy_to_user(buf, &env, sizeof(env)) ||
		    __put_user(xsave->i387.swd, &fp->status) ||
		    __put_user(X86_FXSR_MAGIC, &fp->magic))
			return -1;
	} else {
		struct fregs_state __user *fp = buf;
		u32 swd;
		if (__get_user(swd, &fp->swd) || __put_user(swd, &fp->status))
			return -1;
	}

	return 0;
}

static inline int save_xstate_epilog(void __user *buf, int ia32_frame)
{
	struct xregs_state __user *x = buf;
	struct _fpx_sw_bytes *sw_bytes;
	u32 xfeatures;
	int err;

	/* Setup the bytes not touched by the [f]xsave and reserved for SW. */
	sw_bytes = ia32_frame ? &fx_sw_reserved_ia32 : &fx_sw_reserved;
	err = __copy_to_user(&x->i387.sw_reserved, sw_bytes, sizeof(*sw_bytes));

	if (!use_xsave())
		return err;

	err |= __put_user(FP_XSTATE_MAGIC2,
			  (__u32 *)(buf + fpu_user_xstate_size));

	/*
	 * Read the xfeatures which we copied (directly from the cpu or
	 * from the state in task struct) to the user buffers.
	 */
	err |= __get_user(xfeatures, (__u32 *)&x->header.xfeatures);

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

	err |= __put_user(xfeatures, (__u32 *)&x->header.xfeatures);

	return err;
}

static inline int copy_fpregs_to_sigframe(struct xregs_state __user *buf)
{
	int err;

	if (use_xsave())
		err = copy_xregs_to_user(buf);
	else if (use_fxsr())
		err = copy_fxregs_to_user((struct fxregs_state __user *) buf);
	else
		err = copy_fregs_to_user((struct fregs_state __user *) buf);

	if (unlikely(err) && __clear_user(buf, fpu_user_xstate_size))
		err = -EFAULT;
	return err;
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
 * If the fpu, extended register state is live, save the state directly
 * to the user frame pointed by the aligned pointer 'buf_fx'. Otherwise,
 * copy the thread's fpu state to the user frame starting at 'buf_fx'.
 *
 * If this is a 32-bit frame with fxstate, put a fsave header before
 * the aligned state at 'buf_fx'.
 *
 * For [f]xsave state, update the SW reserved fields in the [f]xsave frame
 * indicating the absence/presence of the extended state to the user.
 */
int copy_fpstate_to_sigframe(void __user *buf, void __user *buf_fx, int size)
{
	struct fpu *fpu = &current->thread.fpu;
	struct xregs_state *xsave = &fpu->state.xsave;
	struct task_struct *tsk = current;
	int ia32_fxstate = (buf != buf_fx);

	ia32_fxstate &= (IS_ENABLED(CONFIG_X86_32) ||
			 IS_ENABLED(CONFIG_IA32_EMULATION));

	if (!access_ok(VERIFY_WRITE, buf, size))
		return -EACCES;

	if (!static_cpu_has(X86_FEATURE_FPU))
		return fpregs_soft_get(current, NULL, 0,
			sizeof(struct user_i387_ia32_struct), NULL,
			(struct _fpstate_32 __user *) buf) ? -1 : 1;

	if (fpu->initialized || using_compacted_format()) {
		/* Save the live register state to the user directly. */
		if (copy_fpregs_to_sigframe(buf_fx))
			return -1;
		/* Update the thread's fxstate to save the fsave header. */
		if (ia32_fxstate)
			copy_fxregs_to_kernel(fpu);
	} else {
		/*
		 * It is a *bug* if kernel uses compacted-format for xsave
		 * area and we copy it out directly to a signal frame. It
		 * should have been handled above by saving the registers
		 * directly.
		 */
		if (boot_cpu_has(X86_FEATURE_XSAVES)) {
			WARN_ONCE(1, "x86/fpu: saving compacted-format xsave area to a signal frame!\n");
			return -1;
		}

		fpstate_sanitize_xstate(fpu);
		if (__copy_to_user(buf_fx, xsave, fpu_user_xstate_size))
			return -1;
	}

	/* Save the fsave header for the 32-bit frames. */
	if ((ia32_fxstate || !use_fxsr()) && save_fsave_header(tsk, buf))
		return -1;

	if (use_fxsr() && save_xstate_epilog(buf_fx, ia32_fxstate))
		return -1;

	return 0;
}

static inline void
sanitize_restored_xstate(struct task_struct *tsk,
			 struct user_i387_ia32_struct *ia32_env,
			 u64 xfeatures, int fx_only)
{
	struct xregs_state *xsave = &tsk->thread.fpu.state.xsave;
	struct xstate_header *header = &xsave->header;

	if (use_xsave()) {
		/*
		 * Note: we don't need to zero the reserved bits in the
		 * xstate_header here because we either didn't copy them at all,
		 * or we checked earlier that they aren't set.
		 */

		/*
		 * Init the state that is not present in the memory
		 * layout and not enabled by the OS.
		 */
		if (fx_only)
			header->xfeatures = XFEATURE_MASK_FPSSE;
		else
			header->xfeatures &= xfeatures;
	}

	if (use_fxsr()) {
		/*
		 * mscsr reserved bits must be masked to zero for security
		 * reasons.
		 */
		xsave->i387.mxcsr &= mxcsr_feature_mask;

		convert_to_fxsr(tsk, ia32_env);
	}
}

/*
 * Restore the extended state if present. Otherwise, restore the FP/SSE state.
 */
static inline int copy_user_to_fpregs_zeroing(void __user *buf, u64 xbv, int fx_only)
{
	if (use_xsave()) {
		if ((unsigned long)buf % 64 || fx_only) {
			u64 init_bv = xfeatures_mask & ~XFEATURE_MASK_FPSSE;
			copy_kernel_to_xregs(&init_fpstate.xsave, init_bv);
			return copy_user_to_fxregs(buf);
		} else {
			u64 init_bv = xfeatures_mask & ~xbv;
			if (unlikely(init_bv))
				copy_kernel_to_xregs(&init_fpstate.xsave, init_bv);
			return copy_user_to_xregs(buf, xbv);
		}
	} else if (use_fxsr()) {
		return copy_user_to_fxregs(buf);
	} else
		return copy_user_to_fregs(buf);
}

static int __fpu__restore_sig(void __user *buf, void __user *buf_fx, int size)
{
	int ia32_fxstate = (buf != buf_fx);
	struct task_struct *tsk = current;
	struct fpu *fpu = &tsk->thread.fpu;
	int state_size = fpu_kernel_xstate_size;
	u64 xfeatures = 0;
	int fx_only = 0;

	ia32_fxstate &= (IS_ENABLED(CONFIG_X86_32) ||
			 IS_ENABLED(CONFIG_IA32_EMULATION));

	if (!buf) {
		fpu__clear(fpu);
		return 0;
	}

	if (!access_ok(VERIFY_READ, buf, size))
		return -EACCES;

	fpu__initialize(fpu);

	if (!static_cpu_has(X86_FEATURE_FPU))
		return fpregs_soft_set(current, NULL,
				       0, sizeof(struct user_i387_ia32_struct),
				       NULL, buf) != 0;

	if (use_xsave()) {
		struct _fpx_sw_bytes fx_sw_user;
		if (unlikely(check_for_xstate(buf_fx, buf_fx, &fx_sw_user))) {
			/*
			 * Couldn't find the extended state information in the
			 * memory layout. Restore just the FP/SSE and init all
			 * the other extended state.
			 */
			state_size = sizeof(struct fxregs_state);
			fx_only = 1;
			trace_x86_fpu_xstate_check_failed(fpu);
		} else {
			state_size = fx_sw_user.xstate_size;
			xfeatures = fx_sw_user.xfeatures;
		}
	}

	if (ia32_fxstate) {
		/*
		 * For 32-bit frames with fxstate, copy the user state to the
		 * thread's fpu state, reconstruct fxstate from the fsave
		 * header. Validate and sanitize the copied state.
		 */
		struct fpu *fpu = &tsk->thread.fpu;
		struct user_i387_ia32_struct env;
		int err = 0;

		/*
		 * Drop the current fpu which clears fpu->initialized. This ensures
		 * that any context-switch during the copy of the new state,
		 * avoids the intermediate state from getting restored/saved.
		 * Thus avoiding the new restored state from getting corrupted.
		 * We will be ready to restore/save the state only after
		 * fpu->initialized is again set.
		 */
		fpu__drop(fpu);

		if (using_compacted_format()) {
			err = copy_user_to_xstate(&fpu->state.xsave, buf_fx);
		} else {
			err = __copy_from_user(&fpu->state.xsave, buf_fx, state_size);

			if (!err && state_size > offsetof(struct xregs_state, header))
				err = validate_xstate_header(&fpu->state.xsave.header);
		}

		if (err || __copy_from_user(&env, buf, sizeof(env))) {
			fpstate_init(&fpu->state);
			trace_x86_fpu_init_state(fpu);
			err = -1;
		} else {
			sanitize_restored_xstate(tsk, &env, xfeatures, fx_only);
		}

		fpu->initialized = 1;
		preempt_disable();
		fpu__restore(fpu);
		preempt_enable();

		return err;
	} else {
		/*
		 * For 64-bit frames and 32-bit fsave frames, restore the user
		 * state to the registers directly (with exceptions handled).
		 */
		user_fpu_begin();
		if (copy_user_to_fpregs_zeroing(buf_fx, xfeatures, fx_only)) {
			fpu__clear(fpu);
			return -1;
		}
	}

	return 0;
}

static inline int xstate_sigframe_size(void)
{
	return use_xsave() ? fpu_user_xstate_size + FP_XSTATE_MAGIC2_SIZE :
			fpu_user_xstate_size;
}

/*
 * Restore FPU state from a sigframe:
 */
int fpu__restore_sig(void __user *buf, int ia32_frame)
{
	void __user *buf_fx = buf;
	int size = xstate_sigframe_size();

	if (ia32_frame && use_fxsr()) {
		buf_fx = buf + sizeof(struct fregs_state);
		size += sizeof(struct fregs_state);
	}

	return __fpu__restore_sig(buf, buf_fx, size);
}

unsigned long
fpu__alloc_mathframe(unsigned long sp, int ia32_frame,
		     unsigned long *buf_fx, unsigned long *size)
{
	unsigned long frame_size = xstate_sigframe_size();

	*buf_fx = sp = round_down(sp - frame_size, 64);
	if (ia32_frame && use_fxsr()) {
		frame_size += sizeof(struct fregs_state);
		sp -= sizeof(struct fregs_state);
	}

	*size = frame_size;

	return sp;
}
/*
 * Prepare the SW reserved portion of the fxsave memory layout, indicating
 * the presence of the extended state information in the memory layout
 * pointed by the fpstate pointer in the sigcontext.
 * This will be saved when ever the FP and extended state context is
 * saved on the user stack during the signal handler delivery to the user.
 */
void fpu__init_prepare_fx_sw_frame(void)
{
	int size = fpu_user_xstate_size + FP_XSTATE_MAGIC2_SIZE;

	fx_sw_reserved.magic1 = FP_XSTATE_MAGIC1;
	fx_sw_reserved.extended_size = size;
	fx_sw_reserved.xfeatures = xfeatures_mask;
	fx_sw_reserved.xstate_size = fpu_user_xstate_size;

	if (IS_ENABLED(CONFIG_IA32_EMULATION) ||
	    IS_ENABLED(CONFIG_X86_32)) {
		int fsave_header_size = sizeof(struct fregs_state);

		fx_sw_reserved_ia32 = fx_sw_reserved;
		fx_sw_reserved_ia32.extended_size = size + fsave_header_size;
	}
}

