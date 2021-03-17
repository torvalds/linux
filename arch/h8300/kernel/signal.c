/*
 *  linux/arch/h8300/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * uClinux H8/300 support by Yoshinori Sato <ysato@users.sourceforge.jp>
 *                and David McCullough <davidm@snapgear.com>
 *
 * Based on
 * Linux/m68k by Hamish Macdonald
 */

/*
 * ++roman (07/09/96): implemented signal stacks (specially for tosemu on
 * Atari :-) Current limitation: Only one sigstack can be active at one time.
 * If a second signal with SA_ONSTACK set arrives while working on a sigstack,
 * SA_ONSTACK is ignored. This behaviour avoids lots of trouble with nested
 * signal handlers!
 */

#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/syscalls.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/highuid.h>
#include <linux/personality.h>
#include <linux/tty.h>
#include <linux/binfmts.h>
#include <linux/tracehook.h>

#include <asm/setup.h>
#include <linux/uaccess.h>
#include <asm/traps.h>
#include <asm/ucontext.h>

/*
 * Do a signal return; undo the signal stack.
 *
 * Keep the return code on the stack quadword aligned!
 * That makes the cache flush below easier.
 */

struct rt_sigframe {
	long dummy_er0;
	long dummy_vector;
#if defined(CONFIG_CPU_H8S)
	short dummy_exr;
#endif
	long dummy_pc;
	char *pretcode;
	struct siginfo *pinfo;
	void *puc;
	unsigned char retcode[8];
	struct siginfo info;
	struct ucontext uc;
	int sig;
} __packed __aligned(2);

static inline int
restore_sigcontext(struct sigcontext *usc, int *pd0)
{
	struct pt_regs *regs = current_pt_regs();
	int err = 0;
	unsigned int ccr;
	unsigned int usp;
	unsigned int er0;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	/* restore passed registers */
#define COPY(r)  do { err |= get_user(regs->r, &usc->sc_##r); } while (0)
	COPY(er1);
	COPY(er2);
	COPY(er3);
	COPY(er5);
	COPY(pc);
	ccr = regs->ccr & 0x10;
	COPY(ccr);
#undef COPY
	regs->ccr &= 0xef;
	regs->ccr |= ccr;
	regs->orig_er0 = -1;		/* disable syscall checks */
	err |= __get_user(usp, &usc->sc_usp);
	regs->sp = usp;

	err |= __get_user(er0, &usc->sc_er0);
	*pd0 = er0;
	return err;
}

asmlinkage int sys_rt_sigreturn(void)
{
	unsigned long usp = rdusp();
	struct rt_sigframe *frame = (struct rt_sigframe *)(usp - 4);
	sigset_t set;
	int er0;

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	if (restore_sigcontext(&frame->uc.uc_mcontext, &er0))
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return er0;

badframe:
	force_sig(SIGSEGV);
	return 0;
}

static int setup_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs,
			     unsigned long mask)
{
	int err = 0;

	err |= __put_user(regs->er0, &sc->sc_er0);
	err |= __put_user(regs->er1, &sc->sc_er1);
	err |= __put_user(regs->er2, &sc->sc_er2);
	err |= __put_user(regs->er3, &sc->sc_er3);
	err |= __put_user(regs->er4, &sc->sc_er4);
	err |= __put_user(regs->er5, &sc->sc_er5);
	err |= __put_user(regs->er6, &sc->sc_er6);
	err |= __put_user(rdusp(),   &sc->sc_usp);
	err |= __put_user(regs->pc,  &sc->sc_pc);
	err |= __put_user(regs->ccr, &sc->sc_ccr);
	err |= __put_user(mask,      &sc->sc_mask);

	return err;
}

static inline void __user *
get_sigframe(struct ksignal *ksig, struct pt_regs *regs, size_t frame_size)
{
	return (void __user *)((sigsp(rdusp(), ksig) - frame_size) & -8UL);
}

static int setup_rt_frame(struct ksignal *ksig, sigset_t *set,
			  struct pt_regs *regs)
{
	struct rt_sigframe *frame;
	int err = 0;
	unsigned char *ret;

	frame = get_sigframe(ksig, regs, sizeof(*frame));

	if (!access_ok(frame, sizeof(*frame)))
		return -EFAULT;

	if (ksig->ka.sa.sa_flags & SA_SIGINFO)
		err |= copy_siginfo_to_user(&frame->info, &ksig->info);

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __save_altstack(&frame->uc.uc_stack, rdusp());
	err |= setup_sigcontext(&frame->uc.uc_mcontext, regs, set->sig[0]);
	err |= copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		return -EFAULT;

	/* Set up to return from userspace.  */
	ret = (unsigned char *)&frame->retcode;
	if (ksig->ka.sa.sa_flags & SA_RESTORER)
		ret = (unsigned char *)(ksig->ka.sa.sa_restorer);
	else {
		/* sub.l er0,er0; mov.b #__NR_rt_sigreturn,r0l; trapa #0 */
		err |= __put_user(0x1a80f800 + (__NR_rt_sigreturn & 0xff),
				  (unsigned long *)(frame->retcode + 0));
		err |= __put_user(0x5700,
				  (unsigned short *)(frame->retcode + 4));
	}
	err |= __put_user(ret, &frame->pretcode);

	if (err)
		return -EFAULT;

	/* Set up registers for signal handler */
	regs->sp  = (unsigned long)frame;
	regs->pc  = (unsigned long)ksig->ka.sa.sa_handler;
	regs->er0 = ksig->sig;
	regs->er1 = (unsigned long)&(frame->info);
	regs->er2 = (unsigned long)&frame->uc;
	regs->er5 = current->mm->start_data;	/* GOT base */

	return 0;
}

static void
handle_restart(struct pt_regs *regs, struct k_sigaction *ka)
{
	switch (regs->er0) {
	case -ERESTARTNOHAND:
		if (!ka)
			goto do_restart;
		regs->er0 = -EINTR;
		break;
	case -ERESTART_RESTARTBLOCK:
		if (!ka) {
			regs->er0 = __NR_restart_syscall;
			regs->pc -= 2;
		} else
			regs->er0 = -EINTR;
		break;
	case -ERESTARTSYS:
		if (!(ka->sa.sa_flags & SA_RESTART)) {
			regs->er0 = -EINTR;
			break;
		}
		fallthrough;
	case -ERESTARTNOINTR:
do_restart:
		regs->er0 = regs->orig_er0;
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
	sigset_t *oldset = sigmask_to_save();
	int ret;
	/* are we from a system call? */
	if (regs->orig_er0 >= 0)
		handle_restart(regs, &ksig->ka);

	ret = setup_rt_frame(ksig, oldset, regs);

	signal_setup_done(ret, ksig, 0);
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
static void do_signal(struct pt_regs *regs)
{
	struct ksignal ksig;

	current->thread.esp0 = (unsigned long) regs;

	if (get_signal(&ksig)) {
		/* Whee!  Actually deliver the signal.  */
		handle_signal(&ksig, regs);
		return;
	}
	/* Did we come from a system call? */
	if (regs->orig_er0 >= 0)
		handle_restart(regs, NULL);

	/* If there's no signal to deliver, we just restore the saved mask.  */
	restore_saved_sigmask();
}

asmlinkage void do_notify_resume(struct pt_regs *regs, u32 thread_info_flags)
{
	if (thread_info_flags & _TIF_SIGPENDING)
		do_signal(regs);

	if (thread_info_flags & _TIF_NOTIFY_RESUME)
		tracehook_notify_resume(regs);
}
