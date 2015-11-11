/*
 * Copyright 2004-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/signal.h>
#include <linux/syscalls.h>
#include <linux/ptrace.h>
#include <linux/tty.h>
#include <linux/personality.h>
#include <linux/binfmts.h>
#include <linux/uaccess.h>
#include <linux/tracehook.h>

#include <asm/cacheflush.h>
#include <asm/ucontext.h>
#include <asm/fixed_code.h>
#include <asm/syscall.h>

/* Location of the trace bit in SYSCFG. */
#define TRACE_BITS 0x0001

struct fdpic_func_descriptor {
	unsigned long	text;
	unsigned long	GOT;
};

struct rt_sigframe {
	int sig;
	struct siginfo *pinfo;
	void *puc;
	/* This is no longer needed by the kernel, but unfortunately userspace
	 * code expects it to be there.  */
	char retcode[8];
	struct siginfo info;
	struct ucontext uc;
};

static inline int
rt_restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc, int *pr0)
{
	unsigned long usp = 0;
	int err = 0;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

#define RESTORE(x) err |= __get_user(regs->x, &sc->sc_##x)

	/* restore passed registers */
	RESTORE(r0); RESTORE(r1); RESTORE(r2); RESTORE(r3);
	RESTORE(r4); RESTORE(r5); RESTORE(r6); RESTORE(r7);
	RESTORE(p0); RESTORE(p1); RESTORE(p2); RESTORE(p3);
	RESTORE(p4); RESTORE(p5);
	err |= __get_user(usp, &sc->sc_usp);
	wrusp(usp);
	RESTORE(a0w); RESTORE(a1w);
	RESTORE(a0x); RESTORE(a1x);
	RESTORE(astat);
	RESTORE(rets);
	RESTORE(pc);
	RESTORE(retx);
	RESTORE(fp);
	RESTORE(i0); RESTORE(i1); RESTORE(i2); RESTORE(i3);
	RESTORE(m0); RESTORE(m1); RESTORE(m2); RESTORE(m3);
	RESTORE(l0); RESTORE(l1); RESTORE(l2); RESTORE(l3);
	RESTORE(b0); RESTORE(b1); RESTORE(b2); RESTORE(b3);
	RESTORE(lc0); RESTORE(lc1);
	RESTORE(lt0); RESTORE(lt1);
	RESTORE(lb0); RESTORE(lb1);
	RESTORE(seqstat);

	regs->orig_p0 = -1;	/* disable syscall checks */

	*pr0 = regs->r0;
	return err;
}

