/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 1994 - 2000  Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/personality.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/compiler.h>

#include <asm/asm.h>
#include <linux/bitops.h>
#include <asm/cacheflush.h>
#include <asm/fpu.h>
#include <asm/sim.h>
#include <asm/uaccess.h>
#include <asm/ucontext.h>
#include <asm/cpu-features.h>

#include "signal-common.h"

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

static int do_signal(sigset_t *oldset, struct pt_regs *regs);

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */

#ifdef CONFIG_TRAD_SIGNALS
save_static_function(sys_sigsuspend);
__attribute_used__ noinline static int
_sys_sigsuspend(nabi_no_regargs struct pt_regs regs)
{
	sigset_t *uset, saveset, newset;

	uset = (sigset_t *) regs.regs[4];
	if (copy_from_user(&newset, uset, sizeof(sigset_t)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs.regs[2] = EINTR;
	regs.regs[7] = 1;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, &regs))
			return -EINTR;
	}
}
#endif

save_static_function(sys_rt_sigsuspend);
__attribute_used__ noinline static int
_sys_rt_sigsuspend(nabi_no_regargs struct pt_regs regs)
{
	sigset_t *unewset, saveset, newset;
	size_t sigsetsize;

	/* XXX Don't preclude handling different sized sigset_t's.  */
	sigsetsize = regs.regs[5];
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	unewset = (sigset_t *) regs.regs[4];
	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	current->blocked = newset;
        recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs.regs[2] = EINTR;
	regs.regs[7] = 1;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, &regs))
			return -EINTR;
	}
}

#ifdef CONFIG_TRAD_SIGNALS
asmlinkage int sys_sigaction(int sig, const struct sigaction *act,
	struct sigaction *oact)
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

asmlinkage int sys_sigaltstack(nabi_no_regargs struct pt_regs regs)
{
	const stack_t *uss = (const stack_t *) regs.regs[4];
	stack_t *uoss = (stack_t *) regs.regs[5];
	unsigned long usp = regs.regs[29];

	return do_sigaltstack(uss, uoss, usp);
}

#if PLAT_TRAMPOLINE_STUFF_LINE
#define __tramp __attribute__((aligned(PLAT_TRAMPOLINE_STUFF_LINE)))
#else
#define __tramp
#endif

#ifdef CONFIG_TRAD_SIGNALS
struct sigframe {
	u32 sf_ass[4];			/* argument save space for o32 */
	u32 sf_code[2] __tramp;		/* signal trampoline */
	struct sigcontext sf_sc __tramp;
	sigset_t sf_mask;
};
#endif

struct rt_sigframe {
	u32 rs_ass[4];			/* argument save space for o32 */
	u32 rs_code[2] __tramp;		/* signal trampoline */
	struct siginfo rs_info __tramp;
	struct ucontext rs_uc;
};

#ifdef CONFIG_TRAD_SIGNALS
save_static_function(sys_sigreturn);
__attribute_used__ noinline static void
_sys_sigreturn(nabi_no_regargs struct pt_regs regs)
{
	struct sigframe *frame;
	sigset_t blocked;

	frame = (struct sigframe *) regs.regs[29];
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&blocked, &frame->sf_mask, sizeof(blocked)))
		goto badframe;

	sigdelsetmask(&blocked, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = blocked;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(&regs, &frame->sf_sc))
		goto badframe;

	/*
	 * Don't let your children do this ...
	 */
	if (current_thread_info()->flags & TIF_SYSCALL_TRACE)
		do_syscall_trace(&regs, 1);
	__asm__ __volatile__(
		"move\t$29, %0\n\t"
		"j\tsyscall_exit"
		:/* no outputs */
		:"r" (&regs));
	/* Unreached */

badframe:
	force_sig(SIGSEGV, current);
}
#endif

save_static_function(sys_rt_sigreturn);
__attribute_used__ noinline static void
_sys_rt_sigreturn(nabi_no_regargs struct pt_regs regs)
{
	struct rt_sigframe *frame;
	sigset_t set;
	stack_t st;

	frame = (struct rt_sigframe *) regs.regs[29];
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->rs_uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(&regs, &frame->rs_uc.uc_mcontext))
		goto badframe;

	if (__copy_from_user(&st, &frame->rs_uc.uc_stack, sizeof(st)))
		goto badframe;
	/* It is more difficult to avoid calling this function than to
	   call it and ignore errors.  */
	do_sigaltstack(&st, NULL, regs.regs[29]);

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
static void inline setup_frame(struct k_sigaction * ka, struct pt_regs *regs,
	int signr, sigset_t *set)
{
	struct sigframe *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		goto give_sigsegv;

	/*
	 * Set up the return code ...
	 *
	 *         li      v0, __NR_sigreturn
	 *         syscall
	 */
	if (PLAT_TRAMPOLINE_STUFF_LINE)
		__clear_user(frame->sf_code, PLAT_TRAMPOLINE_STUFF_LINE);
	err |= __put_user(0x24020000 + __NR_sigreturn, frame->sf_code + 0);
	err |= __put_user(0x0000000c                 , frame->sf_code + 1);
	flush_cache_sigtramp((unsigned long) frame->sf_code);

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
	regs->regs[31] = (unsigned long) frame->sf_code;
	regs->cp0_epc = regs->regs[25] = (unsigned long) ka->sa.sa_handler;

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=0x%p pc=0x%lx ra=0x%p\n",
	       current->comm, current->pid,
	       frame, regs->cp0_epc, frame->regs[31]);
#endif
        return;

give_sigsegv:
	force_sigsegv(signr, current);
}
#endif

