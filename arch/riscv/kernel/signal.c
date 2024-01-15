// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 * Copyright (C) 2012 Regents of the University of California
 */

#include <linux/compat.h>
#include <linux/signal.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/resume_user_mode.h>
#include <linux/linkage.h>
#include <linux/entry-common.h>

#include <asm/ucontext.h>
#include <asm/vdso.h>
#include <asm/signal.h>
#include <asm/signal32.h>
#include <asm/switch_to.h>
#include <asm/vector.h>
#include <asm/csr.h>
#include <asm/cacheflush.h>

unsigned long signal_minsigstksz __ro_after_init;

extern u32 __user_rt_sigreturn[2];
static size_t riscv_v_sc_size __ro_after_init;

#define DEBUG_SIG 0

struct rt_sigframe {
	struct siginfo info;
	struct ucontext uc;
#ifndef CONFIG_MMU
	u32 sigreturn_code[2];
#endif
};

#ifdef CONFIG_FPU
static long restore_fp_state(struct pt_regs *regs,
			     union __riscv_fp_state __user *sc_fpregs)
{
	long err;
	struct __riscv_d_ext_state __user *state = &sc_fpregs->d;

	err = __copy_from_user(&current->thread.fstate, state, sizeof(*state));
	if (unlikely(err))
		return err;

	fstate_restore(current, regs);
	return 0;
}

static long save_fp_state(struct pt_regs *regs,
			  union __riscv_fp_state __user *sc_fpregs)
{
	long err;
	struct __riscv_d_ext_state __user *state = &sc_fpregs->d;

	fstate_save(current, regs);
	err = __copy_to_user(state, &current->thread.fstate, sizeof(*state));
	return err;
}
#else
#define save_fp_state(task, regs) (0)
#define restore_fp_state(task, regs) (0)
#endif

#ifdef CONFIG_RISCV_ISA_V

static long save_v_state(struct pt_regs *regs, void __user **sc_vec)
{
	struct __riscv_ctx_hdr __user *hdr;
	struct __sc_riscv_v_state __user *state;
	void __user *datap;
	long err;

	hdr = *sc_vec;
	/* Place state to the user's signal context space after the hdr */
	state = (struct __sc_riscv_v_state __user *)(hdr + 1);
	/* Point datap right after the end of __sc_riscv_v_state */
	datap = state + 1;

	/* datap is designed to be 16 byte aligned for better performance */
	WARN_ON(unlikely(!IS_ALIGNED((unsigned long)datap, 16)));

	get_cpu_vector_context();
	riscv_v_vstate_save(&current->thread.vstate, regs);
	put_cpu_vector_context();

	/* Copy everything of vstate but datap. */
	err = __copy_to_user(&state->v_state, &current->thread.vstate,
			     offsetof(struct __riscv_v_ext_state, datap));
	/* Copy the pointer datap itself. */
	err |= __put_user(datap, &state->v_state.datap);
	/* Copy the whole vector content to user space datap. */
	err |= __copy_to_user(datap, current->thread.vstate.datap, riscv_v_vsize);
	/* Copy magic to the user space after saving  all vector conetext */
	err |= __put_user(RISCV_V_MAGIC, &hdr->magic);
	err |= __put_user(riscv_v_sc_size, &hdr->size);
	if (unlikely(err))
		return err;

	/* Only progress the sv_vec if everything has done successfully  */
	*sc_vec += riscv_v_sc_size;
	return 0;
}

/*
 * Restore Vector extension context from the user's signal frame. This function
 * assumes a valid extension header. So magic and size checking must be done by
 * the caller.
 */
static long __restore_v_state(struct pt_regs *regs, void __user *sc_vec)
{
	long err;
	struct __sc_riscv_v_state __user *state = sc_vec;
	void __user *datap;

	/* Copy everything of __sc_riscv_v_state except datap. */
	err = __copy_from_user(&current->thread.vstate, &state->v_state,
			       offsetof(struct __riscv_v_ext_state, datap));
	if (unlikely(err))
		return err;

	/* Copy the pointer datap itself. */
	err = __get_user(datap, &state->v_state.datap);
	if (unlikely(err))
		return err;
	/*
	 * Copy the whole vector content from user space datap. Use
	 * copy_from_user to prevent information leak.
	 */
	err = copy_from_user(current->thread.vstate.datap, datap, riscv_v_vsize);
	if (unlikely(err))
		return err;

	riscv_v_vstate_set_restore(current, regs);

	return err;
}
#else
#define save_v_state(task, regs) (0)
#define __restore_v_state(task, regs) (0)
#endif

