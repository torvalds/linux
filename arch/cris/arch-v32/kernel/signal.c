/*
 * Copyright (C) 2003, Axis Communications AB.
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/syscalls.h>
#include <linux/vmalloc.h>

#include <asm/io.h>
#include <asm/processor.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <arch/ptrace.h>
#include <arch/hwregs/cpu_vect.h>

extern unsigned long cris_signal_return_page;

/* Flag to check if a signal is blockable. */
#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

/*
 * A syscall in CRIS is really a "break 13" instruction, which is 2
 * bytes. The registers is manipulated so upon return the instruction
 * will be executed again.
 *
 * This relies on that PC points to the instruction after the break call.
 */
#define RESTART_CRIS_SYS(regs) regs->r10 = regs->orig_r10; regs->erp -= 2;

/* Signal frames. */
struct signal_frame {
	struct sigcontext sc;
	unsigned long extramask[_NSIG_WORDS - 1];
	unsigned char retcode[8];	/* Trampoline code. */
};

struct rt_signal_frame {
	struct siginfo *pinfo;
	void *puc;
	struct siginfo info;
	struct ucontext uc;
	unsigned char retcode[8];	/* Trampoline code. */
};

void do_signal(int restart, struct pt_regs *regs);
void keep_debug_flags(unsigned long oldccs, unsigned long oldspc,
		      struct pt_regs *regs);
/*
 * Swap in the new signal mask, and wait for a signal. Define some
 * dummy arguments to be able to reach the regs argument.
 */
int
sys_sigsuspend(old_sigset_t mask)
{
	sigset_t blocked;
	siginitset(&blocked, mask);
	return sigsuspend(&blocked);
}

int
sys_sigaction(int signal, const struct old_sigaction *act,
	      struct old_sigaction *oact)
{
	int retval;
	struct k_sigaction newk;
	struct k_sigaction oldk;

