// SPDX-License-Identifier: GPL-2.0
/*
 * FPU signal frame handling routines.
 */

#include <linux/compat.h>
#include <linux/cpu.h>
#include <linux/pagemap.h>

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

		fpregs_lock();
		if (!test_thread_flag(TIF_NEED_FPU_LOAD))
			copy_fxregs_to_kernel(&tsk->thread.fpu);
		fpregs_unlock();

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
			  (__u32 __user *)(buf + fpu_user_xstate_size));

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
 * Try to save it directly to the user frame with disabled page fault handler.
 * If this fails then do the slow path where the FPU state is first saved to
 * task's fpu->state and then copy it to the user frame pointed to by the
 * aligned pointer 'buf_fx'.
 *
 * If this is a 32-bit frame with fxstate, put a fsave header before
 * the aligned state at 'buf_fx'.
 *
 * For [f]xsave state, update the SW reserved fields in the [f]xsave frame
 * indicating the absence/presence of the extended state to the user.
 */
int copy_fpstate_to_sigframe(void __user *buf, void __user *buf_fx, int size)
{
	struct task_struct *tsk = current;
	int ia32_fxstate = (buf != buf_fx);
	int ret;

	ia32_fxstate &= (IS_ENABLED(CONFIG_X86_32) ||
			 IS_ENABLED(CONFIG_IA32_EMULATION));

	if (!static_cpu_has(X86_FEATURE_FPU)) {
		struct user_i387_ia32_struct fp;
		fpregs_soft_get(current, NULL, (struct membuf){.p = &fp,
						.left = sizeof(fp)});
		return copy_to_user(buf, &fp, sizeof(fp)) ? -EFAULT : 0;
	}

	if (!access_ok(buf, size))
		return -EACCES;
retry:
	/*
	 * Load the FPU registers if they are not valid for the current task.
	 * With a valid FPU state we can attempt to save the state directly to
	 * userland's stack frame which will likely succeed. If it does not,
	 * resolve the fault in the user memory and try again.
	 */
	fpregs_lock();
	if (test_thread_flag(TIF_NEED_FPU_LOAD))
		__fpregs_load_activate();

	pagefault_disable();
	ret = copy_fpregs_to_sigframe(buf_fx);
	pagefault_enable();
	fpregs_unlock();

	if (ret) {
		if (!fault_in_pages_writeable(buf_fx, fpu_user_xstate_size))
			goto retry;
		return -EFAULT;
	}

	/* Save the fsave header for the 32-bit frames. */
	if ((ia32_fxstate || !use_fxsr()) && save_fsave_header(tsk, buf))
		return -1;

	if (use_fxsr() && save_xstate_epilog(buf_fx, ia32_fxstate))
		return -1;

	return 0;
}

static inline void
sanitize_restored_user_xstate(union fpregs_state *state,
			      struct user_i387_ia32_struct *ia32_env,
			      u64 user_xfeatures, int fx_only)
{
	struct xregs_state *xsave = &state->xsave;
	struct xstate_header *header = &xsave->header;

	if (use_xsave()) {
		/*
		 * Note: we don't need to zero the reserved bits in the
		 * xstate_header here because we either didn't copy them at all,
		 * or we checked earlier that they aren't set.
		 */

		/*
		 * 'user_xfeatures' might have bits clear which are
		 * set in header->xfeatures. This represents features that
		 * were in init state prior to a signal delivery, and need
		 * to be reset back to the init state.  Clear any user
		 * feature bits which are set in the kernel buffer to get
		 * them back to the init state.
		 *
		 * Supervisor state is unchanged by input from userspace.
		 * Ensure supervisor state bits stay set and supervisor
		 * state is not modified.
		 */
		if (fx_only)
			header->xfeatures = XFEATURE_MASK_FPSSE;
		else
			header->xfeatures &= user_xfeatures |
					     xfeatures_mask_supervisor();
	}

	if (use_fxsr()) {
		/*
		 * mscsr reserved bits must be masked to zero for security
		 * reasons.
		 */
		xsave->i387.mxcsr &= mxcsr_feature_mask;

		if (ia32_env)
			convert_to_fxsr(&state->fxsave, ia32_env);
	}
}

