/*
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/personality.h>
#include <linux/suspend.h>
#include <linux/ptrace.h>
#include <linux/elf.h>
#include <linux/compat.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <asm/processor.h>
#include <asm/ucontext.h>
#include <asm/sigframe.h>
#include <asm/syscalls.h>
#include <arch/interrupts.h>

#define DEBUG_SIG 0

SYSCALL_DEFINE3(sigaltstack, const stack_t __user *, uss,
		stack_t __user *, uoss, struct pt_regs *, regs)
{
	return do_sigaltstack(uss, uoss, regs->sp);
}


/*
 * Do a signal return; undo the signal stack.
 */

int restore_sigcontext(struct pt_regs *regs,
		       struct sigcontext __user *sc)
{
	int err = 0;
	int i;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	/*
	 * Enforce that sigcontext is like pt_regs, and doesn't mess
	 * up our stack alignment rules.
	 */
	BUILD_BUG_ON(sizeof(struct sigcontext) != sizeof(struct pt_regs));
	BUILD_BUG_ON(sizeof(struct sigcontext) % 8 != 0);

	for (i = 0; i < sizeof(struct pt_regs)/sizeof(long); ++i)
		err |= __get_user(regs->regs[i], &sc->gregs[i]);

	/* Ensure that the PL is always set to USER_PL. */
	regs->ex1 = PL_ICS_EX1(USER_PL, EX1_ICS(regs->ex1));

	regs->faultnum = INT_SWINT_1_SIGRETURN;

	return err;
}

void signal_fault(const char *type, struct pt_regs *regs,
		  void __user *frame, int sig)
{
	trace_unhandled_signal(type, regs, (unsigned long)frame, SIGSEGV);
	force_sigsegv(sig, current);
}

/* The assembly shim for this function arranges to ignore the return value. */
SYSCALL_DEFINE1(rt_sigreturn, struct pt_regs *, regs)
{
	struct rt_sigframe __user *frame =
		(struct rt_sigframe __user *)(regs->sp);
	sigset_t set;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (do_sigaltstack(&frame->uc.uc_stack, NULL, regs->sp) == -EFAULT)
		goto badframe;

	return 0;

badframe:
	signal_fault("bad sigreturn frame", regs, frame, 0);
	return 0;
}

/*
 * Set up a signal frame.
 */

int setup_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs)
{
	int i, err = 0;

	for (i = 0; i < sizeof(struct pt_regs)/sizeof(long); ++i)
		err |= __put_user(regs->regs[i], &sc->gregs[i]);

	return err;
}

/*
 * Determine which stack to use..
 */
static inline void __user *get_sigframe(struct k_sigaction *ka,
					struct pt_regs *regs,
					size_t frame_size)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = regs->sp;

	/*
	 * If we are on the alternate signal stack and would overflow
	 * it, don't.  Return an always-bogus address instead so we
	 * will die with SIGSEGV.
	 */
	if (on_sig_stack(sp) && !likely(on_sig_stack(sp - frame_size)))
		return (void __user __force *)-1UL;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (sas_ss_flags(sp) == 0)
			sp = current->sas_ss_sp + current->sas_ss_size;
	}

	sp -= frame_size;
	/*
	 * Align the stack pointer according to the TILE ABI,
	 * i.e. so that on function entry (sp & 15) == 0.
	 */
	sp &= -16UL;
	return (void __user *) sp;
}

static int setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs *regs)
{
	unsigned long restorer;
	struct rt_sigframe __user *frame;
	int err = 0;
	int usig;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	usig = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	/* Always write at least the signal number for the stack backtracer. */
	if (ka->sa.sa_flags & SA_SIGINFO) {
		/* At sigreturn time, restore the callee-save registers too. */
		err |= copy_siginfo_to_user(&frame->info, info);
		regs->flags |= PT_FLAGS_RESTORE_REGS;
	} else {
		err |= __put_user(info->si_signo, &frame->info.si_signo);
	}

	/* Create the ucontext.  */
	err |= __clear_user(&frame->save_area, sizeof(frame->save_area));
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(NULL, &frame->uc.uc_link);
	err |= __put_user((void __user *)(current->sas_ss_sp),
			  &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->sp),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, regs);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		goto give_sigsegv;

	restorer = VDSO_BASE;
	if (ka->sa.sa_flags & SA_RESTORER)
		restorer = (unsigned long) ka->sa.sa_restorer;

	/*
	 * Set up registers for signal handler.
	 * Registers that we don't modify keep the value they had from
	 * user-space at the time we took the signal.
	 * We always pass siginfo and mcontext, regardless of SA_SIGINFO,
	 * since some things rely on this (e.g. glibc's debug/segfault.c).
	 */
	regs->pc = (unsigned long) ka->sa.sa_handler;
	regs->ex1 = PL_ICS_EX1(USER_PL, 1); /* set crit sec in handler */
	regs->sp = (unsigned long) frame;
	regs->lr = restorer;
	regs->regs[0] = (unsigned long) usig;
	regs->regs[1] = (unsigned long) &frame->info;
	regs->regs[2] = (unsigned long) &frame->uc;
	regs->flags |= PT_FLAGS_CALLER_SAVES;
	return 0;

give_sigsegv:
	signal_fault("bad setup frame", regs, frame, sig);
	return -EFAULT;
}

/*
 * OK, we're invoking a handler
 */

