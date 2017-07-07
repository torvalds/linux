/*
 * Copyright (C) 2003 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/cache.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/compat.h>
#include <linux/bitops.h>

#include <asm/abi.h>
#include <asm/asm.h>
#include <asm/cacheflush.h>
#include <asm/compat-signal.h>
#include <asm/sim.h>
#include <linux/uaccess.h>
#include <asm/ucontext.h>
#include <asm/fpu.h>
#include <asm/cpu-features.h>
#include <asm/war.h>

#include "signal-common.h"

/*
 * Including <asm/unistd.h> would give use the 64-bit syscall numbers ...
 */
#define __NR_N32_restart_syscall	6214

extern int setup_sigcontext(struct pt_regs *, struct sigcontext __user *);
extern int restore_sigcontext(struct pt_regs *, struct sigcontext __user *);

struct ucontextn32 {
	u32		    uc_flags;
	s32		    uc_link;
	compat_stack_t      uc_stack;
	struct sigcontext   uc_mcontext;
	compat_sigset_t	    uc_sigmask;	  /* mask last for extensibility */
};

struct rt_sigframe_n32 {
	u32 rs_ass[4];			/* argument save space for o32 */
	u32 rs_pad[2];			/* Was: signal trampoline */
	struct compat_siginfo rs_info;
	struct ucontextn32 rs_uc;
};

asmlinkage void sysn32_rt_sigreturn(nabi_no_regargs struct pt_regs regs)
{
	struct rt_sigframe_n32 __user *frame;
	sigset_t set;
	int sig;

	frame = (struct rt_sigframe_n32 __user *) regs.regs[29];
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_conv_sigset_from_user(&set, &frame->rs_uc.uc_sigmask))
		goto badframe;

	set_current_blocked(&set);

	sig = restore_sigcontext(&regs, &frame->rs_uc.uc_mcontext);
	if (sig < 0)
		goto badframe;
	else if (sig)
		force_sig(sig, current);

	if (compat_restore_altstack(&frame->rs_uc.uc_stack))
		goto badframe;

	/*
	 * Don't let your children do this ...
	 */
	__asm__ __volatile__(
		"move\t$29, %0\n\t"
		"j\tsyscall_exit"
		:/* no outputs */
		:"r" (&regs));
	/* Unreached */

badframe:
	force_sig(SIGSEGV, current);
}

static int setup_rt_frame_n32(void *sig_return, struct ksignal *ksig,
			      struct pt_regs *regs, sigset_t *set)
{
	struct rt_sigframe_n32 __user *frame;
	int err = 0;

	frame = get_sigframe(ksig, regs, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		return -EFAULT;

	/* Create siginfo.  */
	err |= copy_siginfo_to_user32(&frame->rs_info, &ksig->info);

	/* Create the ucontext.	 */
	err |= __put_user(0, &frame->rs_uc.uc_flags);
	err |= __put_user(0, &frame->rs_uc.uc_link);
	err |= __compat_save_altstack(&frame->rs_uc.uc_stack, regs->regs[29]);
	err |= setup_sigcontext(regs, &frame->rs_uc.uc_mcontext);
	err |= __copy_conv_sigset_to_user(&frame->rs_uc.uc_sigmask, set);

	if (err)
		return -EFAULT;

	/*
	 * Arguments to signal handler:
	 *
	 *   a0 = signal number
	 *   a1 = 0 (should be cause)
	 *   a2 = pointer to ucontext
	 *
	 * $25 and c0_epc point to the signal handler, $29 points to
	 * the struct rt_sigframe.
	 */
	regs->regs[ 4] = ksig->sig;
	regs->regs[ 5] = (unsigned long) &frame->rs_info;
	regs->regs[ 6] = (unsigned long) &frame->rs_uc;
	regs->regs[29] = (unsigned long) frame;
	regs->regs[31] = (unsigned long) sig_return;
	regs->cp0_epc = regs->regs[25] = (unsigned long) ksig->ka.sa.sa_handler;

	DEBUGP("SIG deliver (%s:%d): sp=0x%p pc=0x%lx ra=0x%lx\n",
	       current->comm, current->pid,
	       frame, regs->cp0_epc, regs->regs[31]);

	return 0;
}

struct mips_abi mips_abi_n32 = {
	.setup_rt_frame = setup_rt_frame_n32,
	.restart	= __NR_N32_restart_syscall,

	.off_sc_fpregs = offsetof(struct sigcontext, sc_fpregs),
	.off_sc_fpc_csr = offsetof(struct sigcontext, sc_fpc_csr),
	.off_sc_used_math = offsetof(struct sigcontext, sc_used_math),

	.vdso		= &vdso_image_n32,
};
