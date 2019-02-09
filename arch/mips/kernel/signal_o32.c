/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 1994 - 2000, 2006  Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2016, Imagination Technologies Ltd.
 */
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>

#include <asm/abi.h>
#include <asm/compat-signal.h>
#include <asm/dsp.h>
#include <asm/sim.h>
#include <asm/unistd.h>

#include "signal-common.h"

/*
 * Including <asm/unistd.h> would give use the 64-bit syscall numbers ...
 */
#define __NR_O32_restart_syscall	4253

struct sigframe32 {
	u32 sf_ass[4];		/* argument save space for o32 */
	u32 sf_pad[2];		/* Was: signal trampoline */
	struct sigcontext32 sf_sc;
	compat_sigset_t sf_mask;
};

struct ucontext32 {
	u32		    uc_flags;
	s32		    uc_link;
	compat_stack_t      uc_stack;
	struct sigcontext32 uc_mcontext;
	compat_sigset_t	    uc_sigmask;	  /* mask last for extensibility */
};

struct rt_sigframe32 {
	u32 rs_ass[4];			/* argument save space for o32 */
	u32 rs_pad[2];			/* Was: signal trampoline */
	compat_siginfo_t rs_info;
	struct ucontext32 rs_uc;
};

static int setup_sigcontext32(struct pt_regs *regs,
			      struct sigcontext32 __user *sc)
{
	int err = 0;
	int i;

	err |= __put_user(regs->cp0_epc, &sc->sc_pc);

	err |= __put_user(0, &sc->sc_regs[0]);
	for (i = 1; i < 32; i++)
		err |= __put_user(regs->regs[i], &sc->sc_regs[i]);

	err |= __put_user(regs->hi, &sc->sc_mdhi);
	err |= __put_user(regs->lo, &sc->sc_mdlo);
	if (cpu_has_dsp) {
		err |= __put_user(rddsp(DSP_MASK), &sc->sc_dsp);
		err |= __put_user(mfhi1(), &sc->sc_hi1);
		err |= __put_user(mflo1(), &sc->sc_lo1);
		err |= __put_user(mfhi2(), &sc->sc_hi2);
		err |= __put_user(mflo2(), &sc->sc_lo2);
		err |= __put_user(mfhi3(), &sc->sc_hi3);
		err |= __put_user(mflo3(), &sc->sc_lo3);
	}

	/*
	 * Save FPU state to signal context.  Signal handler
	 * will "inherit" current FPU state.
	 */
	err |= protected_save_fp_context(sc);

	return err;
}

static int restore_sigcontext32(struct pt_regs *regs,
				struct sigcontext32 __user *sc)
{
	int err = 0;
	s32 treg;
	int i;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	err |= __get_user(regs->cp0_epc, &sc->sc_pc);
	err |= __get_user(regs->hi, &sc->sc_mdhi);
	err |= __get_user(regs->lo, &sc->sc_mdlo);
	if (cpu_has_dsp) {
		err |= __get_user(treg, &sc->sc_hi1); mthi1(treg);
		err |= __get_user(treg, &sc->sc_lo1); mtlo1(treg);
		err |= __get_user(treg, &sc->sc_hi2); mthi2(treg);
		err |= __get_user(treg, &sc->sc_lo2); mtlo2(treg);
		err |= __get_user(treg, &sc->sc_hi3); mthi3(treg);
		err |= __get_user(treg, &sc->sc_lo3); mtlo3(treg);
		err |= __get_user(treg, &sc->sc_dsp); wrdsp(treg, DSP_MASK);
	}

	for (i = 1; i < 32; i++)
		err |= __get_user(regs->regs[i], &sc->sc_regs[i]);

	return err ?: protected_restore_fp_context(sc);
}

static int setup_frame_32(void *sig_return, struct ksignal *ksig,
			  struct pt_regs *regs, sigset_t *set)
{
	struct sigframe32 __user *frame;
	int err = 0;

