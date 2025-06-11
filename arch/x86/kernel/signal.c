// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen SuSE Labs
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *  2000-06-20  Pentium III FXSR, SSE support by Gareth Hughes
 *  2000-2002   x86-64 support by Andi Kleen
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/personality.h>
#include <linux/uaccess.h>
#include <linux/user-return-notifier.h>
#include <linux/uprobes.h>
#include <linux/context_tracking.h>
#include <linux/entry-common.h>
#include <linux/syscalls.h>
#include <linux/rseq.h>

#include <asm/processor.h>
#include <asm/ucontext.h>
#include <asm/fpu/signal.h>
#include <asm/fpu/xstate.h>
#include <asm/vdso.h>
#include <asm/mce.h>
#include <asm/sighandling.h>
#include <asm/vm86.h>

#include <asm/syscall.h>
#include <asm/sigframe.h>
#include <asm/signal.h>
#include <asm/shstk.h>

static inline int is_ia32_compat_frame(struct ksignal *ksig)
{
	return IS_ENABLED(CONFIG_IA32_EMULATION) &&
		ksig->ka.sa.sa_flags & SA_IA32_ABI;
}

static inline int is_ia32_frame(struct ksignal *ksig)
{
	return IS_ENABLED(CONFIG_X86_32) || is_ia32_compat_frame(ksig);
}

static inline int is_x32_frame(struct ksignal *ksig)
{
	return IS_ENABLED(CONFIG_X86_X32_ABI) &&
		ksig->ka.sa.sa_flags & SA_X32_ABI;
}

/*
 * Enable all pkeys temporarily, so as to ensure that both the current
 * execution stack as well as the alternate signal stack are writeable.
 * The application can use any of the available pkeys to protect the
 * alternate signal stack, and we don't know which one it is, so enable
 * all. The PKRU register will be reset to init_pkru later in the flow,
 * in fpu__clear_user_states(), and it is the application's responsibility
 * to enable the appropriate pkey as the first step in the signal handler
 * so that the handler does not segfault.
 */
static inline u32 sig_prepare_pkru(void)
{
	u32 orig_pkru = read_pkru();

	write_pkru(0);
	return orig_pkru;
}

/*
 * Set up a signal frame.
 */

/* x86 ABI requires 16-byte alignment */
#define FRAME_ALIGNMENT	16UL

#define MAX_FRAME_PADDING	(FRAME_ALIGNMENT - 1)

/*
 * Determine which stack to use..
 */
void __user *
get_sigframe(struct ksignal *ksig, struct pt_regs *regs, size_t frame_size,
	     void __user **fpstate)
{
	struct k_sigaction *ka = &ksig->ka;
	int ia32_frame = is_ia32_frame(ksig);
	/* Default to using normal stack */
	bool nested_altstack = on_sig_stack(regs->sp);
	bool entering_altstack = false;
	unsigned long math_size = 0;
	unsigned long sp = regs->sp;
	unsigned long buf_fx = 0;
	u32 pkru;

	/* redzone */
	if (!ia32_frame)
		sp -= 128;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		/*
		 * This checks nested_altstack via sas_ss_flags(). Sensible
		 * programs use SS_AUTODISARM, which disables that check, and
		 * programs that don't use SS_AUTODISARM get compatible.
		 */
		if (sas_ss_flags(sp) == 0) {
			sp = current->sas_ss_sp + current->sas_ss_size;
			entering_altstack = true;
		}
	} else if (ia32_frame &&
		   !nested_altstack &&
		   regs->ss != __USER_DS &&
		   !(ka->sa.sa_flags & SA_RESTORER) &&
		   ka->sa.sa_restorer) {
		/* This is the legacy signal stack switching. */
		sp = (unsigned long) ka->sa.sa_restorer;
		entering_altstack = true;
	}

	sp = fpu__alloc_mathframe(sp, ia32_frame, &buf_fx, &math_size);
	*fpstate = (void __user *)sp;

	sp -= frame_size;

	if (ia32_frame)
		/*
		 * Align the stack pointer according to the i386 ABI,
		 * i.e. so that on function entry ((sp + 4) & 15) == 0.
		 */
		sp = ((sp + 4) & -FRAME_ALIGNMENT) - 4;
	else
		sp = round_down(sp, FRAME_ALIGNMENT) - 8;

	/*
	 * If we are on the alternate signal stack and would overflow it, don't.
	 * Return an always-bogus address instead so we will die with SIGSEGV.
	 */
	if (unlikely((nested_altstack || entering_altstack) &&
		     !__on_sig_stack(sp))) {

		if (show_unhandled_signals && printk_ratelimit())
			pr_info("%s[%d] overflowed sigaltstack\n",
				current->comm, task_pid_nr(current));

		return (void __user *)-1L;
	}

	/* Update PKRU to enable access to the alternate signal stack. */
	pkru = sig_prepare_pkru();
	/* save i387 and extended state */
	if (!copy_fpstate_to_sigframe(*fpstate, (void __user *)buf_fx, math_size, pkru)) {
		/*
		 * Restore PKRU to the original, user-defined value; disable
		 * extra pkeys enabled for the alternate signal stack, if any.
		 */
		write_pkru(pkru);
		return (void __user *)-1L;
	}

	return (void __user *)sp;
}