/*
 * Restore the extended state if present. Otherwise, restore the FP/SSE state.
 */
static int copy_user_to_fpregs_zeroing(void __user *buf, u64 xbv, int fx_only)
{
	u64 init_bv;
	int r;

	if (use_xsave()) {
		if (fx_only) {
			init_bv = xfeatures_mask_user() & ~XFEATURE_MASK_FPSSE;

			r = copy_user_to_fxregs(buf);
			if (!r)
				copy_kernel_to_xregs(&init_fpstate.xsave, init_bv);
			return r;
		} else {
			init_bv = xfeatures_mask_user() & ~xbv;

			r = copy_user_to_xregs(buf, xbv);
			if (!r && unlikely(init_bv))
				copy_kernel_to_xregs(&init_fpstate.xsave, init_bv);
			return r;
		}
	} else if (use_fxsr()) {
		return copy_user_to_fxregs(buf);
	} else
		return copy_user_to_fregs(buf);
}

static int __fpu__restore_sig(void __user *buf, void __user *buf_fx, int size)
{
	struct user_i387_ia32_struct *envp = NULL;
	int state_size = fpu_kernel_xstate_size;
	int ia32_fxstate = (buf != buf_fx);
	struct task_struct *tsk = current;
	struct fpu *fpu = &tsk->thread.fpu;
	struct user_i387_ia32_struct env;
	u64 user_xfeatures = 0;
	int fx_only = 0;
	int ret = 0;

	ia32_fxstate &= (IS_ENABLED(CONFIG_X86_32) ||
			 IS_ENABLED(CONFIG_IA32_EMULATION));

	if (!buf) {
		fpu__clear_user_states(fpu);
		return 0;
	}

	if (!access_ok(buf, size)) {
		ret = -EACCES;
		goto out;
	}

	if (!static_cpu_has(X86_FEATURE_FPU)) {
		ret = fpregs_soft_set(current, NULL, 0,
				      sizeof(struct user_i387_ia32_struct),
				      NULL, buf);
		goto out;
	}

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
			user_xfeatures = fx_sw_user.xfeatures;
		}
	}

	if ((unsigned long)buf_fx % 64)
		fx_only = 1;

	if (!ia32_fxstate) {
		/*
		 * Attempt to restore the FPU registers directly from user
		 * memory. For that to succeed, the user access cannot cause
		 * page faults. If it does, fall back to the slow path below,
		 * going through the kernel buffer with the enabled pagefault
		 * handler.
		 */
		fpregs_lock();
		pagefault_disable();
		ret = copy_user_to_fpregs_zeroing(buf_fx, user_xfeatures, fx_only);
		pagefault_enable();
		if (!ret) {

			/*
			 * Restore supervisor states: previous context switch
			 * etc has done XSAVES and saved the supervisor states
			 * in the kernel buffer from which they can be restored
			 * now.
			 *
			 * We cannot do a single XRSTORS here - which would
			 * be nice - because the rest of the FPU registers are
			 * being restored from a user buffer directly. The
			 * single XRSTORS happens below, when the user buffer
			 * has been copied to the kernel one.
			 */
			if (test_thread_flag(TIF_NEED_FPU_LOAD) &&
			    xfeatures_mask_supervisor())
				copy_kernel_to_xregs(&fpu->state.xsave,
						     xfeatures_mask_supervisor());
			fpregs_mark_activate();
			fpregs_unlock();
			return 0;
		}

		/*
		 * The above did an FPU restore operation, restricted to
		 * the user portion of the registers, and failed, but the
		 * microcode might have modified the FPU registers
		 * nevertheless.
		 *
		 * If the FPU registers do not belong to current, then
		 * invalidate the FPU register state otherwise the task might
		 * preempt current and return to user space with corrupted
		 * FPU registers.
		 *
		 * In case current owns the FPU registers then no further
		 * action is required. The fixup below will handle it
		 * correctly.
		 */
		if (test_thread_flag(TIF_NEED_FPU_LOAD))
			__cpu_invalidate_fpregs_state();

		fpregs_unlock();
	} else {
		/*
		 * For 32-bit frames with fxstate, copy the fxstate so it can
		 * be reconstructed later.
		 */
		ret = __copy_from_user(&env, buf, sizeof(env));
		if (ret)
			goto out;
		envp = &env;
	}

	/*
	 * By setting TIF_NEED_FPU_LOAD it is ensured that our xstate is
	 * not modified on context switch and that the xstate is considered
	 * to be loaded again on return to userland (overriding last_cpu avoids
	 * the optimisation).
	 */
	fpregs_lock();

	if (!test_thread_flag(TIF_NEED_FPU_LOAD)) {

		/*
		 * Supervisor states are not modified by user space input.  Save
		 * current supervisor states first and invalidate the FPU regs.
		 */
		if (xfeatures_mask_supervisor())
			copy_supervisor_to_kernel(&fpu->state.xsave);
		set_thread_flag(TIF_NEED_FPU_LOAD);
	}
	__fpu_invalidate_fpregs_state(fpu);
	fpregs_unlock();

	if (use_xsave() && !fx_only) {
		u64 init_bv = xfeatures_mask_user() & ~user_xfeatures;

		ret = copy_user_to_xstate(&fpu->state.xsave, buf_fx);
		if (ret)
			goto out;

		sanitize_restored_user_xstate(&fpu->state, envp, user_xfeatures,
					      fx_only);

		fpregs_lock();
		if (unlikely(init_bv))
			copy_kernel_to_xregs(&init_fpstate.xsave, init_bv);

		/*
		 * Restore previously saved supervisor xstates along with
		 * copied-in user xstates.
		 */
		ret = copy_kernel_to_xregs_err(&fpu->state.xsave,
					       user_xfeatures | xfeatures_mask_supervisor());

	} else if (use_fxsr()) {
		ret = __copy_from_user(&fpu->state.fxsave, buf_fx, state_size);
		if (ret) {
			ret = -EFAULT;
			goto out;
		}

		sanitize_restored_user_xstate(&fpu->state, envp, user_xfeatures,
					      fx_only);

		fpregs_lock();
		if (use_xsave()) {
			u64 init_bv;

			init_bv = xfeatures_mask_user() & ~XFEATURE_MASK_FPSSE;
			copy_kernel_to_xregs(&init_fpstate.xsave, init_bv);
		}

		ret = copy_kernel_to_fxregs_err(&fpu->state.fxsave);
	} else {
		ret = __copy_from_user(&fpu->state.fsave, buf_fx, state_size);
		if (ret)
			goto out;

		fpregs_lock();
		ret = copy_kernel_to_fregs_err(&fpu->state.fsave);
	}
	if (!ret)
		fpregs_mark_activate();
	else
		fpregs_deactivate(fpu);
	fpregs_unlock();

out:
	if (ret)
		fpu__clear_user_states(fpu);
	return ret;
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
	fx_sw_reserved.xfeatures = xfeatures_mask_user();
	fx_sw_reserved.xstate_size = fpu_user_xstate_size;

	if (IS_ENABLED(CONFIG_IA32_EMULATION) ||
	    IS_ENABLED(CONFIG_X86_32)) {
		int fsave_header_size = sizeof(struct fregs_state);

		fx_sw_reserved_ia32 = fx_sw_reserved;
		fx_sw_reserved_ia32.extended_size = size + fsave_header_size;
	}
}

