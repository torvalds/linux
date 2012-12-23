/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 1994 - 2000  Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/cache.h>
#include <linux/irqflags.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/personality.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/compiler.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/tracehook.h>

#include <asm/abi.h>
#include <asm/asm.h>
#include <linux/bitops.h>
#include <asm/cacheflush.h>
#include <asm/fpu.h>
#include <asm/sim.h>
#include <asm/ucontext.h>
#include <asm/cpu-features.h>
#include <asm/war.h>
#include <asm/vdso.h>
#include <asm/dsp.h>

#include "signal-common.h"

static int (*save_fp_context)(struct sigcontext __user *sc);
static int (*restore_fp_context)(struct sigcontext __user *sc);

extern asmlinkage int _save_fp_context(struct sigcontext __user *sc);
extern asmlinkage int _restore_fp_context(struct sigcontext __user *sc);

extern asmlinkage int fpu_emulator_save_context(struct sigcontext __user *sc);
extern asmlinkage int fpu_emulator_restore_context(struct sigcontext __user *sc);

struct sigframe {
	u32 sf_ass[4];		/* argument save space for o32 */
	u32 sf_pad[2];		/* Was: signal trampoline */
	struct sigcontext sf_sc;
	sigset_t sf_mask;
};

struct rt_sigframe {
	u32 rs_ass[4];		/* argument save space for o32 */
	u32 rs_pad[2];		/* Was: signal trampoline */
	struct siginfo rs_info;
	struct ucontext rs_uc;
};

/*
 * Helper routines
 */
static int protected_save_fp_context(struct sigcontext __user *sc)
{
	int err;
	while (1) {
		lock_fpu_owner();
		own_fpu_inatomic(1);
		err = save_fp_context(sc); /* this might fail */
		unlock_fpu_owner();
		if (likely(!err))
			break;
		/* touch the sigcontext and try again */
		err = __put_user(0, &sc->sc_fpregs[0]) |
			__put_user(0, &sc->sc_fpregs[31]) |
			__put_user(0, &sc->sc_fpc_csr);
		if (err)
			break;	/* really bad sigcontext */
	}
	return err;
}

static int protected_restore_fp_context(struct sigcontext __user *sc)
{
	int err, tmp __maybe_unused;
	while (1) {
		lock_fpu_owner();
		own_fpu_inatomic(0);
		err = restore_fp_context(sc); /* this might fail */
		unlock_fpu_owner();
		if (likely(!err))
			break;
		/* touch the sigcontext and try again */
		err = __get_user(tmp, &sc->sc_fpregs[0]) |
			__get_user(tmp, &sc->sc_fpregs[31]) |
			__get_user(tmp, &sc->sc_fpc_csr);
		if (err)
			break;	/* really bad sigcontext */
	}
	return err;
}

int setup_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc)
{
	int err = 0;
	int i;
	unsigned int used_math;

	err |= __put_user(regs->cp0_epc, &sc->sc_pc);

	err |= __put_user(0, &sc->sc_regs[0]);
	for (i = 1; i < 32; i++)
		err |= __put_user(regs->regs[i], &sc->sc_regs[i]);

#ifdef CONFIG_CPU_HAS_SMARTMIPS
	err |= __put_user(regs->acx, &sc->sc_acx);
#endif
	err |= __put_user(regs->hi, &sc->sc_mdhi);
	err |= __put_user(regs->lo, &sc->sc_mdlo);
	if (cpu_has_dsp) {
		err |= __put_user(mfhi1(), &sc->sc_hi1);
		err |= __put_user(mflo1(), &sc->sc_lo1);
		err |= __put_user(mfhi2(), &sc->sc_hi2);
		err |= __put_user(mflo2(), &sc->sc_lo2);
		err |= __put_user(mfhi3(), &sc->sc_hi3);
		err |= __put_user(mflo3(), &sc->sc_lo3);
		err |= __put_user(rddsp(DSP_MASK), &sc->sc_dsp);
	}

	used_math = !!used_math();
	err |= __put_user(used_math, &sc->sc_used_math);

	if (used_math) {
		/*
		 * Save FPU state to signal context. Signal handler
		 * will "inherit" current FPU state.
		 */
		err |= protected_save_fp_context(sc);
	}
	return err;
}