/*
 * There are four different struct types for signal frame: sigframe_ia32,
 * rt_sigframe_ia32, rt_sigframe_x32, and rt_sigframe. Use the worst case
 * -- the largest size. It means the size for 64-bit apps is a bit more
 * than needed, but this keeps the code simple.
 */
#if defined(CONFIG_X86_32) || defined(CONFIG_IA32_EMULATION)
# define MAX_FRAME_SIGINFO_UCTXT_SIZE	sizeof(struct sigframe_ia32)
#else
# define MAX_FRAME_SIGINFO_UCTXT_SIZE	sizeof(struct rt_sigframe)
#endif

/*
 * The FP state frame contains an XSAVE buffer which must be 64-byte aligned.
 * If a signal frame starts at an unaligned address, extra space is required.
 * This is the max alignment padding, conservatively.
 */
#define MAX_XSAVE_PADDING	63UL

/*
 * The frame data is composed of the following areas and laid out as:
 *
 * -------------------------
 * | alignment padding     |
 * -------------------------
 * | (f)xsave frame        |
 * -------------------------
 * | fsave header          |
 * -------------------------
 * | alignment padding     |
 * -------------------------
 * | siginfo + ucontext    |
 * -------------------------
 */

/* max_frame_size tells userspace the worst case signal stack size. */
static unsigned long __ro_after_init max_frame_size;
static unsigned int __ro_after_init fpu_default_state_size;

static int __init init_sigframe_size(void)
{
	fpu_default_state_size = fpu__get_fpstate_size();

	max_frame_size = MAX_FRAME_SIGINFO_UCTXT_SIZE + MAX_FRAME_PADDING;

	max_frame_size += fpu_default_state_size + MAX_XSAVE_PADDING;

	/* Userspace expects an aligned size. */
	max_frame_size = round_up(max_frame_size, FRAME_ALIGNMENT);

	pr_info("max sigframe size: %lu\n", max_frame_size);
	return 0;
}
early_initcall(init_sigframe_size);

unsigned long get_sigframe_size(void)
{
	return max_frame_size;
}

static int
setup_rt_frame(struct ksignal *ksig, struct pt_regs *regs)
{
	/* Perform fixup for the pre-signal frame. */
	rseq_signal_deliver(ksig, regs);

	/* Set up the stack frame */
	if (is_ia32_frame(ksig)) {
		if (ksig->ka.sa.sa_flags & SA_SIGINFO)
			return ia32_setup_rt_frame(ksig, regs);
		else
			return ia32_setup_frame(ksig, regs);
	} else if (is_x32_frame(ksig)) {
		return x32_setup_rt_frame(ksig, regs);
	} else {
		return x64_setup_rt_frame(ksig, regs);
	}
}

static void
handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	bool stepping, failed;
	struct fpu *fpu = x86_task_fpu(current);

	if (v8086_mode(regs))
		save_v86_state((struct kernel_vm86_regs *) regs, VM86_SIGNAL);

	/* Are we from a system call? */
	if (syscall_get_nr(current, regs) != -1) {
		/* If so, check system call restarting.. */
		switch (syscall_get_error(current, regs)) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->ax = -EINTR;
			break;

		case -ERESTARTSYS:
			if (!(ksig->ka.sa.sa_flags & SA_RESTART)) {
				regs->ax = -EINTR;
				break;
			}
			fallthrough;
		case -ERESTARTNOINTR:
			regs->ax = regs->orig_ax;
			regs->ip -= 2;
			break;
		}
	}

	/*
	 * If TF is set due to a debugger (TIF_FORCED_TF), clear TF now
	 * so that register information in the sigcontext is correct and
	 * then notify the tracer before entering the signal handler.
	 */
	stepping = test_thread_flag(TIF_SINGLESTEP);
	if (stepping)
		user_disable_single_step(current);

	failed = (setup_rt_frame(ksig, regs) < 0);
	if (!failed) {
		/*
		 * Clear the direction flag as per the ABI for function entry.
		 *
		 * Clear RF when entering the signal handler, because
		 * it might disable possible debug exception from the
		 * signal handler.
		 *
		 * Clear TF for the case when it wasn't set by debugger to
		 * avoid the recursive send_sigtrap() in SIGTRAP handler.
		 */
		regs->flags &= ~(X86_EFLAGS_DF|X86_EFLAGS_RF|X86_EFLAGS_TF);
		/*
		 * Ensure the signal handler starts with the new fpu state.
		 */
		fpu__clear_user_states(fpu);
	}
	signal_setup_done(failed, ksig, stepping);
}