static void inline setup_rt_frame(struct k_sigaction * ka, struct pt_regs *regs,
	int signr, sigset_t *set, siginfo_t *info)
{
	struct rt_sigframe *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		goto give_sigsegv;

	/*
	 * Set up the return code ...
	 *
	 *         li      v0, __NR_rt_sigreturn
	 *         syscall
	 */
	if (PLAT_TRAMPOLINE_STUFF_LINE)
		__clear_user(frame->rs_code, PLAT_TRAMPOLINE_STUFF_LINE);
	err |= __put_user(0x24020000 + __NR_rt_sigreturn, frame->rs_code + 0);
	err |= __put_user(0x0000000c                    , frame->rs_code + 1);
	flush_cache_sigtramp((unsigned long) frame->rs_code);

	/* Create siginfo.  */
	err |= copy_siginfo_to_user(&frame->rs_info, info);

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->rs_uc.uc_flags);
	err |= __put_user(0, &frame->rs_uc.uc_link);
	err |= __put_user((void *)current->sas_ss_sp,
	                  &frame->rs_uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->regs[29]),
	                  &frame->rs_uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size,
	                  &frame->rs_uc.uc_stack.ss_size);
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
	regs->regs[31] = (unsigned long) frame->rs_code;
	regs->cp0_epc = regs->regs[25] = (unsigned long) ka->sa.sa_handler;

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=0x%p pc=0x%lx ra=0x%p\n",
	       current->comm, current->pid,
	       frame, regs->cp0_epc, regs->regs[31]);
#endif
	return;

give_sigsegv:
	force_sigsegv(signr, current);
}

extern void setup_rt_frame_n32(struct k_sigaction * ka,
	struct pt_regs *regs, int signr, sigset_t *set, siginfo_t *info);

static inline void handle_signal(unsigned long sig, siginfo_t *info,
	struct k_sigaction *ka, sigset_t *oldset, struct pt_regs *regs)
{
	switch(regs->regs[0]) {
	case ERESTART_RESTARTBLOCK:
	case ERESTARTNOHAND:
		regs->regs[2] = EINTR;
		break;
	case ERESTARTSYS:
		if(!(ka->sa.sa_flags & SA_RESTART)) {
			regs->regs[2] = EINTR;
			break;
		}
	/* fallthrough */
	case ERESTARTNOINTR:		/* Userland will reload $v0.  */
		regs->regs[7] = regs->regs[26];
		regs->cp0_epc -= 8;
	}

	regs->regs[0] = 0;		/* Don't deal with this again.  */

#ifdef CONFIG_TRAD_SIGNALS
	if (ka->sa.sa_flags & SA_SIGINFO) {
#else
	if (1) {
#endif
#ifdef CONFIG_MIPS32_N32
		if ((current->thread.mflags & MF_ABI_MASK) == MF_N32)
			setup_rt_frame_n32 (ka, regs, sig, oldset, info);
		else
#endif
			setup_rt_frame(ka, regs, sig, oldset, info);
	}
#ifdef CONFIG_TRAD_SIGNALS
	else
		setup_frame(ka, regs, sig, oldset);
#endif

	if (!(ka->sa.sa_flags & SA_NODEFER)) {
		spin_lock_irq(&current->sighand->siglock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,sig);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}
}

extern int do_signal32(sigset_t *oldset, struct pt_regs *regs);
extern int do_irix_signal(sigset_t *oldset, struct pt_regs *regs);

static int do_signal(sigset_t *oldset, struct pt_regs *regs)
{
	struct k_sigaction ka;
	siginfo_t info;
	int signr;

#ifdef CONFIG_BINFMT_ELF32
	if ((current->thread.mflags & MF_ABI_MASK) == MF_O32) {
		return do_signal32(oldset, regs);
	}
#endif

	/*
	 * We want the common case to go fast, which is why we may in certain
	 * cases get here from kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return 1;

	if (try_to_freeze(0))
		goto no_signal;

	if (!oldset)
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		handle_signal(signr, &info, &ka, oldset, regs);
		return 1;
	}

no_signal:
	/*
	 * Who's code doesn't conform to the restartable syscall convention
	 * dies here!!!  The li instruction, a single machine instruction,
	 * must directly be followed by the syscall instruction.
	 */
	if (regs->regs[0]) {
		if (regs->regs[2] == ERESTARTNOHAND ||
		    regs->regs[2] == ERESTARTSYS ||
		    regs->regs[2] == ERESTARTNOINTR) {
			regs->regs[7] = regs->regs[26];
			regs->cp0_epc -= 8;
		}
		if (regs->regs[2] == ERESTART_RESTARTBLOCK) {
			regs->regs[2] = __NR_restart_syscall;
			regs->regs[7] = regs->regs[26];
			regs->cp0_epc -= 4;
		}
	}
	return 0;
}

/*
 * notification of userspace execution resumption
 * - triggered by current->work.notify_resume
 */
asmlinkage void do_notify_resume(struct pt_regs *regs, sigset_t *oldset,
	__u32 thread_info_flags)
{
	/* deal with pending signal delivery */
	if (thread_info_flags & _TIF_SIGPENDING) {
#ifdef CONFIG_BINFMT_ELF32
		if (likely((current->thread.mflags & MF_ABI_MASK) == MF_O32)) {
			do_signal32(oldset, regs);
			return;
		}
#endif
#ifdef CONFIG_BINFMT_IRIX
		if (unlikely(current->personality != PER_LINUX)) {
			do_irix_signal(oldset, regs);
			return;
		}
#endif
		do_signal(oldset, regs);
	}
}
