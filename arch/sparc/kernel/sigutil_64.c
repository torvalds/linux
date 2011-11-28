#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/thread_info.h>
#include <linux/uaccess.h>
#include <linux/errno.h>

#include <asm/sigcontext.h>
#include <asm/fpumacro.h>
#include <asm/ptrace.h>

#include "sigutil.h"

int save_fpu_state(struct pt_regs *regs, __siginfo_fpu_t __user *fpu)
{
	unsigned long *fpregs = current_thread_info()->fpregs;
	unsigned long fprs;
	int err = 0;
	
	fprs = current_thread_info()->fpsaved[0];
	if (fprs & FPRS_DL)
		err |= copy_to_user(&fpu->si_float_regs[0], fpregs,
				    (sizeof(unsigned int) * 32));
	if (fprs & FPRS_DU)
		err |= copy_to_user(&fpu->si_float_regs[32], fpregs+16,
				    (sizeof(unsigned int) * 32));
	err |= __put_user(current_thread_info()->xfsr[0], &fpu->si_fsr);
	err |= __put_user(current_thread_info()->gsr[0], &fpu->si_gsr);
	err |= __put_user(fprs, &fpu->si_fprs);

	return err;
}

int restore_fpu_state(struct pt_regs *regs, __siginfo_fpu_t __user *fpu)
{
	unsigned long *fpregs = current_thread_info()->fpregs;
	unsigned long fprs;
	int err;

	err = __get_user(fprs, &fpu->si_fprs);
	fprs_write(0);
	regs->tstate &= ~TSTATE_PEF;
	if (fprs & FPRS_DL)
		err |= copy_from_user(fpregs, &fpu->si_float_regs[0],
		       	       (sizeof(unsigned int) * 32));
	if (fprs & FPRS_DU)
		err |= copy_from_user(fpregs+16, &fpu->si_float_regs[32],
		       	       (sizeof(unsigned int) * 32));
	err |= __get_user(current_thread_info()->xfsr[0], &fpu->si_fsr);
	err |= __get_user(current_thread_info()->gsr[0], &fpu->si_gsr);
	current_thread_info()->fpsaved[0] |= fprs;
	return err;
}

int save_rwin_state(int wsaved, __siginfo_rwin_t __user *rwin)
{
	int i, err = __put_user(wsaved, &rwin->wsaved);

	for (i = 0; i < wsaved; i++) {
		struct reg_window *rp = &current_thread_info()->reg_window[i];
		unsigned long fp = current_thread_info()->rwbuf_stkptrs[i];

		err |= copy_to_user(&rwin->reg_window[i], rp,
				    sizeof(struct reg_window));
		err |= __put_user(fp, &rwin->rwbuf_stkptrs[i]);
	}
	return err;
}

int restore_rwin_state(__siginfo_rwin_t __user *rp)
{
	struct thread_info *t = current_thread_info();
	int i, wsaved, err;

	__get_user(wsaved, &rp->wsaved);
	if (wsaved > NSWINS)
		return -EFAULT;

	err = 0;
	for (i = 0; i < wsaved; i++) {
		err |= copy_from_user(&t->reg_window[i],
				      &rp->reg_window[i],
				      sizeof(struct reg_window));
		err |= __get_user(t->rwbuf_stkptrs[i],
				  &rp->rwbuf_stkptrs[i]);
	}
	if (err)
		return err;

	set_thread_wsaved(wsaved);
	synchronize_user_stack();
	if (get_thread_wsaved())
		return -EFAULT;
	return 0;
}
