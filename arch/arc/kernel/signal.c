/*
 * Signal Handling for ARC
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * vineetg: Jan 2010 (Restarting of timer related syscalls)
 *
 * vineetg: Nov 2009 (Everything needed for TIF_RESTORE_SIGMASK)
 *  -do_signal() supports TIF_RESTORE_SIGMASK
 *  -do_signal() no loner needs oldset, required by OLD sys_sigsuspend
 *  -sys_rt_sigsuspend() now comes from generic code, so discard arch implemen
 *  -sys_sigsuspend() no longer needs to fudge ptregs, hence that arg removed
 *  -sys_sigsuspend() no longer loops for do_signal(), sets TIF_xxx and leaves
 *   the job to do_signal()
 *
 * vineetg: July 2009
 *  -Modified Code to support the uClibc provided userland sigreturn stub
 *   to avoid kernel synthesing it on user stack at runtime, costing TLB
 *   probes and Cache line flushes.
 *
 * vineetg: July 2009
 *  -In stash_usr_regs( ) and restore_usr_regs( ), save/restore of user regs
 *   in done in block copy rather than one word at a time.
 *   This saves around 2K of code and improves LMBench lat_sig <catch>
 *
 * rajeshwarr: Feb 2009
 *  - Support for Realtime Signals
 *
 * vineetg: Aug 11th 2008: Bug #94183
 *  -ViXS were still seeing crashes when using insmod to load drivers.
 *   It turned out that the code to change Execute permssions for TLB entries
 *   of user was not guarded for interrupts (mod_tlb_permission)
 *   This was causing TLB entries to be overwritten on unrelated indexes
 *
 * Vineetg: July 15th 2008: Bug #94183
 *  -Exception happens in Delay slot of a JMP, and before user space resumes,
 *   Signal is delivered (Ctrl + C) = >SIGINT.
 *   setup_frame( ) sets up PC,SP,BLINK to enable user space signal handler
 *   to run, but doesn't clear the Delay slot bit from status32. As a result,
 *   on resuming user mode, signal handler branches off to BTA of orig JMP
 *  -FIX: clear the DE bit from status32 in setup_frame( )
 *
 * Rahul Trivedi, Kanika Nema: Codito Technologies 2004
 */

#include <linux/signal.h>
#include <linux/ptrace.h>
#include <linux/personality.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/tracehook.h>
#include <asm/ucontext.h>

struct rt_sigframe {
	struct siginfo info;
	struct ucontext uc;
#define MAGIC_SIGALTSTK		0x07302004
	unsigned int sigret_magic;
};

static int
stash_usr_regs(struct rt_sigframe __user *sf, struct pt_regs *regs,
	       sigset_t *set)
{
	int err;
	struct user_regs_struct uregs;

	uregs.scratch.bta	= regs->bta;
	uregs.scratch.lp_start	= regs->lp_start;
	uregs.scratch.lp_end	= regs->lp_end;
	uregs.scratch.lp_count	= regs->lp_count;
	uregs.scratch.status32	= regs->status32;
	uregs.scratch.ret	= regs->ret;
	uregs.scratch.blink	= regs->blink;
	uregs.scratch.fp	= regs->fp;
	uregs.scratch.gp	= regs->r26;
	uregs.scratch.r12	= regs->r12;
	uregs.scratch.r11	= regs->r11;
	uregs.scratch.r10	= regs->r10;
	uregs.scratch.r9	= regs->r9;
	uregs.scratch.r8	= regs->r8;
	uregs.scratch.r7	= regs->r7;
	uregs.scratch.r6	= regs->r6;
	uregs.scratch.r5	= regs->r5;
	uregs.scratch.r4	= regs->r4;
	uregs.scratch.r3	= regs->r3;
	uregs.scratch.r2	= regs->r2;
	uregs.scratch.r1	= regs->r1;
	uregs.scratch.r0	= regs->r0;
	uregs.scratch.sp	= regs->sp;

	err = __copy_to_user(&(sf->uc.uc_mcontext.regs.scratch), &uregs.scratch,
			     sizeof(sf->uc.uc_mcontext.regs.scratch));
	err |= __copy_to_user(&sf->uc.uc_sigmask, set, sizeof(sigset_t));

	return err;
}

