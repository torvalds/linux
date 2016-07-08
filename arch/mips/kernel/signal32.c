/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 1994 - 2000, 2006  Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/cache.h>
#include <linux/compat.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/syscalls.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/suspend.h>
#include <linux/compiler.h>
#include <linux/uaccess.h>

#include <asm/abi.h>
#include <asm/asm.h>
#include <asm/compat-signal.h>
#include <linux/bitops.h>
#include <asm/cacheflush.h>
#include <asm/sim.h>
#include <asm/ucontext.h>
#include <asm/fpu.h>
#include <asm/war.h>
#include <asm/dsp.h>

#include "signal-common.h"

/*
 * Including <asm/unistd.h> would give use the 64-bit syscall numbers ...
 */
#define __NR_O32_restart_syscall	4253

/* 32-bit compatibility types */

typedef unsigned int __sighandler32_t;
typedef void (*vfptr_t)(void);

struct ucontext32 {
	u32		    uc_flags;
	s32		    uc_link;
	compat_stack_t      uc_stack;
	struct sigcontext32 uc_mcontext;
	compat_sigset_t	    uc_sigmask;	  /* mask last for extensibility */
};

struct sigframe32 {
	u32 sf_ass[4];		/* argument save space for o32 */
	u32 sf_pad[2];		/* Was: signal trampoline */
	struct sigcontext32 sf_sc;
	compat_sigset_t sf_mask;
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

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */

asmlinkage int sys32_sigsuspend(compat_sigset_t __user *uset)
{
	return compat_sys_rt_sigsuspend(uset, sizeof(compat_sigset_t));
}

SYSCALL_DEFINE3(32_sigaction, long, sig, const struct compat_sigaction __user *, act,
	struct compat_sigaction __user *, oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;
	int err = 0;

	if (act) {
		old_sigset_t mask;
		s32 handler;

		if (!access_ok(VERIFY_READ, act, sizeof(*act)))
			return -EFAULT;
		err |= __get_user(handler, &act->sa_handler);
		new_ka.sa.sa_handler = (void __user *)(s64)handler;
		err |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		err |= __get_user(mask, &act->sa_mask.sig[0]);
		if (err)
			return -EFAULT;

		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)))
			return -EFAULT;
		err |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		err |= __put_user((u32)(u64)old_ka.sa.sa_handler,
				  &oact->sa_handler);
		err |= __put_user(old_ka.sa.sa_mask.sig[0], oact->sa_mask.sig);
		err |= __put_user(0, &oact->sa_mask.sig[1]);
		err |= __put_user(0, &oact->sa_mask.sig[2]);
		err |= __put_user(0, &oact->sa_mask.sig[3]);
		if (err)
			return -EFAULT;
	}

	return ret;
}

int copy_siginfo_to_user32(compat_siginfo_t __user *to, const siginfo_t *from)
{
	int err;

	if (!access_ok (VERIFY_WRITE, to, sizeof(compat_siginfo_t)))
		return -EFAULT;

	/* If you change siginfo_t structure, please be sure
	   this code is fixed accordingly.
	   It should never copy any pad contained in the structure
	   to avoid security leaks, but must copy the generic
	   3 ints plus the relevant union member.
	   This routine must convert siginfo from 64bit to 32bit as well
	   at the same time.  */
	err = __put_user(from->si_signo, &to->si_signo);
	err |= __put_user(from->si_errno, &to->si_errno);
	err |= __put_user((short)from->si_code, &to->si_code);
	if (from->si_code < 0)
		err |= __copy_to_user(&to->_sifields._pad, &from->_sifields._pad, SI_PAD_SIZE);
	else {
		switch (from->si_code >> 16) {
		case __SI_TIMER >> 16:
			err |= __put_user(from->si_tid, &to->si_tid);
			err |= __put_user(from->si_overrun, &to->si_overrun);
			err |= __put_user(from->si_int, &to->si_int);
			break;
		case __SI_CHLD >> 16:
			err |= __put_user(from->si_utime, &to->si_utime);
			err |= __put_user(from->si_stime, &to->si_stime);
			err |= __put_user(from->si_status, &to->si_status);
		default:
			err |= __put_user(from->si_pid, &to->si_pid);
			err |= __put_user(from->si_uid, &to->si_uid);
			break;
		case __SI_FAULT >> 16:
			err |= __put_user((unsigned long)from->si_addr, &to->si_addr);
			break;
		case __SI_POLL >> 16:
			err |= __put_user(from->si_band, &to->si_band);
			err |= __put_user(from->si_fd, &to->si_fd);
			break;
		case __SI_RT >> 16: /* This is not generated by the kernel as of now.  */
		case __SI_MESGQ >> 16:
			err |= __put_user(from->si_pid, &to->si_pid);
			err |= __put_user(from->si_uid, &to->si_uid);
			err |= __put_user(from->si_int, &to->si_int);
			break;
		case __SI_SYS >> 16:
			err |= __copy_to_user(&to->si_call_addr, &from->si_call_addr,
					      sizeof(compat_uptr_t));
			err |= __put_user(from->si_syscall, &to->si_syscall);
			err |= __put_user(from->si_arch, &to->si_arch);
			break;
		}
	}
	return err;
}

int copy_siginfo_from_user32(siginfo_t *to, compat_siginfo_t __user *from)
{
	if (copy_from_user(to, from, 3*sizeof(int)) ||
	    copy_from_user(to->_sifields._pad,
			   from->_sifields._pad, SI_PAD_SIZE32))
		return -EFAULT;

	return 0;
}

asmlinkage void sys32_sigreturn(nabi_no_regargs struct pt_regs regs)
{
	struct sigframe32 __user *frame;
	sigset_t blocked;
	int sig;

	frame = (struct sigframe32 __user *) regs.regs[29];
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_conv_sigset_from_user(&blocked, &frame->sf_mask))
		goto badframe;

	set_current_blocked(&blocked);

	sig = restore_sigcontext32(&regs, &frame->sf_sc);
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
		:/* no outputs */
		:"r" (&regs));
	/* Unreached */

badframe:
	force_sig(SIGSEGV, current);
}

asmlinkage void sys32_rt_sigreturn(nabi_no_regargs struct pt_regs regs)
{
	struct rt_sigframe32 __user *frame;
	sigset_t set;
	int sig;

	frame = (struct rt_sigframe32 __user *) regs.regs[29];
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_conv_sigset_from_user(&set, &frame->rs_uc.uc_sigmask))
		goto badframe;

	set_current_blocked(&set);

	sig = restore_sigcontext32(&regs, &frame->rs_uc.uc_mcontext);
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