int fpcsr_pending(unsigned int __user *fpcsr)
{
	int err, sig = 0;
	unsigned int csr, enabled;

	err = __get_user(csr, fpcsr);
	enabled = FPU_CSR_UNI_X | ((csr & FPU_CSR_ALL_E) << 5);
	/*
	 * If the signal handler set some FPU exceptions, clear it and
	 * send SIGFPE.
	 */
	if (csr & enabled) {
		csr &= ~enabled;
		err |= __put_user(csr, fpcsr);
		sig = SIGFPE;
	}
	return err ?: sig;
}

static int
check_and_restore_fp_context(struct sigcontext __user *sc)
{
	int err, sig;

	err = sig = fpcsr_pending(&sc->sc_fpc_csr);
	if (err > 0)
		err = 0;
	err |= protected_restore_fp_context(sc);
	return err ?: sig;
}

int restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc)
{
	unsigned int used_math;
	unsigned long treg;
	int err = 0;
	int i;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	err |= __get_user(regs->cp0_epc, &sc->sc_pc);

#ifdef CONFIG_CPU_HAS_SMARTMIPS
	err |= __get_user(regs->acx, &sc->sc_acx);
#endif
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

	err |= __get_user(used_math, &sc->sc_used_math);
	conditional_used_math(used_math);

	if (used_math) {
		/* restore fpu context if we have used it before */
		if (!err)
			err = check_and_restore_fp_context(sc);
	} else {
		/* signal handler may have used FPU.  Give it up. */
		lose_fpu(0);
	}

	return err;
}

void __user *get_sigframe(struct k_sigaction *ka, struct pt_regs *regs,
			  size_t frame_size)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = regs->regs[29];

	/*
	 * FPU emulator may have it's own trampoline active just
	 * above the user stack, 16-bytes before the next lowest
	 * 16 byte boundary.  Try to avoid trashing it.
	 */
	sp -= 32;

	/* This is the X/Open sanctioned signal stack switching.  */
	if ((ka->sa.sa_flags & SA_ONSTACK) && (sas_ss_flags (sp) == 0))
		sp = current->sas_ss_sp + current->sas_ss_size;

	return (void __user *)((sp - frame_size) & (ICACHE_REFILLS_WORKAROUND_WAR ? ~(cpu_icache_line_size()-1) : ALMASK));
}

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */

#ifdef CONFIG_TRAD_SIGNALS
asmlinkage int sys_sigsuspend(nabi_no_regargs struct pt_regs regs)
{
	sigset_t newset;
	sigset_t __user *uset;

	uset = (sigset_t __user *) regs.regs[4];
	if (copy_from_user(&newset, uset, sizeof(sigset_t)))
		return -EFAULT;
	return sigsuspend(&newset);
}
#endif