static int restore_usr_regs(struct pt_regs *regs, struct rt_sigframe __user *sf)
{
	sigset_t set;
	int err;
	struct user_regs_struct uregs;

	err = __copy_from_user(&set, &sf->uc.uc_sigmask, sizeof(set));
	err |= __copy_from_user(&uregs.scratch,
				&(sf->uc.uc_mcontext.regs.scratch),
				sizeof(sf->uc.uc_mcontext.regs.scratch));
	if (err)
		return err;

	set_current_blocked(&set);
	regs->bta	= uregs.scratch.bta;
	regs->lp_start	= uregs.scratch.lp_start;
	regs->lp_end	= uregs.scratch.lp_end;
	regs->lp_count	= uregs.scratch.lp_count;
	regs->status32	= uregs.scratch.status32;
	regs->ret	= uregs.scratch.ret;
	regs->blink	= uregs.scratch.blink;
	regs->fp	= uregs.scratch.fp;
	regs->r26	= uregs.scratch.gp;
	regs->r12	= uregs.scratch.r12;
	regs->r11	= uregs.scratch.r11;
	regs->r10	= uregs.scratch.r10;
	regs->r9	= uregs.scratch.r9;
	regs->r8	= uregs.scratch.r8;
	regs->r7	= uregs.scratch.r7;
	regs->r6	= uregs.scratch.r6;
	regs->r5	= uregs.scratch.r5;
	regs->r4	= uregs.scratch.r4;
	regs->r3	= uregs.scratch.r3;
	regs->r2	= uregs.scratch.r2;
	regs->r1	= uregs.scratch.r1;
	regs->r0	= uregs.scratch.r0;
	regs->sp	= uregs.scratch.sp;

	return 0;
}

static inline int is_do_ss_needed(unsigned int magic)
{
	if (MAGIC_SIGALTSTK == magic)
		return 1;
	else
		return 0;
}