static long restore_sigcontext(struct pt_regs *regs,
	struct sigcontext __user *sc)
{
	void __user *sc_ext_ptr = &sc->sc_extdesc.hdr;
	__u32 rsvd;
	long err;
	/* sc_regs is structured the same as the start of pt_regs */
	err = __copy_from_user(regs, &sc->sc_regs, sizeof(sc->sc_regs));
	if (unlikely(err))
		return err;

	/* Restore the floating-point state. */
	if (has_fpu()) {
		err = restore_fp_state(regs, &sc->sc_fpregs);
		if (unlikely(err))
			return err;
	}

	/* Check the reserved word before extensions parsing */
	err = __get_user(rsvd, &sc->sc_extdesc.reserved);
	if (unlikely(err))
		return err;
	if (unlikely(rsvd))
		return -EINVAL;

	while (!err) {
		__u32 magic, size;
		struct __riscv_ctx_hdr __user *head = sc_ext_ptr;

		err |= __get_user(magic, &head->magic);
		err |= __get_user(size, &head->size);
		if (unlikely(err))
			return err;

		sc_ext_ptr += sizeof(*head);
		switch (magic) {
		case END_MAGIC:
			if (size != END_HDR_SIZE)
				return -EINVAL;

			return 0;
		case RISCV_V_MAGIC:
			if (!has_vector() || !riscv_v_vstate_query(regs) ||
			    size != riscv_v_sc_size)
				return -EINVAL;

			err = __restore_v_state(regs, sc_ext_ptr);
			break;
		default:
			return -EINVAL;
		}
		sc_ext_ptr = (void __user *)head + size;
	}
	return err;
}

static size_t get_rt_frame_size(bool cal_all)
{
	struct rt_sigframe __user *frame;
	size_t frame_size;
	size_t total_context_size = 0;

	frame_size = sizeof(*frame);

	if (has_vector()) {
		if (cal_all || riscv_v_vstate_query(task_pt_regs(current)))
			total_context_size += riscv_v_sc_size;
	}
	/*
	 * Preserved a __riscv_ctx_hdr for END signal context header if an
	 * extension uses __riscv_extra_ext_header
	 */
	if (total_context_size)
		total_context_size += sizeof(struct __riscv_ctx_hdr);

	frame_size += total_context_size;

	frame_size = round_up(frame_size, 16);
	return frame_size;
}

SYSCALL_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe __user *frame;
	struct task_struct *task;
	sigset_t set;
	size_t frame_size = get_rt_frame_size(false);

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	frame = (struct rt_sigframe __user *)regs->sp;

	if (!access_ok(frame, frame_size))
		goto badframe;

	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	regs->cause = -1UL;

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

static long setup_sigcontext(struct rt_sigframe __user *frame,
	struct pt_regs *regs)
{
	struct sigcontext __user *sc = &frame->uc.uc_mcontext;
	struct __riscv_ctx_hdr __user *sc_ext_ptr = &sc->sc_extdesc.hdr;
	long err;

	/* sc_regs is structured the same as the start of pt_regs */
	err = __copy_to_user(&sc->sc_regs, regs, sizeof(sc->sc_regs));
	/* Save the floating-point state. */
	if (has_fpu())
		err |= save_fp_state(regs, &sc->sc_fpregs);
	/* Save the vector state. */
	if (has_vector() && riscv_v_vstate_query(regs))
		err |= save_v_state(regs, (void __user **)&sc_ext_ptr);
	/* Write zero to fp-reserved space and check it on restore_sigcontext */
	err |= __put_user(0, &sc->sc_extdesc.reserved);
	/* And put END __riscv_ctx_hdr at the end. */
	err |= __put_user(END_MAGIC, &sc_ext_ptr->magic);
	err |= __put_user(END_HDR_SIZE, &sc_ext_ptr->size);

	return err;
}