	frame = get_sigframe(ksig, regs, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		return -EFAULT;

	err |= setup_sigcontext32(regs, &frame->sf_sc);
	err |= __copy_conv_sigset_to_user(&frame->sf_mask, set);

	if (err)
		return -EFAULT;

	/*
	 * Arguments to signal handler:
	 *
	 *   a0 = signal number
	 *   a1 = 0 (should be cause)
	 *   a2 = pointer to struct sigcontext
	 *
	 * $25 and c0_epc point to the signal handler, $29 points to the
	 * struct sigframe.
	 */
	regs->regs[ 4] = ksig->sig;
	regs->regs[ 5] = 0;
	regs->regs[ 6] = (unsigned long) &frame->sf_sc;
	regs->regs[29] = (unsigned long) frame;
	regs->regs[31] = (unsigned long) sig_return;
	regs->cp0_epc = regs->regs[25] = (unsigned long) ksig->ka.sa.sa_handler;

	DEBUGP("SIG deliver (%s:%d): sp=0x%p pc=0x%lx ra=0x%lx\n",
	       current->comm, current->pid,
	       frame, regs->cp0_epc, regs->regs[31]);

	return 0;
}

asmlinkage void sys32_rt_sigreturn(void)
{
	struct rt_sigframe32 __user *frame;
	struct pt_regs *regs;
	sigset_t set;
	int sig;

	regs = current_pt_regs();
	frame = (struct rt_sigframe32 __user *)regs->regs[29];
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_conv_sigset_from_user(&set, &frame->rs_uc.uc_sigmask))
		goto badframe;

	set_current_blocked(&set);

	sig = restore_sigcontext32(regs, &frame->rs_uc.uc_mcontext);
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
		: /* no outputs */
		: "r" (regs));
	/* Unreached */

badframe:
	force_sig(SIGSEGV, current);
}

static int setup_rt_frame_32(void *sig_return, struct ksignal *ksig,
			     struct pt_regs *regs, sigset_t *set)
{
	struct rt_sigframe32 __user *frame;
	int err = 0;

	frame = get_sigframe(ksig, regs, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		return -EFAULT;

	/* Convert (siginfo_t -> compat_siginfo_t) and copy to user. */
	err |= copy_siginfo_to_user32(&frame->rs_info, &ksig->info);

	/* Create the ucontext.	 */
	err |= __put_user(0, &frame->rs_uc.uc_flags);
	err |= __put_user(0, &frame->rs_uc.uc_link);
	err |= __compat_save_altstack(&frame->rs_uc.uc_stack, regs->regs[29]);
	err |= setup_sigcontext32(regs, &frame->rs_uc.uc_mcontext);
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
	 * the struct rt_sigframe32.
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

/*
 * o32 compatibility on 64-bit kernels, without DSP ASE
 */
struct mips_abi mips_abi_32 = {
	.setup_frame	= setup_frame_32,
	.setup_rt_frame = setup_rt_frame_32,
	.restart	= __NR_O32_restart_syscall,

	.off_sc_fpregs = offsetof(struct sigcontext32, sc_fpregs),
	.off_sc_fpc_csr = offsetof(struct sigcontext32, sc_fpc_csr),
	.off_sc_used_math = offsetof(struct sigcontext32, sc_used_math),

	.vdso		= &vdso_image_o32,
};


asmlinkage void sys32_sigreturn(void)
{
	struct sigframe32 __user *frame;
	struct pt_regs *regs;
	sigset_t blocked;
	int sig;

	regs = current_pt_regs();
	frame = (struct sigframe32 __user *)regs->regs[29];
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_conv_sigset_from_user(&blocked, &frame->sf_mask))
		goto badframe;

	set_current_blocked(&blocked);

	sig = restore_sigcontext32(regs, &frame->sf_sc);
	if (sig < 0)
		goto badframe;
	else if (sig)
		force_sig(sig, current);

	/*
	 * Don't let your children do this ...
	 */
	__asm__ __volatile__(
		"move\t$29, %0\n\t"
		"j\tsyscall_exit"
		: /* no outputs */
		: "r" (regs));
	/* Unreached */

badframe:
	force_sig(SIGSEGV, current);
}