static void handle_signal(unsigned long sig, siginfo_t *info,
			 struct k_sigaction *ka,
			 struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
	int ret;

	/* Are we from a system call? */
	if (regs->faultnum == INT_SWINT_1) {
		/* If so, check system call restarting.. */
		switch (regs->regs[0]) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->regs[0] = -EINTR;
			break;

		case -ERESTARTSYS:
			if (!(ka->sa.sa_flags & SA_RESTART)) {
				regs->regs[0] = -EINTR;
				break;
			}
			/* fallthrough */
		case -ERESTARTNOINTR:
			/* Reload caller-saves to restore r0..r5 and r10. */
			regs->flags |= PT_FLAGS_CALLER_SAVES;
			regs->regs[0] = regs->orig_r0;
			regs->pc -= 8;
		}
	}

	/* Set up the stack frame */
#ifdef CONFIG_COMPAT
	if (is_compat_task())
		ret = compat_setup_rt_frame(sig, ka, info, oldset, regs);
	else
#endif
		ret = setup_rt_frame(sig, ka, info, oldset, regs);
	if (ret)
		return;
	signal_delivered(sig, info, ka, regs,
			test_thread_flag(TIF_SINGLESTEP));
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
void do_signal(struct pt_regs *regs)
{
	siginfo_t info;
	int signr;
	struct k_sigaction ka;

	/*
	 * i386 will check if we're coming from kernel mode and bail out
	 * here.  In my experience this just turns weird crashes into
	 * weird spin-hangs.  But if we find a case where this seems
	 * helpful, we can reinstate the check on "!user_mode(regs)".
	 */

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		/* Whee! Actually deliver the signal.  */
		handle_signal(signr, &info, &ka, regs);
		goto done;
	}

	/* Did we come from a system call? */
	if (regs->faultnum == INT_SWINT_1) {
		/* Restart the system call - no handlers present */
		switch (regs->regs[0]) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->flags |= PT_FLAGS_CALLER_SAVES;
			regs->regs[0] = regs->orig_r0;
			regs->pc -= 8;
			break;

		case -ERESTART_RESTARTBLOCK:
			regs->flags |= PT_FLAGS_CALLER_SAVES;
			regs->regs[TREG_SYSCALL_NR] = __NR_restart_syscall;
			regs->pc -= 8;
			break;
		}
	}

	/* If there's no signal to deliver, just put the saved sigmask back. */
	restore_saved_sigmask();

done:
	/* Avoid double syscall restart if there are nested signals. */
	regs->faultnum = INT_SWINT_1_SIGRETURN;
}

int show_unhandled_signals = 1;

static int __init crashinfo(char *str)
{
	unsigned long val;
	const char *word;

	if (*str == '\0')
		val = 2;
	else if (*str != '=' || strict_strtoul(++str, 0, &val) != 0)
		return 0;
	show_unhandled_signals = val;
	switch (show_unhandled_signals) {
	case 0:
		word = "No";
		break;
	case 1:
		word = "One-line";
		break;
	default:
		word = "Detailed";
		break;
	}
	pr_info("%s crash reports will be generated on the console\n", word);
	return 1;
}
__setup("crashinfo", crashinfo);

static void dump_mem(void __user *address)
{
	void __user *addr;
	enum { region_size = 256, bytes_per_line = 16 };
	int i, j, k;
	int found_readable_mem = 0;

	pr_err("\n");
	if (!access_ok(VERIFY_READ, address, 1)) {
		pr_err("Not dumping at address 0x%lx (kernel address)\n",
		       (unsigned long)address);
		return;
	}

	addr = (void __user *)
		(((unsigned long)address & -bytes_per_line) - region_size/2);
	if (addr > address)
		addr = NULL;
	for (i = 0; i < region_size;
	     addr += bytes_per_line, i += bytes_per_line) {
		unsigned char buf[bytes_per_line];
		char line[100];
		if (copy_from_user(buf, addr, bytes_per_line))
			continue;
		if (!found_readable_mem) {
			pr_err("Dumping memory around address 0x%lx:\n",
			       (unsigned long)address);
			found_readable_mem = 1;
		}
		j = sprintf(line, REGFMT":", (unsigned long)addr);
		for (k = 0; k < bytes_per_line; ++k)
			j += sprintf(&line[j], " %02x", buf[k]);
		pr_err("%s\n", line);
	}
	if (!found_readable_mem)
		pr_err("No readable memory around address 0x%lx\n",
		       (unsigned long)address);
}

void trace_unhandled_signal(const char *type, struct pt_regs *regs,
			    unsigned long address, int sig)
{
	struct task_struct *tsk = current;

	if (show_unhandled_signals == 0)
		return;

	/* If the signal is handled, don't show it here. */
	if (!is_global_init(tsk)) {
		void __user *handler =
			tsk->sighand->action[sig-1].sa.sa_handler;
		if (handler != SIG_IGN && handler != SIG_DFL)
			return;
	}

	/* Rate-limit the one-line output, not the detailed output. */
	if (show_unhandled_signals <= 1 && !printk_ratelimit())
		return;

	printk("%s%s[%d]: %s at %lx pc "REGFMT" signal %d",
	       task_pid_nr(tsk) > 1 ? KERN_INFO : KERN_EMERG,
	       tsk->comm, task_pid_nr(tsk), type, address, regs->pc, sig);

	print_vma_addr(KERN_CONT " in ", regs->pc);

	printk(KERN_CONT "\n");

	if (show_unhandled_signals > 1) {
		switch (sig) {
		case SIGILL:
		case SIGFPE:
		case SIGSEGV:
		case SIGBUS:
			pr_err("User crash: signal %d,"
			       " trap %ld, address 0x%lx\n",
			       sig, regs->faultnum, address);
			show_regs(regs);
			dump_mem((void __user *)address);
			break;
		default:
			pr_err("User crash: signal %d, trap %ld\n",
			       sig, regs->faultnum);
			break;
		}
	}
}