asmlinkage int sys_rt_sigreturn(void)
{
	struct pt_regs *regs = current_pt_regs();
	unsigned long usp = rdusp();
	struct rt_sigframe *frame = (struct rt_sigframe *)(usp);
	sigset_t set;
	int r0;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	if (rt_restore_sigcontext(regs, &frame->uc.uc_mcontext, &r0))
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return r0;

 badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

static inline int rt_setup_sigcontext(struct sigcontext *sc, struct pt_regs *regs)
{
	int err = 0;

#define SETUP(x) err |= __put_user(regs->x, &sc->sc_##x)

	SETUP(r0); SETUP(r1); SETUP(r2); SETUP(r3);
	SETUP(r4); SETUP(r5); SETUP(r6); SETUP(r7);
	SETUP(p0); SETUP(p1); SETUP(p2); SETUP(p3);
	SETUP(p4); SETUP(p5);
	err |= __put_user(rdusp(), &sc->sc_usp);
	SETUP(a0w); SETUP(a1w);
	SETUP(a0x); SETUP(a1x);
	SETUP(astat);
	SETUP(rets);
	SETUP(pc);
	SETUP(retx);
	SETUP(fp);
	SETUP(i0); SETUP(i1); SETUP(i2); SETUP(i3);
	SETUP(m0); SETUP(m1); SETUP(m2); SETUP(m3);
	SETUP(l0); SETUP(l1); SETUP(l2); SETUP(l3);
	SETUP(b0); SETUP(b1); SETUP(b2); SETUP(b3);
	SETUP(lc0); SETUP(lc1);
	SETUP(lt0); SETUP(lt1);
	SETUP(lb0); SETUP(lb1);
	SETUP(seqstat);

	return err;
}

static inline void *get_sigframe(struct ksignal *ksig,
				 size_t frame_size)
{
	unsigned long usp = sigsp(rdusp(), ksig);

	return (void *)((usp - frame_size) & -8UL);
}

static int
setup_rt_frame(struct ksignal *ksig, sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe *frame;
	int err = 0;

	frame = get_sigframe(ksig, sizeof(*frame));

	err |= __put_user(ksig->sig, &frame->sig);

	err |= __put_user(&frame->info, &frame->pinfo);
	err |= __put_user(&frame->uc, &frame->puc);
	err |= copy_siginfo_to_user(&frame->info, &ksig->info);

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __save_altstack(&frame->uc.uc_stack, rdusp());
	err |= rt_setup_sigcontext(&frame->uc.uc_mcontext, regs);
	err |= copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	if (err)
		return -EFAULT;

	/* Set up registers for signal handler */
	if (current->personality & FDPIC_FUNCPTRS) {
		struct fdpic_func_descriptor __user *funcptr =
			(struct fdpic_func_descriptor *) ksig->ka.sa.sa_handler;
		u32 pc, p3;
		err |= __get_user(pc, &funcptr->text);
		err |= __get_user(p3, &funcptr->GOT);
		if (err)
			return -EFAULT;
		regs->pc = pc;
		regs->p3 = p3;
	} else
		regs->pc = (unsigned long)ksig->ka.sa.sa_handler;
	wrusp((unsigned long)frame);
	regs->rets = SIGRETURN_STUB;

	regs->r0 = frame->sig;
	regs->r1 = (unsigned long)(&frame->info);
	regs->r2 = (unsigned long)(&frame->uc);

	return 0;
}

static inline void
handle_restart(struct pt_regs *regs, struct k_sigaction *ka, int has_handler)
{
	switch (regs->r0) {
	case -ERESTARTNOHAND:
		if (!has_handler)
			goto do_restart;
		regs->r0 = -EINTR;
		break;

	case -ERESTARTSYS:
		if (has_handler && !(ka->sa.sa_flags & SA_RESTART)) {
			regs->r0 = -EINTR;
			break;
		}
		/* fallthrough */
	case -ERESTARTNOINTR:
 do_restart:
		regs->p0 = regs->orig_p0;
		regs->r0 = regs->orig_r0;
		regs->pc -= 2;
		break;

	case -ERESTART_RESTARTBLOCK:
		regs->p0 = __NR_restart_syscall;
		regs->pc -= 2;
		break;
	}
}

/*
 * OK, we're invoking a handler
 */
static void
handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	int ret;

	/* are we from a system call? to see pt_regs->orig_p0 */
	if (regs->orig_p0 >= 0)
		/* If so, check system call restarting.. */
		handle_restart(regs, &ksig->ka, 1);

	/* set up the stack frame */
	ret = setup_rt_frame(ksig, sigmask_to_save(), regs);

	signal_setup_done(ret, ksig, test_thread_flag(TIF_SINGLESTEP));
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals
 * that the kernel can handle, and then we build all the user-level signal
 * handling stack-frames in one go after that.
 */
asmlinkage void do_signal(struct pt_regs *regs)
{
	struct ksignal ksig;

	current->thread.esp0 = (unsigned long)regs;

	if (get_signal(&ksig)) {
		/* Whee!  Actually deliver the signal.  */
		handle_signal(&ksig, regs);
		return;
	}

	/* Did we come from a system call? */
	if (regs->orig_p0 >= 0)
		/* Restart the system call - no handlers present */
		handle_restart(regs, NULL, 0);

	/* if there's no signal to deliver, we just put the saved sigmask
	 * back */
	restore_saved_sigmask();
}

/*
 * notification of userspace execution resumption
 */
asmlinkage void do_notify_resume(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SIGPENDING))
		do_signal(regs);

	if (test_thread_flag(TIF_NOTIFY_RESUME)) {
		clear_thread_flag(TIF_NOTIFY_RESUME);
		tracehook_notify_resume(regs);
	}
}