static inline void __user *get_sigframe(struct ksignal *ksig,
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

static int setup_rt_frame(struct ksignal *ksig, sigset_t *set,
	struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	long err = 0;
	unsigned long __maybe_unused addr;
	size_t frame_size = get_rt_frame_size(false);

	frame = get_sigframe(ksig, regs, frame_size);
	if (!access_ok(frame, frame_size))
		return -EFAULT;

	err |= copy_siginfo_to_user(&frame->info, &ksig->info);

	/* Create the ucontext. */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(NULL, &frame->uc.uc_link);
	err |= __save_altstack(&frame->uc.uc_stack, regs->sp);
	err |= setup_sigcontext(frame, regs);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		return -EFAULT;

	/* Set up to return from userspace. */
#ifdef CONFIG_MMU
	regs->ra = (unsigned long)VDSO_SYMBOL(
		current->mm->context.vdso, rt_sigreturn);
#else
	/*
	 * For the nommu case we don't have a VDSO.  Instead we push two
	 * instructions to call the rt_sigreturn syscall onto the user stack.
	 */
	if (copy_to_user(&frame->sigreturn_code, __user_rt_sigreturn,
			 sizeof(frame->sigreturn_code)))
		return -EFAULT;

	addr = (unsigned long)&frame->sigreturn_code;
	/* Make sure the two instructions are pushed to icache. */
	flush_icache_range(addr, addr + sizeof(frame->sigreturn_code));

	regs->ra = addr;
#endif /* CONFIG_MMU */

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

#if DEBUG_SIG
	pr_info("SIG deliver (%s:%d): sig=%d pc=%p ra=%p sp=%p\n",
		current->comm, task_pid_nr(current), ksig->sig,
		(void *)regs->epc, (void *)regs->ra, frame);
#endif

	return 0;
}

static void handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
	int ret;

	rseq_signal_deliver(ksig, regs);

	/* Set up the stack frame */
	if (is_compat_task())
		ret = compat_setup_rt_frame(ksig, oldset, regs);
	else
		ret = setup_rt_frame(ksig, oldset, regs);

	signal_setup_done(ret, ksig, 0);
}

void arch_do_signal_or_restart(struct pt_regs *regs)
{
	unsigned long continue_addr = 0, restart_addr = 0;
	int retval = 0;
	struct ksignal ksig;
	bool syscall = (regs->cause == EXC_SYSCALL);

	/* If we were from a system call, check for system call restarting */
	if (syscall) {
		continue_addr = regs->epc;
		restart_addr = continue_addr - 4;
		retval = regs->a0;

		/* Avoid additional syscall restarting via ret_from_exception */
		regs->cause = -1UL;

		/*
		 * Prepare for system call restart. We do this here so that a
		 * debugger will see the already changed PC.
		 */
		switch (retval) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
		case -ERESTART_RESTARTBLOCK:
			regs->a0 = regs->orig_a0;
			regs->epc = restart_addr;
			break;
		}
	}

	/*
	 * Get the signal to deliver. When running under ptrace, at this point
	 * the debugger may change all of our registers.
	 */
	if (get_signal(&ksig)) {
		/*
		 * Depending on the signal settings, we may need to revert the
		 * decision to restart the system call, but skip this if a
		 * debugger has chosen to restart at a different PC.
		 */
		if (regs->epc == restart_addr &&
		    (retval == -ERESTARTNOHAND ||
		     retval == -ERESTART_RESTARTBLOCK ||
		     (retval == -ERESTARTSYS &&
		      !(ksig.ka.sa.sa_flags & SA_RESTART)))) {
			regs->a0 = -EINTR;
			regs->epc = continue_addr;
		}

		/* Actually deliver the signal */
		handle_signal(&ksig, regs);
		return;
	}

	/*
	 * Handle restarting a different system call. As above, if a debugger
	 * has chosen to restart at a different PC, ignore the restart.
	 */
	if (syscall && regs->epc == restart_addr && retval == -ERESTART_RESTARTBLOCK)
		regs->a7 = __NR_restart_syscall;

	/*
	 * If there is no signal to deliver, we just put the saved
	 * sigmask back.
	 */
	restore_saved_sigmask();
}

void init_rt_signal_env(void);
void __init init_rt_signal_env(void)
{
	riscv_v_sc_size = sizeof(struct __riscv_ctx_hdr) +
			  sizeof(struct __sc_riscv_v_state) + riscv_v_vsize;
	/*
	 * Determine the stack space required for guaranteed signal delivery.
	 * The signal_minsigstksz will be populated into the AT_MINSIGSTKSZ entry
	 * in the auxiliary array at process startup.
	 */
	signal_minsigstksz = get_rt_frame_size(true);
}

#ifdef CONFIG_DYNAMIC_SIGFRAME
bool sigaltstack_size_valid(size_t ss_size)
{
	return ss_size > get_rt_frame_size(false);
}
#endif /* CONFIG_DYNAMIC_SIGFRAME */
