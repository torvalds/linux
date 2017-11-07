// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/thread_info.h>
#include <linux/uaccess.h>
#include <linux/sched.h>

#include <asm/sigcontext.h>
#include <asm/fpumacro.h>
#include <asm/ptrace.h>
#include <asm/switch_to.h>

#include "sigutil.h"

int save_fpu_state(struct pt_regs *regs, __siginfo_fpu_t __user *fpu)
{
	int err = 0;
#ifdef CONFIG_SMP
	if (test_tsk_thread_flag(current, TIF_USEDFPU)) {
		put_psr(get_psr() | PSR_EF);
		fpsave(&current->thread.float_regs[0], &current->thread.fsr,
		       &current->thread.fpqueue[0], &current->thread.fpqdepth);
		regs->psr &= ~(PSR_EF);
		clear_tsk_thread_flag(current, TIF_USEDFPU);
	}
#else
	if (current == last_task_used_math) {
		put_psr(get_psr() | PSR_EF);
		fpsave(&current->thread.float_regs[0], &current->thread.fsr,
		       &current->thread.fpqueue[0], &current->thread.fpqdepth);
		last_task_used_math = NULL;
		regs->psr &= ~(PSR_EF);
	}
#endif
	err |= __copy_to_user(&fpu->si_float_regs[0],
			      &current->thread.float_regs[0],
			      (sizeof(unsigned long) * 32));
	err |= __put_user(current->thread.fsr, &fpu->si_fsr);
	err |= __put_user(current->thread.fpqdepth, &fpu->si_fpqdepth);
	if (current->thread.fpqdepth != 0)
		err |= __copy_to_user(&fpu->si_fpqueue[0],
				      &current->thread.fpqueue[0],
				      ((sizeof(unsigned long) +
				      (sizeof(unsigned long *)))*16));
	clear_used_math();
	return err;
}

int restore_fpu_state(struct pt_regs *regs, __siginfo_fpu_t __user *fpu)
{
	int err;

	if (((unsigned long) fpu) & 3)
		return -EFAULT;

#ifdef CONFIG_SMP
	if (test_tsk_thread_flag(current, TIF_USEDFPU))
		regs->psr &= ~PSR_EF;
#else
	if (current == last_task_used_math) {
		last_task_used_math = NULL;
		regs->psr &= ~PSR_EF;
	}
#endif
	set_used_math();
	clear_tsk_thread_flag(current, TIF_USEDFPU);

	if (!access_ok(VERIFY_READ, fpu, sizeof(*fpu)))
		return -EFAULT;

	err = __copy_from_user(&current->thread.float_regs[0], &fpu->si_float_regs[0],
			       (sizeof(unsigned long) * 32));
	err |= __get_user(current->thread.fsr, &fpu->si_fsr);
	err |= __get_user(current->thread.fpqdepth, &fpu->si_fpqdepth);
	if (current->thread.fpqdepth != 0)
		err |= __copy_from_user(&current->thread.fpqueue[0],
					&fpu->si_fpqueue[0],
					((sizeof(unsigned long) +
					(sizeof(unsigned long *)))*16));
	return err;
}

int save_rwin_state(int wsaved, __siginfo_rwin_t __user *rwin)
{
	int i, err = __put_user(wsaved, &rwin->wsaved);

	for (i = 0; i < wsaved; i++) {
		struct reg_window32 *rp;
		unsigned long fp;

		rp = &current_thread_info()->reg_window[i];
		fp = current_thread_info()->rwbuf_stkptrs[i];
		err |= copy_to_user(&rwin->reg_window[i], rp,
				    sizeof(struct reg_window32));
		err |= __put_user(fp, &rwin->rwbuf_stkptrs[i]);
	}
	return err;
}

int restore_rwin_state(__siginfo_rwin_t __user *rp)
{
	struct thread_info *t = current_thread_info();
	int i, wsaved, err;

	if (((unsigned long) rp) & 3)
		return -EFAULT;

	get_user(wsaved, &rp->wsaved);
	if (wsaved > NSWINS)
		return -EFAULT;

	err = 0;
	for (i = 0; i < wsaved; i++) {
		err |= copy_from_user(&t->reg_window[i],
				      &rp->reg_window[i],
				      sizeof(struct reg_window32));
		err |= __get_user(t->rwbuf_stkptrs[i],
				  &rp->rwbuf_stkptrs[i]);
	}
	if (err)
		return err;

	t->w_saved = wsaved;
	synchronize_user_stack();
	if (t->w_saved)
		return -EFAULT;
	return 0;

}