	if (act) {
		old_sigset_t mask;

		if (!access_ok(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(newk.sa.sa_handler, &act->sa_handler) ||
		    __get_user(newk.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;

		__get_user(newk.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&newk.sa.sa_mask, mask);
	}

	retval = do_sigaction(signal, act ? &newk : NULL, oact ? &oldk : NULL);

	if (!retval && oact) {
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(oldk.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(oldk.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;

		__put_user(oldk.sa.sa_flags, &oact->sa_flags);
		__put_user(oldk.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return retval;
}

int
sys_sigaltstack(const stack_t __user *uss, stack_t __user *uoss)
{
	return do_sigaltstack(uss, uoss, rdusp());
}

static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc)
{
	unsigned int err = 0;
	unsigned long old_usp;

        /* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	/*
	 * Restore the registers from &sc->regs. sc is already checked
	 * for VERIFY_READ since the signal_frame was previously
	 * checked in sys_sigreturn().
	 */
	if (__copy_from_user(regs, sc, sizeof(struct pt_regs)))
		goto badframe;

	/* Make that the user-mode flag is set. */
	regs->ccs |= (1 << (U_CCS_BITNR + CCS_SHIFT));

	/* Restore the old USP. */
	err |= __get_user(old_usp, &sc->usp);
	wrusp(old_usp);

	return err;

badframe:
	return 1;
}

/* Define some dummy arguments to be able to reach the regs argument. */
asmlinkage int
sys_sigreturn(long r10, long r11, long r12, long r13, long mof, long srp,
	      struct pt_regs *regs)
{
	sigset_t set;
	struct signal_frame __user *frame;
	unsigned long oldspc = regs->spc;
	unsigned long oldccs = regs->ccs;

	frame = (struct signal_frame *) rdusp();

	/*
	 * Since the signal is stacked on a dword boundary, the frame
	 * should be dword aligned here as well. It it's not, then the
	 * user is trying some funny business.
	 */
	if (((long)frame) & 3)
		goto badframe;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;

	if (__get_user(set.sig[0], &frame->sc.oldmask) ||
	    (_NSIG_WORDS > 1 && __copy_from_user(&set.sig[1],
						 frame->extramask,
						 sizeof(frame->extramask))))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->sc))
		goto badframe;

	keep_debug_flags(oldccs, oldspc, regs);

	return regs->r10;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

/* Define some dummy variables to be able to reach the regs argument. */
asmlinkage int
sys_rt_sigreturn(long r10, long r11, long r12, long r13, long mof, long srp,
		 struct pt_regs *regs)
{
	sigset_t set;
	struct rt_signal_frame __user *frame;
	unsigned long oldspc = regs->spc;
	unsigned long oldccs = regs->ccs;

	frame = (struct rt_signal_frame *) rdusp();

	/*
	 * Since the signal is stacked on a dword boundary, the frame
	 * should be dword aligned here as well. It it's not, then the
	 * user is trying some funny business.
	 */
	if (((long)frame) & 3)
		goto badframe;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;

	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (do_sigaltstack(&frame->uc.uc_stack, NULL, rdusp()) == -EFAULT)
		goto badframe;

	keep_debug_flags(oldccs, oldspc, regs);

	return regs->r10;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

/* Setup a signal frame. */
static int
setup_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs,
		 unsigned long mask)
{
	int err;
	unsigned long usp;

	err = 0;
	usp = rdusp();

	/*
	 * Copy the registers. They are located first in sc, so it's
	 * possible to use sc directly.
	 */
	err |= __copy_to_user(sc, regs, sizeof(struct pt_regs));

	err |= __put_user(mask, &sc->oldmask);
	err |= __put_user(usp, &sc->usp);

	return err;
}

/* Figure out where to put the new signal frame - usually on the stack. */
static inline void __user *
get_sigframe(struct k_sigaction *ka, struct pt_regs * regs, size_t frame_size)
{
	unsigned long sp;

	sp = rdusp();

	/* This is the X/Open sanctioned signal stack switching. */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (!on_sig_stack(sp))
			sp = current->sas_ss_sp + current->sas_ss_size;
	}

	/* Make sure the frame is dword-aligned. */
	sp &= ~3;

	return (void __user *)(sp - frame_size);
}

/* Grab and setup a signal frame.
 *
 * Basically a lot of state-info is stacked, and arranged for the
 * user-mode program to return to the kernel using either a trampiline
 * which performs the syscall sigreturn(), or a provided user-mode
 * trampoline.
  */
static int
setup_frame(int sig, struct k_sigaction *ka,  sigset_t *set,
	    struct pt_regs * regs)
{
	int err;
	unsigned long return_ip;
	struct signal_frame __user *frame;

	err = 0;
	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	err |= setup_sigcontext(&frame->sc, regs, set->sig[0]);

	if (err)
		goto give_sigsegv;

	if (_NSIG_WORDS > 1) {
		err |= __copy_to_user(frame->extramask, &set->sig[1],
				      sizeof(frame->extramask));
	}

	if (err)
		goto give_sigsegv;

	/*
	 * Set up to return from user-space. If provided, use a stub
	 * already located in user-space.
	 */
	if (ka->sa.sa_flags & SA_RESTORER) {
		return_ip = (unsigned long)ka->sa.sa_restorer;
	} else {
		/* Trampoline - the desired return ip is in the signal return page. */
		return_ip = cris_signal_return_page;

		/*
		 * This is movu.w __NR_sigreturn, r9; break 13;
		 *
		 * WE DO NOT USE IT ANY MORE! It's only left here for historical
		 * reasons and because gdb uses it as a signature to notice
		 * signal handler stack frames.
		 */
		err |= __put_user(0x9c5f,         (short __user*)(frame->retcode+0));
		err |= __put_user(__NR_sigreturn, (short __user*)(frame->retcode+2));
		err |= __put_user(0xe93d,         (short __user*)(frame->retcode+4));
	}

	if (err)
		goto give_sigsegv;

	/*
	 * Set up registers for signal handler.
	 *
	 * Where the code enters now.
	 * Where the code enter later.
	 * First argument, signo.
	 */
	regs->erp = (unsigned long) ka->sa.sa_handler;
	regs->srp = return_ip;
	regs->r10 = sig;

	/* Actually move the USP to reflect the stacked frame. */
	wrusp((unsigned long)frame);

	return 0;

give_sigsegv:
	force_sigsegv(sig, current);
	return -EFAULT;
}

static int
setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
	       sigset_t *set, struct pt_regs * regs)
{
	int err;
	unsigned long return_ip;
	struct rt_signal_frame __user *frame;

	err = 0;
	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	/* TODO: what is the current->exec_domain stuff and invmap ? */

	err |= __put_user(&frame->info, &frame->pinfo);
	err |= __put_user(&frame->uc, &frame->puc);
	err |= copy_siginfo_to_user(&frame->info, info);

	if (err)
		goto give_sigsegv;

	/* Clear all the bits of the ucontext we don't use.  */
	err |= __clear_user(&frame->uc, offsetof(struct ucontext, uc_mcontext));
	err |= setup_sigcontext(&frame->uc.uc_mcontext, regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	if (err)
		goto give_sigsegv;

	/*
	 * Set up to return from user-space. If provided, use a stub
	 * already located in user-space.
	 */
	if (ka->sa.sa_flags & SA_RESTORER) {
		return_ip = (unsigned long) ka->sa.sa_restorer;
	} else {
		/* Trampoline - the desired return ip is in the signal return page. */
		return_ip = cris_signal_return_page + 6;

		/*
		 * This is movu.w __NR_rt_sigreturn, r9; break 13;
		 *
		 * WE DO NOT USE IT ANY MORE! It's only left here for historical
		 * reasons and because gdb uses it as a signature to notice
		 * signal handler stack frames.
		 */
		err |= __put_user(0x9c5f, (short __user*)(frame->retcode+0));

		err |= __put_user(__NR_rt_sigreturn,
				  (short __user*)(frame->retcode+2));

		err |= __put_user(0xe93d, (short __user*)(frame->retcode+4));
	}

	if (err)
		goto give_sigsegv;

	/*
	 * Set up registers for signal handler.
	 *
	 * Where the code enters now.
	 * Where the code enters later.
	 * First argument is signo.
	 * Second argument is (siginfo_t *).
	 * Third argument is unused.
	 */
	regs->erp = (unsigned long) ka->sa.sa_handler;
	regs->srp = return_ip;
	regs->r10 = sig;
	regs->r11 = (unsigned long) &frame->info;
	regs->r12 = 0;

	/* Actually move the usp to reflect the stacked frame. */
	wrusp((unsigned long)frame);

	return 0;

give_sigsegv:
	force_sigsegv(sig, current);
	return -EFAULT;
}

/* Invoke a signal handler to, well, handle the signal. */
static inline int
handle_signal(int canrestart, unsigned long sig,
	      siginfo_t *info, struct k_sigaction *ka,
              sigset_t *oldset, struct pt_regs * regs)
{
	int ret;

	/* Check if this got called from a system call. */
	if (canrestart) {
		/* If so, check system call restarting. */
		switch (regs->r10) {
			case -ERESTART_RESTARTBLOCK:
			case -ERESTARTNOHAND:
				/*
				 * This means that the syscall should
				 * only be restarted if there was no
				 * handler for the signal, and since
				 * this point isn't reached unless
				 * there is a handler, there's no need
				 * to restart.
				 */
				regs->r10 = -EINTR;
				break;

                        case -ERESTARTSYS:
				/*
				 * This means restart the syscall if
                                 * there is no handler, or the handler
                                 * was registered with SA_RESTART.
				 */
				if (!(ka->sa.sa_flags & SA_RESTART)) {
					regs->r10 = -EINTR;
					break;
				}

				/* Fall through. */

			case -ERESTARTNOINTR:
				/*
				 * This means that the syscall should
                                 * be called again after the signal
                                 * handler returns.
				 */
				RESTART_CRIS_SYS(regs);
				break;
                }
        }

	/* Set up the stack frame. */
	if (ka->sa.sa_flags & SA_SIGINFO)
		ret = setup_rt_frame(sig, ka, info, oldset, regs);
	else
		ret = setup_frame(sig, ka, oldset, regs);

	if (ret == 0)
		block_sigmask(ka, sig);

	return ret;
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Also note that the regs structure given here as an argument, is the latest
 * pushed pt_regs. It may or may not be the same as the first pushed registers
 * when the initial usermode->kernelmode transition took place. Therefore
 * we can use user_mode(regs) to see if we came directly from kernel or user
 * mode below.
 */
void
do_signal(int canrestart, struct pt_regs *regs)
{
	int signr;
	siginfo_t info;
        struct k_sigaction ka;
	sigset_t *oldset;

	/*
	 * The common case should go fast, which is why this point is
	 * reached from kernel-mode. If that's the case, just return
	 * without doing anything.
	 */
	if (!user_mode(regs))
		return;

	if (test_thread_flag(TIF_RESTORE_SIGMASK))
		oldset = &current->saved_sigmask;
	else
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);

	if (signr > 0) {
		/* Whee!  Actually deliver the signal.  */
		if (handle_signal(canrestart, signr, &info, &ka,
				oldset, regs)) {
			/* a signal was successfully delivered; the saved
			 * sigmask will have been stored in the signal frame,
			 * and will be restored by sigreturn, so we can simply
			 * clear the TIF_RESTORE_SIGMASK flag */
			if (test_thread_flag(TIF_RESTORE_SIGMASK))
				clear_thread_flag(TIF_RESTORE_SIGMASK);
		}

		return;
	}

	/* Got here from a system call? */
	if (canrestart) {
		/* Restart the system call - no handlers present. */
		if (regs->r10 == -ERESTARTNOHAND ||
		    regs->r10 == -ERESTARTSYS ||
		    regs->r10 == -ERESTARTNOINTR) {
			RESTART_CRIS_SYS(regs);
		}

		if (regs->r10 == -ERESTART_RESTARTBLOCK){
			regs->r9 = __NR_restart_syscall;
			regs->erp -= 2;
		}
	}

	/* if there's no signal to deliver, we just put the saved sigmask
	 * back */
	if (test_thread_flag(TIF_RESTORE_SIGMASK)) {
		clear_thread_flag(TIF_RESTORE_SIGMASK);
		sigprocmask(SIG_SETMASK, &current->saved_sigmask, NULL);
	}
}

asmlinkage void
ugdb_trap_user(struct thread_info *ti, int sig)
{
	if (((user_regs(ti)->exs & 0xff00) >> 8) != SINGLE_STEP_INTR_VECT) {
		/* Zero single-step PC if the reason we stopped wasn't a single
		   step exception. This is to avoid relying on it when it isn't
		   reliable. */
		user_regs(ti)->spc = 0;
	}
	/* FIXME: Filter out false h/w breakpoint hits (i.e. EDA
	   not within any configured h/w breakpoint range). Synchronize with
	   what already exists for kernel debugging.  */
	if (((user_regs(ti)->exs & 0xff00) >> 8) == BREAK_8_INTR_VECT) {
		/* Break 8: subtract 2 from ERP unless in a delay slot. */
		if (!(user_regs(ti)->erp & 0x1))
			user_regs(ti)->erp -= 2;
	}
	sys_kill(ti->task->pid, sig);
}

void
keep_debug_flags(unsigned long oldccs, unsigned long oldspc,
		 struct pt_regs *regs)
{
	if (oldccs & (1 << Q_CCS_BITNR)) {
		/* Pending single step due to single-stepping the break 13
		   in the signal trampoline: keep the Q flag. */
		regs->ccs |= (1 << Q_CCS_BITNR);
		/* S flag should be set - complain if it's not. */
		if (!(oldccs & (1 << (S_CCS_BITNR + CCS_SHIFT)))) {
			printk("Q flag but no S flag?");
		}
		regs->ccs |= (1 << (S_CCS_BITNR + CCS_SHIFT));
		/* Assume the SPC is valid and interesting. */
		regs->spc = oldspc;

	} else if (oldccs & (1 << (S_CCS_BITNR + CCS_SHIFT))) {
		/* If a h/w bp was set in the signal handler we need
		   to keep the S flag. */
		regs->ccs |= (1 << (S_CCS_BITNR + CCS_SHIFT));
		/* Don't keep the old SPC though; if we got here due to
		   a single-step, the Q flag should have been set. */
	} else if (regs->spc) {
		/* If we were single-stepping *before* the signal was taken,
		   we don't want to restore that state now, because GDB will
		   have forgotten all about it. */
		regs->spc = 0;
		regs->ccs &= ~(1 << (S_CCS_BITNR + CCS_SHIFT));
	}
}

/* Set up the trampolines on the signal return page. */
int __init
cris_init_signal(void)
{
	u16* data = kmalloc(PAGE_SIZE, GFP_KERNEL);

	/* This is movu.w __NR_sigreturn, r9; break 13; */
	data[0] = 0x9c5f;
	data[1] = __NR_sigreturn;
	data[2] = 0xe93d;
	/* This is movu.w __NR_rt_sigreturn, r9; break 13; */
	data[3] = 0x9c5f;
	data[4] = __NR_rt_sigreturn;
	data[5] = 0xe93d;

	/* Map to userspace with appropriate permissions (no write access...) */
	cris_signal_return_page = (unsigned long)
          __ioremap_prot(virt_to_phys(data), PAGE_SIZE, PAGE_SIGNAL_TRAMPOLINE);

	return 0;
}

__initcall(cris_init_signal);
