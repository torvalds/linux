// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/compat.h>
#include <linux/signal.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/linkage.h>

#include <asm/csr.h>
#include <asm/signal32.h>
#include <asm/switch_to.h>
#include <asm/ucontext.h>
#include <asm/vdso.h>

#define COMPAT_DEBUG_SIG 0

struct compat_sigcontext {
	struct compat_user_regs_struct sc_regs;
	union __riscv_fp_state sc_fpregs;
};

struct compat_ucontext {
	compat_ulong_t		uc_flags;
	struct compat_ucontext	*uc_link;
	compat_stack_t		uc_stack;
	sigset_t		uc_sigmask;
	/* There's some padding here to allow sigset_t to be expanded in the
	 * future.  Though this is unlikely, other architectures put uc_sigmask
	 * at the end of this structure and explicitly state it can be
	 * expanded, so we didn't want to box ourselves in here. */
	__u8		  __unused[1024 / 8 - sizeof(sigset_t)];
	/* We can't put uc_sigmask at the end of this structure because we need
	 * to be able to expand sigcontext in the future.  For example, the
	 * vector ISA extension will almost certainly add ISA state.  We want
	 * to ensure all user-visible ISA state can be saved and restored via a
	 * ucontext, so we're putting this at the end in order to allow for
	 * infinite extensibility.  Since we know this will be extended and we
	 * assume sigset_t won't be extended an extreme amount, we're
	 * prioritizing this. */
	struct compat_sigcontext uc_mcontext;
};

struct compat_rt_sigframe {
	struct compat_siginfo info;
	struct compat_ucontext uc;
};

#ifdef CONFIG_FPU
static long compat_restore_fp_state(struct pt_regs *regs,
	union __riscv_fp_state __user *sc_fpregs)
{
	long err;
	struct __riscv_d_ext_state __user *state = &sc_fpregs->d;
	size_t i;

	err = __copy_from_user(&current->thread.fstate, state, sizeof(*state));
	if (unlikely(err))
		return err;

	fstate_restore(current, regs);

	/* We support no other extension state at this time. */
	for (i = 0; i < ARRAY_SIZE(sc_fpregs->q.reserved); i++) {
		u32 value;

		err = __get_user(value, &sc_fpregs->q.reserved[i]);
		if (unlikely(err))
			break;
		if (value != 0)
			return -EINVAL;
	}

	return err;
}

static long compat_save_fp_state(struct pt_regs *regs,
			  union __riscv_fp_state __user *sc_fpregs)
{
	long err;
	struct __riscv_d_ext_state __user *state = &sc_fpregs->d;
	size_t i;

	fstate_save(current, regs);
	err = __copy_to_user(state, &current->thread.fstate, sizeof(*state));
	if (unlikely(err))
		return err;

	/* We support no other extension state at this time. */
	for (i = 0; i < ARRAY_SIZE(sc_fpregs->q.reserved); i++) {
		err = __put_user(0, &sc_fpregs->q.reserved[i]);
		if (unlikely(err))
			break;
	}

	return err;
}
#else
#define compat_save_fp_state(task, regs) (0)
#define compat_restore_fp_state(task, regs) (0)
#endif

static long compat_restore_sigcontext(struct pt_regs *regs,
	struct compat_sigcontext __user *sc)
{
	long err;
	struct compat_user_regs_struct cregs;

	/* sc_regs is structured the same as the start of pt_regs */
	err = __copy_from_user(&cregs, &sc->sc_regs, sizeof(sc->sc_regs));

	cregs_to_regs(&cregs, regs);

	/* Restore the floating-point state. */
	if (has_fpu())
		err |= compat_restore_fp_state(regs, &sc->sc_fpregs);
	return err;
}

COMPAT_SYSCALL_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct compat_rt_sigframe __user *frame;
	struct task_struct *task;
	sigset_t set;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	frame = (struct compat_rt_sigframe __user *)regs->sp;

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;

	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	if (compat_restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (compat_restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->a0;

badframe:
	task = current;
	if (show_unhandled_signals) {
		pr_info_ratelimited(
			"%s[%d]: bad frame in %s: frame=%p pc=%p sp=%p\n",
			task->comm, task_pid_nr(task), __func__,
			frame, (void *)regs->epc, (void *)regs->sp);
	}
	force_sig(SIGSEGV);
	return 0;
}

static long compat_setup_sigcontext(struct compat_rt_sigframe __user *frame,
	struct pt_regs *regs)
{
	struct compat_sigcontext __user *sc = &frame->uc.uc_mcontext;
	struct compat_user_regs_struct cregs;
	long err;

	regs_to_cregs(&cregs, regs);

	/* sc_regs is structured the same as the start of pt_regs */
	err = __copy_to_user(&sc->sc_regs, &cregs, sizeof(sc->sc_regs));
	/* Save the floating-point state. */
	if (has_fpu())
		err |= compat_save_fp_state(regs, &sc->sc_fpregs);
	return err;
}

static inline void __user *compat_get_sigframe(struct ksignal *ksig,
	struct pt_regs *regs, size_t framesize)
{
	unsigned long sp;
	/* Default to using normal stack */
	sp = regs->sp;

	/*
	 * If we are on the alternate signal stack and would overflow it, don't.
	 * Return an always-bogus address instead so we will die with SIGSEGV.
	 */
	if (on_sig_stack(sp) && !likely(on_sig_stack(sp - framesize)))
		return (void __user __force *)(-1UL);

	/* This is the X/Open sanctioned signal stack switching. */
	sp = sigsp(sp, ksig) - framesize;

	/* Align the stack frame. */
	sp &= ~0xfUL;

	return (void __user *)sp;
}

int compat_setup_rt_frame(struct ksignal *ksig, sigset_t *set,
	struct pt_regs *regs)
{
	struct compat_rt_sigframe __user *frame;
	long err = 0;

	frame = compat_get_sigframe(ksig, regs, sizeof(*frame));
	if (!access_ok(frame, sizeof(*frame)))
		return -EFAULT;

	err |= copy_siginfo_to_user32(&frame->info, &ksig->info);

	/* Create the ucontext. */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(NULL, &frame->uc.uc_link);
	err |= __compat_save_altstack(&frame->uc.uc_stack, regs->sp);
	err |= compat_setup_sigcontext(frame, regs);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		return -EFAULT;

	regs->ra = (unsigned long)COMPAT_VDSO_SYMBOL(
			current->mm->context.vdso, rt_sigreturn);

	/*
	 * Set up registers for signal handler.
	 * Registers that we don't modify keep the value they had from
	 * user-space at the time we took the signal.
	 * We always pass siginfo and mcontext, regardless of SA_SIGINFO,
	 * since some things rely on this (e.g. glibc's debug/segfault.c).
	 */
	regs->epc = (unsigned long)ksig->ka.sa.sa_handler;
	regs->sp = (unsigned long)frame;
	regs->a0 = ksig->sig;                     /* a0: signal number */
	regs->a1 = (unsigned long)(&frame->info); /* a1: siginfo pointer */
	regs->a2 = (unsigned long)(&frame->uc);   /* a2: ucontext pointer */

#if COMPAT_DEBUG_SIG
	pr_info("SIG deliver (%s:%d): sig=%d pc=%p ra=%p sp=%p\n",
		current->comm, task_pid_nr(current), ksig->sig,
		(void *)regs->epc, (void *)regs->ra, frame);
#endif

	return 0;
}