SYSCALL_DEFINE0(rt_sigreturn)
{
	struct rt_sigframe __user *sf;
	unsigned int magic;
	struct pt_regs *regs = current_pt_regs();

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	/* Since we stacked the signal on a word boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (regs->sp & 3)
		goto badframe;

	sf = (struct rt_sigframe __force __user *)(regs->sp);

	if (!access_ok(VERIFY_READ, sf, sizeof(*sf)))
		goto badframe;

	if (__get_user(magic, &sf->sigret_magic))
		goto badframe;

	if (unlikely(is_do_ss_needed(magic)))
		if (restore_altstack(&sf->uc.uc_stack))
			goto badframe;

	if (restore_usr_regs(regs, sf))
		goto badframe;

	/* Don't restart from sigreturn */
	syscall_wont_restart(regs);

	/*
	 * Ensure that sigreturn always returns to user mode (in case the
	 * regs saved on user stack got fudged between save and sigreturn)
	 * Otherwise it is easy to panic the kernel with a custom
	 * signal handler and/or restorer which clobberes the status32/ret
	 * to return to a bogus location in kernel mode.
	 */
	regs->status32 |= STATUS_U_MASK;

	return regs->r0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

/*
 * Determine which stack to use..
 */
static inline void __user *get_sigframe(struct ksignal *ksig,
					struct pt_regs *regs,
					unsigned long framesize)
{
	unsigned long sp = sigsp(regs->sp, ksig);
	void __user *frame;

	/* No matter what happens, 'sp' must be word
	 * aligned otherwise nasty things could happen
	 */

	/* ATPCS B01 mandates 8-byte alignment */
	frame = (void __user *)((sp - framesize) & ~7);

	/* Check that we can actually write to the signal frame */
	if (!access_ok(VERIFY_WRITE, frame, framesize))
		frame = NULL;

	return frame;
}

static int
setup_rt_frame(struct ksignal *ksig, sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *sf;
	unsigned int magic = 0;
	int err = 0;

	sf = get_sigframe(ksig, regs, sizeof(struct rt_sigframe));
	if (!sf)
		return 1;

	/*
	 * w/o SA_SIGINFO, struct ucontext is partially populated (only
	 * uc_mcontext/uc_sigmask) for kernel's normal user state preservation
	 * during signal handler execution. This works for SA_SIGINFO as well
	 * although the semantics are now overloaded (the same reg state can be
	 * inspected by userland: but are they allowed to fiddle with it ?
	 */
	err |= stash_usr_regs(sf, regs, set);

	/*
	 * SA_SIGINFO requires 3 args to signal handler:
	 *  #1: sig-no (common to any handler)
	 *  #2: struct siginfo
	 *  #3: struct ucontext (completely populated)
	 */
	if (unlikely(ksig->ka.sa.sa_flags & SA_SIGINFO)) {
		err |= copy_siginfo_to_user(&sf->info, &ksig->info);
		err |= __put_user(0, &sf->uc.uc_flags);
		err |= __put_user(NULL, &sf->uc.uc_link);
		err |= __save_altstack(&sf->uc.uc_stack, regs->sp);

		/* setup args 2 and 3 for user mode handler */
		regs->r1 = (unsigned long)&sf->info;
		regs->r2 = (unsigned long)&sf->uc;

		/*
		 * small optim to avoid unconditonally calling do_sigaltstack
		 * in sigreturn path, now that we only have rt_sigreturn
		 */
		magic = MAGIC_SIGALTSTK;
	}

	err |= __put_user(magic, &sf->sigret_magic);
	if (err)
		return err;

	/* #1 arg to the user Signal handler */
	regs->r0 = ksig->sig;

	/* setup PC of user space signal handler */
	regs->ret = (unsigned long)ksig->ka.sa.sa_handler;

	/*
	 * handler returns using sigreturn stub provided already by userpsace
	 * If not, nuke the process right away
	 */
	if(!(ksig->ka.sa.sa_flags & SA_RESTORER))
		return 1;

	regs->blink = (unsigned long)ksig->ka.sa.sa_restorer;

	/* User Stack for signal handler will be above the frame just carved */
	regs->sp = (unsigned long)sf;

	/*
	 * Bug 94183, Clear the DE bit, so that when signal handler
	 * starts to run, it doesn't use BTA
	 */
	regs->status32 &= ~STATUS_DE_MASK;
	regs->status32 |= STATUS_L_MASK;

	return err;
}

static void arc_restart_syscall(struct k_sigaction *ka, struct pt_regs *regs)
{
	switch (regs->r0) {
	case -ERESTART_RESTARTBLOCK:
	case -ERESTARTNOHAND:
		/*
		 * ERESTARTNOHAND means that the syscall should
		 * only be restarted if there was no handler for
		 * the signal, and since we only get here if there
		 * is a handler, we don't restart
		 */
		regs->r0 = -EINTR;   /* ERESTART_xxx is internal */
		break;

	case -ERESTARTSYS:
		/*
		 * ERESTARTSYS means to restart the syscall if
		 * there is no handler or the handler was
		 * registered with SA_RESTART
		 */
		if (!(ka->sa.sa_flags & SA_RESTART)) {
			regs->r0 = -EINTR;
			break;
		}
		/* fallthrough */

	case -ERESTARTNOINTR:
		/*
		 * ERESTARTNOINTR means that the syscall should
		 * be called again after the signal handler returns.
		 * Setup reg state just as it was before doing the trap
		 * r0 has been clobbered with sys call ret code thus it
		 * needs to be reloaded with orig first arg to syscall
		 * in orig_r0. Rest of relevant reg-file:
		 * r8 (syscall num) and (r1 - r7) will be reset to
		 * their orig user space value when we ret from kernel
		 */
		regs->r0 = regs->orig_r0;
		regs->ret -= is_isa_arcv2() ? 2 : 4;
		break;
	}
}

/*
 * OK, we're invoking a handler
 */
static void
handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
	int failed;

	/* Set up the stack frame */
	failed = setup_rt_frame(ksig, oldset, regs);

	signal_setup_done(failed, ksig, 0);
}

void do_signal(struct pt_regs *regs)
{
	struct ksignal ksig;
	int restart_scall;

	restart_scall = in_syscall(regs) && syscall_restartable(regs);

	if (get_signal(&ksig)) {
		if (restart_scall) {
			arc_restart_syscall(&ksig.ka, regs);
			syscall_wont_restart(regs);	/* No more restarts */
		}
		handle_signal(&ksig, regs);
		return;
	}

	if (restart_scall) {
		/* No handler for syscall: restart it */
		if (regs->r0 == -ERESTARTNOHAND ||
		    regs->r0 == -ERESTARTSYS || regs->r0 == -ERESTARTNOINTR) {
			regs->r0 = regs->orig_r0;
			regs->ret -= is_isa_arcv2() ? 2 : 4;
		} else if (regs->r0 == -ERESTART_RESTARTBLOCK) {
			regs->r8 = __NR_restart_syscall;
			regs->ret -= is_isa_arcv2() ? 2 : 4;
		}
		syscall_wont_restart(regs);	/* No more restarts */
	}

	/* If there's no signal to deliver, restore the saved sigmask back */
	restore_saved_sigmask();
}

void do_notify_resume(struct pt_regs *regs)
{
	/*
	 * ASM glue gaurantees that this is only called when returning to
	 * user mode
	 */
	if (test_and_clear_thread_flag(TIF_NOTIFY_RESUME))
		tracehook_notify_resume(regs);
}