SYSCALL_DEFINE2(rt_sigsuspend, sigset_t __user *,unewset, size_t, sigsetsize)
{
	sigset_t newset;

	/* XXX Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	return sigsuspend(&newset);
}

#ifdef CONFIG_TRAD_SIGNALS
SYSCALL_DEFINE3(sigaction, int, sig, const struct sigaction __user *, act,
	struct sigaction __user *, oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;
	int err = 0;

	if (act) {
		old_sigset_t mask;

		if (!access_ok(VERIFY_READ, act, sizeof(*act)))
			return -EFAULT;
		err |= __get_user(new_ka.sa.sa_handler, &act->sa_handler);
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
		err |= __put_user(old_ka.sa.sa_handler, &oact->sa_handler);
		err |= __put_user(old_ka.sa.sa_mask.sig[0], oact->sa_mask.sig);
		err |= __put_user(0, &oact->sa_mask.sig[1]);
		err |= __put_user(0, &oact->sa_mask.sig[2]);
		err |= __put_user(0, &oact->sa_mask.sig[3]);
		if (err)
			return -EFAULT;
	}

	return ret;
}
#endif

#ifdef CONFIG_TRAD_SIGNALS
asmlinkage void sys_sigreturn(nabi_no_regargs struct pt_regs regs)
{
	struct sigframe __user *frame;
	sigset_t blocked;
	int sig;

	frame = (struct sigframe __user *) regs.regs[29];
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&blocked, &frame->sf_mask, sizeof(blocked)))
		goto badframe;

	set_current_blocked(&blocked);

	sig = restore_sigcontext(&regs, &frame->sf_sc);
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
#endif /* CONFIG_TRAD_SIGNALS */

asmlinkage void sys_rt_sigreturn(nabi_no_regargs struct pt_regs regs)
{
	struct rt_sigframe __user *frame;
	sigset_t set;
	int sig;

	frame = (struct rt_sigframe __user *) regs.regs[29];
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->rs_uc.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	sig = restore_sigcontext(&regs, &frame->rs_uc.uc_mcontext);
	if (sig < 0)
		goto badframe;
	else if (sig)
		force_sig(sig, current);

	if (restore_altstack(&frame->rs_uc.uc_stack))
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

#ifdef CONFIG_TRAD_SIGNALS
static int setup_frame(void *sig_return, struct k_sigaction *ka,
		       struct pt_regs *regs, int signr, sigset_t *set)
{
	struct sigframe __user *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		goto give_sigsegv;

	err |= setup_sigcontext(regs, &frame->sf_sc);
	err |= __copy_to_user(&frame->sf_mask, set, sizeof(*set));
	if (err)
		goto give_sigsegv;

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
	regs->regs[ 4] = signr;
	regs->regs[ 5] = 0;
	regs->regs[ 6] = (unsigned long) &frame->sf_sc;
	regs->regs[29] = (unsigned long) frame;
	regs->regs[31] = (unsigned long) sig_return;
	regs->cp0_epc = regs->regs[25] = (unsigned long) ka->sa.sa_handler;

	DEBUGP("SIG deliver (%s:%d): sp=0x%p pc=0x%lx ra=0x%lx\n",
	       current->comm, current->pid,
	       frame, regs->cp0_epc, regs->regs[31]);
	return 0;

give_sigsegv:
	force_sigsegv(signr, current);
	return -EFAULT;
}
#endif

static int setup_rt_frame(void *sig_return, struct k_sigaction *ka,
			  struct pt_regs *regs,	int signr, sigset_t *set,
			  siginfo_t *info)
{
	struct rt_sigframe __user *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		goto give_sigsegv;

	/* Create siginfo.  */
	err |= copy_siginfo_to_user(&frame->rs_info, info);

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->rs_uc.uc_flags);
	err |= __put_user(NULL, &frame->rs_uc.uc_link);
	err |= __save_altstack(&frame->rs_uc.uc_stack, regs->regs[29]);
	err |= setup_sigcontext(regs, &frame->rs_uc.uc_mcontext);
	err |= __copy_to_user(&frame->rs_uc.uc_sigmask, set, sizeof(*set));

	if (err)
		goto give_sigsegv;

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
	regs->regs[ 4] = signr;
	regs->regs[ 5] = (unsigned long) &frame->rs_info;
	regs->regs[ 6] = (unsigned long) &frame->rs_uc;
	regs->regs[29] = (unsigned long) frame;
	regs->regs[31] = (unsigned long) sig_return;
	regs->cp0_epc = regs->regs[25] = (unsigned long) ka->sa.sa_handler;

	DEBUGP("SIG deliver (%s:%d): sp=0x%p pc=0x%lx ra=0x%lx\n",
	       current->comm, current->pid,
	       frame, regs->cp0_epc, regs->regs[31]);

	return 0;

give_sigsegv:
	force_sigsegv(signr, current);
	return -EFAULT;
}

struct mips_abi mips_abi = {
#ifdef CONFIG_TRAD_SIGNALS
	.setup_frame	= setup_frame,
	.signal_return_offset = offsetof(struct mips_vdso, signal_trampoline),
#endif
	.setup_rt_frame	= setup_rt_frame,
	.rt_signal_return_offset =
		offsetof(struct mips_vdso, rt_signal_trampoline),
	.restart	= __NR_restart_syscall
};

static void handle_signal(unsigned long sig, siginfo_t *info,
	struct k_sigaction *ka, struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
	int ret;
	struct mips_abi *abi = current->thread.abi;
	void *vdso = current->mm->context.vdso;

	if (regs->regs[0]) {
		switch(regs->regs[2]) {
		case ERESTART_RESTARTBLOCK:
		case ERESTARTNOHAND:
			regs->regs[2] = EINTR;
			break;
		case ERESTARTSYS:
			if (!(ka->sa.sa_flags & SA_RESTART)) {
				regs->regs[2] = EINTR;
				break;
			}
		/* fallthrough */
		case ERESTARTNOINTR:
			regs->regs[7] = regs->regs[26];
			regs->regs[2] = regs->regs[0];
			regs->cp0_epc -= 4;
		}

		regs->regs[0] = 0;		/* Don't deal with this again.  */
	}

	if (sig_uses_siginfo(ka))
		ret = abi->setup_rt_frame(vdso + abi->rt_signal_return_offset,
					  ka, regs, sig, oldset, info);
	else
		ret = abi->setup_frame(vdso + abi->signal_return_offset,
				       ka, regs, sig, oldset);

	if (ret)
		return;

	signal_delivered(sig, info, ka, regs, 0);
}

static void do_signal(struct pt_regs *regs)
{
	struct k_sigaction ka;
	siginfo_t info;
	int signr;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		/* Whee!  Actually deliver the signal.  */
		handle_signal(signr, &info, &ka, regs);
		return;
	}

	if (regs->regs[0]) {
		switch (regs->regs[2]) {
		case ERESTARTNOHAND:
		case ERESTARTSYS:
		case ERESTARTNOINTR:
			regs->regs[2] = regs->regs[0];
			regs->regs[7] = regs->regs[26];
			regs->cp0_epc -= 4;
			break;

		case ERESTART_RESTARTBLOCK:
			regs->regs[2] = current->thread.abi->restart;
			regs->regs[7] = regs->regs[26];
			regs->cp0_epc -= 4;
			break;
		}
		regs->regs[0] = 0;	/* Don't deal with this again.  */
	}

	/*
	 * If there's no signal to deliver, we just put the saved sigmask
	 * back
	 */
	restore_saved_sigmask();
}

