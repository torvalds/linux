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
#include <asm/vdso.h>
#include <asm/dsp.h>

#include "signal-common.h"

static int (*save_fp_context32)(struct sigcontext32 __user *sc);
static int (*restore_fp_context32)(struct sigcontext32 __user *sc);

extern asmlinkage int _save_fp_context32(struct sigcontext32 __user *sc);
extern asmlinkage int _restore_fp_context32(struct sigcontext32 __user *sc);

extern asmlinkage int fpu_emulator_save_context32(struct sigcontext32 __user *sc);
extern asmlinkage int fpu_emulator_restore_context32(struct sigcontext32 __user *sc);

/*
 * Including <asm/unistd.h> would give use the 64-bit syscall numbers ...
 */
#define __NR_O32_restart_syscall        4253

/* 32-bit compatibility types */

typedef unsigned int __sighandler32_t;
typedef void (*vfptr_t)(void);

struct sigaction32 {
	unsigned int		sa_flags;
	__sighandler32_t	sa_handler;
	compat_sigset_t		sa_mask;
};

struct ucontext32 {
	u32                 uc_flags;
	s32                 uc_link;
	compat_stack_t      uc_stack;
	struct sigcontext32 uc_mcontext;
	compat_sigset_t     uc_sigmask;   /* mask last for extensibility */
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

/*
 * sigcontext handlers
 */
static int protected_save_fp_context32(struct sigcontext32 __user *sc)
{
	int err;
	while (1) {
		lock_fpu_owner();
		own_fpu_inatomic(1);
		err = save_fp_context32(sc); /* this might fail */
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

static int protected_restore_fp_context32(struct sigcontext32 __user *sc)
{
	int err, tmp __maybe_unused;
	while (1) {
		lock_fpu_owner();
		own_fpu_inatomic(0);
		err = restore_fp_context32(sc); /* this might fail */
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

static int setup_sigcontext32(struct pt_regs *regs,
			      struct sigcontext32 __user *sc)
{
	int err = 0;
	int i;
	u32 used_math;

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

	used_math = !!used_math();
	err |= __put_user(used_math, &sc->sc_used_math);

	if (used_math) {
		/*
		 * Save FPU state to signal context.  Signal handler
		 * will "inherit" current FPU state.
		 */
		err |= protected_save_fp_context32(sc);
	}
	return err;
}

static int
check_and_restore_fp_context32(struct sigcontext32 __user *sc)
{
	int err, sig;

	err = sig = fpcsr_pending(&sc->sc_fpc_csr);
	if (err > 0)
		err = 0;
	err |= protected_restore_fp_context32(sc);
	return err ?: sig;
}

static int restore_sigcontext32(struct pt_regs *regs,
				struct sigcontext32 __user *sc)
{
	u32 used_math;
	int err = 0;
	s32 treg;
	int i;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

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

	err |= __get_user(used_math, &sc->sc_used_math);
	conditional_used_math(used_math);

	if (used_math) {
		/* restore fpu context if we have used it before */
		if (!err)
			err = check_and_restore_fp_context32(sc);
	} else {
		/* signal handler may have used FPU.  Give it up. */
		lose_fpu(0);
	}

	return err;
}

/*
 *
 */
extern void __put_sigset_unknown_nsig(void);
extern void __get_sigset_unknown_nsig(void);

static inline int put_sigset(const sigset_t *kbuf, compat_sigset_t __user *ubuf)
{
	int err = 0;

	if (!access_ok(VERIFY_WRITE, ubuf, sizeof(*ubuf)))
		return -EFAULT;

	switch (_NSIG_WORDS) {
	default:
		__put_sigset_unknown_nsig();
	case 2:
		err |= __put_user(kbuf->sig[1] >> 32, &ubuf->sig[3]);
		err |= __put_user(kbuf->sig[1] & 0xffffffff, &ubuf->sig[2]);
	case 1:
		err |= __put_user(kbuf->sig[0] >> 32, &ubuf->sig[1]);
		err |= __put_user(kbuf->sig[0] & 0xffffffff, &ubuf->sig[0]);
	}

	return err;
}

static inline int get_sigset(sigset_t *kbuf, const compat_sigset_t __user *ubuf)
{
	int err = 0;
	unsigned long sig[4];

	if (!access_ok(VERIFY_READ, ubuf, sizeof(*ubuf)))
		return -EFAULT;

	switch (_NSIG_WORDS) {
	default:
		__get_sigset_unknown_nsig();
	case 2:
		err |= __get_user(sig[3], &ubuf->sig[3]);
		err |= __get_user(sig[2], &ubuf->sig[2]);
		kbuf->sig[1] = sig[2] | (sig[3] << 32);
	case 1:
		err |= __get_user(sig[1], &ubuf->sig[1]);
		err |= __get_user(sig[0], &ubuf->sig[0]);
		kbuf->sig[0] = sig[0] | (sig[1] << 32);
	}

	return err;
}

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */

asmlinkage int sys32_sigsuspend(compat_sigset_t __user *uset)
{
	return compat_sys_rt_sigsuspend(uset, sizeof(compat_sigset_t));
}

SYSCALL_DEFINE3(32_sigaction, long, sig, const struct sigaction32 __user *, act,
	struct sigaction32 __user *, oact)
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

int copy_siginfo_to_user32(compat_siginfo_t __user *to, siginfo_t *from)
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
		}
	}
	return err;
}

int copy_siginfo_from_user32(siginfo_t *to, compat_siginfo_t __user *from)
{
	memset(to, 0, sizeof *to);

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

static int setup_frame_32(void *sig_return, struct k_sigaction *ka,
			  struct pt_regs *regs, int signr, sigset_t *set)
{
	struct sigframe32 __user *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		goto give_sigsegv;

	err |= setup_sigcontext32(regs, &frame->sf_sc);
	err |= __copy_conv_sigset_to_user(&frame->sf_mask, set);

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

static int setup_rt_frame_32(void *sig_return, struct k_sigaction *ka,
			     struct pt_regs *regs, int signr, sigset_t *set,
			     siginfo_t *info)
{
	struct rt_sigframe32 __user *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		goto give_sigsegv;

	/* Convert (siginfo_t -> compat_siginfo_t) and copy to user. */
	err |= copy_siginfo_to_user32(&frame->rs_info, info);

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->rs_uc.uc_flags);
	err |= __put_user(0, &frame->rs_uc.uc_link);
	err |= __compat_save_altstack(&frame->rs_uc.uc_stack, regs->regs[29]);
	err |= setup_sigcontext32(regs, &frame->rs_uc.uc_mcontext);
	err |= __copy_conv_sigset_to_user(&frame->rs_uc.uc_sigmask, set);

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
	 * the struct rt_sigframe32.
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

/*
 * o32 compatibility on 64-bit kernels, without DSP ASE
 */
struct mips_abi mips_abi_32 = {
	.setup_frame	= setup_frame_32,
	.signal_return_offset =
		offsetof(struct mips_vdso, o32_signal_trampoline),
	.setup_rt_frame	= setup_rt_frame_32,
	.rt_signal_return_offset =
		offsetof(struct mips_vdso, o32_rt_signal_trampoline),
	.restart	= __NR_O32_restart_syscall
};

SYSCALL_DEFINE4(32_rt_sigaction, int, sig,
	const struct sigaction32 __user *, act,
	struct sigaction32 __user *, oact, unsigned int, sigsetsize)
{
	struct k_sigaction new_sa, old_sa;
	int ret = -EINVAL;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		goto out;

	if (act) {
		s32 handler;
		int err = 0;

		if (!access_ok(VERIFY_READ, act, sizeof(*act)))
			return -EFAULT;
		err |= __get_user(handler, &act->sa_handler);
		new_sa.sa.sa_handler = (void __user *)(s64)handler;
		err |= __get_user(new_sa.sa.sa_flags, &act->sa_flags);
		err |= get_sigset(&new_sa.sa.sa_mask, &act->sa_mask);
		if (err)
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_sa : NULL, oact ? &old_sa : NULL);

	if (!ret && oact) {
		int err = 0;

		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)))
			return -EFAULT;

		err |= __put_user((u32)(u64)old_sa.sa.sa_handler,
		                   &oact->sa_handler);
		err |= __put_user(old_sa.sa.sa_flags, &oact->sa_flags);
		err |= put_sigset(&old_sa.sa.sa_mask, &oact->sa_mask);
		if (err)
			return -EFAULT;
	}
out:
	return ret;
}

static int signal32_init(void)
{
	if (cpu_has_fpu) {
		save_fp_context32 = _save_fp_context32;
		restore_fp_context32 = _restore_fp_context32;
	} else {
		save_fp_context32 = fpu_emulator_save_context32;
		restore_fp_context32 = fpu_emulator_restore_context32;
	}

	return 0;
}

arch_initcall(signal32_init);