static inline unsigned long get_nr_restart_syscall(const struct pt_regs *regs)
{
#ifdef CONFIG_IA32_EMULATION
	if (current->restart_block.arch_data & TS_COMPAT)
		return __NR_ia32_restart_syscall;
#endif
#ifdef CONFIG_X86_X32_ABI
	return __NR_restart_syscall | (regs->orig_ax & __X32_SYSCALL_BIT);
#else
	return __NR_restart_syscall;
#endif
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
void arch_do_signal_or_restart(struct pt_regs *regs)
{
	struct ksignal ksig;

	if (get_signal(&ksig)) {
		/* Whee! Actually deliver the signal.  */
		handle_signal(&ksig, regs);
		return;
	}

	/* Did we come from a system call? */
	if (syscall_get_nr(current, regs) != -1) {
		/* Restart the system call - no handlers present */
		switch (syscall_get_error(current, regs)) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->ax = regs->orig_ax;
			regs->ip -= 2;
			break;

		case -ERESTART_RESTARTBLOCK:
			regs->ax = get_nr_restart_syscall(regs);
			regs->ip -= 2;
			break;
		}
	}

	/*
	 * If there's no signal to deliver, we just put the saved sigmask
	 * back.
	 */
	restore_saved_sigmask();
}

void signal_fault(struct pt_regs *regs, void __user *frame, char *where)
{
	struct task_struct *me = current;

	if (show_unhandled_signals && printk_ratelimit()) {
		printk("%s"
		       "%s[%d] bad frame in %s frame:%p ip:%lx sp:%lx orax:%lx",
		       task_pid_nr(current) > 1 ? KERN_INFO : KERN_EMERG,
		       me->comm, me->pid, where, frame,
		       regs->ip, regs->sp, regs->orig_ax);
		print_vma_addr(KERN_CONT " in ", regs->ip);
		pr_cont("\n");
	}

	force_sig(SIGSEGV);
}

#ifdef CONFIG_DYNAMIC_SIGFRAME
#ifdef CONFIG_STRICT_SIGALTSTACK_SIZE
static bool strict_sigaltstack_size __ro_after_init = true;
#else
static bool strict_sigaltstack_size __ro_after_init = false;
#endif

static int __init strict_sas_size(char *arg)
{
	return kstrtobool(arg, &strict_sigaltstack_size) == 0;
}
__setup("strict_sas_size", strict_sas_size);

/*
 * MINSIGSTKSZ is 2048 and can't be changed despite the fact that AVX512
 * exceeds that size already. As such programs might never use the
 * sigaltstack they just continued to work. While always checking against
 * the real size would be correct, this might be considered a regression.
 *
 * Therefore avoid the sanity check, unless enforced by kernel
 * configuration or command line option.
 *
 * When dynamic FPU features are supported, the check is also enforced when
 * the task has permissions to use dynamic features. Tasks which have no
 * permission are checked against the size of the non-dynamic feature set
 * if strict checking is enabled. This avoids forcing all tasks on the
 * system to allocate large sigaltstacks even if they are never going
 * to use a dynamic feature. As this is serialized via sighand::siglock
 * any permission request for a dynamic feature either happened already
 * or will see the newly install sigaltstack size in the permission checks.
 */
bool sigaltstack_size_valid(size_t ss_size)
{
	unsigned long fsize = max_frame_size - fpu_default_state_size;
	u64 mask;

	lockdep_assert_held(&current->sighand->siglock);

	if (!fpu_state_size_dynamic() && !strict_sigaltstack_size)
		return true;

	fsize += x86_task_fpu(current->group_leader)->perm.__user_state_size;
	if (likely(ss_size > fsize))
		return true;

	if (strict_sigaltstack_size)
		return ss_size > fsize;

	mask = x86_task_fpu(current->group_leader)->perm.__state_perm;
	if (mask & XFEATURE_MASK_USER_DYNAMIC)
		return ss_size > fsize;

	return true;
}
#endif /* CONFIG_DYNAMIC_SIGFRAME */