/*
 * notification of userspace execution resumption
 * - triggered by the TIF_WORK_MASK flags
 */
asmlinkage void do_notify_resume(struct pt_regs *regs, void *unused,
	__u32 thread_info_flags)
{
	local_irq_enable();

	/* deal with pending signal delivery */
	if (thread_info_flags & _TIF_SIGPENDING)
		do_signal(regs);

	if (thread_info_flags & _TIF_NOTIFY_RESUME) {
		clear_thread_flag(TIF_NOTIFY_RESUME);
		tracehook_notify_resume(regs);
	}
}

#ifdef CONFIG_SMP
static int smp_save_fp_context(struct sigcontext __user *sc)
{
	return raw_cpu_has_fpu
	       ? _save_fp_context(sc)
	       : fpu_emulator_save_context(sc);
}

static int smp_restore_fp_context(struct sigcontext __user *sc)
{
	return raw_cpu_has_fpu
	       ? _restore_fp_context(sc)
	       : fpu_emulator_restore_context(sc);
}
#endif

static int signal_setup(void)
{
#ifdef CONFIG_SMP
	/* For now just do the cpu_has_fpu check when the functions are invoked */
	save_fp_context = smp_save_fp_context;
	restore_fp_context = smp_restore_fp_context;
#else
	if (cpu_has_fpu) {
		save_fp_context = _save_fp_context;
		restore_fp_context = _restore_fp_context;
	} else {
		save_fp_context = fpu_emulator_save_context;
		restore_fp_context = fpu_emulator_restore_context;
	}
#endif

	return 0;
}

arch_initcall(signal_setup);
