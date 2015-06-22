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

static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc)
{
	unsigned int err = 0;
	unsigned long old_usp;

        /* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	/*
	 * Restore the registers from &sc->regs. sc is already checked
	 * for VERIFY_READ since the signal_frame was previously
	 * checked in sys_sigreturn().
	 */
	if (__copy_from_user(regs, sc, sizeof(struct pt_regs)))
		goto badframe;

	/* Make that the user-mode flag is set. */
	regs->ccs |= (1 << (U_CCS_BITNR + CCS_SHIFT));

	/* Don't perform syscall restarting */
	regs->exs = -1;

	/* Restore the old USP. */
	err |= __get_user(old_usp, &sc->usp);
	wrusp(old_usp);

	return err;

badframe:
	return 1;
}

asmlinkage int sys_sigreturn(void)
{
	struct pt_regs *regs = current_pt_regs();
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

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->sc))
		goto badframe;

	keep_debug_flags(oldccs, oldspc, regs);

	return regs->r10;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage int sys_rt_sigreturn(void)
{
	struct pt_regs *regs = current_pt_regs();
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

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
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
get_sigframe(struct ksignal *ksig, size_t frame_size)
{
	unsigned long sp = sigsp(rdusp(), ksig);

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
setup_frame(struct ksignal *ksig, sigset_t *set, struct pt_regs *regs)
{
	int err;
	unsigned long return_ip;
	struct signal_frame __user *frame;

	err = 0;
	frame = get_sigframe(ksig, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		return -EFAULT;

	err |= setup_sigcontext(&frame->sc, regs, set->sig[0]);

	if (err)
		return -EFAULT;

	if (_NSIG_WORDS > 1) {
		err |= __copy_to_user(frame->extramask, &set->sig[1],
				      sizeof(frame->extramask));
	}

	if (err)
		return -EFAULT;

	/*
	 * Set up to return from user-space. If provided, use a stub
	 * already located in user-space.
	 */
	if (ksig->ka.sa.sa_flags & SA_RESTORER) {
		return_ip = (unsigned long)ksig->ka.sa.sa_restorer;
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
		return -EFAULT;

	/*
	 * Set up registers for signal handler.
	 *
	 * Where the code enters now.
	 * Where the code enter later.
	 * First argument, signo.
	 */
	regs->erp = (unsigned long) ksig->ka.sa.sa_handler;
	regs->srp = return_ip;
	regs->r10 = ksig->sig;

	/* Actually move the USP to reflect the stacked frame. */
	wrusp((unsigned long)frame);

	return 0;
}

static int
setup_rt_frame(struct ksignal *ksig, sigset_t *set, struct pt_regs *regs)
{
	int err;
	unsigned long return_ip;
	struct rt_signal_frame __user *frame;

	err = 0;
	frame = get_sigframe(ksig, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		return -EFAULT;

	err |= __put_user(&frame->info, &frame->pinfo);
	err |= __put_user(&frame->uc, &frame->puc);
	err |= copy_siginfo_to_user(&frame->info, &ksig->info);

	if (err)
		return -EFAULT;

	/* Clear all the bits of the ucontext we don't use.  */
	err |= __clear_user(&frame->uc, offsetof(struct ucontext, uc_mcontext));
	err |= setup_sigcontext(&frame->uc.uc_mcontext, regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	err |= __save_altstack(&frame->uc.uc_stack, rdusp());

	if (err)
		return -EFAULT;

	/*
	 * Set up to return from user-space. If provided, use a stub
	 * already located in user-space.
	 */
	if (ksig->ka.sa.sa_flags & SA_RESTORER) {
		return_ip = (unsigned long) ksig->ka.sa.sa_restorer;
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
		return -EFAULT;

	/*
	 * Set up registers for signal handler.
	 *
	 * Where the code enters now.
	 * Where the code enters later.
	 * First argument is signo.
	 * Second argument is (siginfo_t *).
	 * Third argument is unused.
	 */
	regs->erp = (unsigned long) ksig->ka.sa.sa_handler;
	regs->srp = return_ip;
	regs->r10 = ksig->sig;
	regs->r11 = (unsigned long) &frame->info;
	regs->r12 = 0;

	/* Actually move the usp to reflect the stacked frame. */
	wrusp((unsigned long)frame);

	return 0;
}

/* Invoke a signal handler to, well, handle the signal. */
static inline void
handle_signal(int canrestart, struct ksignal *ksig, struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
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
				if (!(ksig->ka.sa.sa_flags & SA_RESTART)) {
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
	if (ksig->ka.sa.sa_flags & SA_SIGINFO)
		ret = setup_rt_frame(ksig, oldset, regs);
	else
		ret = setup_frame(ksig, oldset, regs);

	signal_setup_done(ret, ksig, 0);
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
	struct ksignal ksig;

	canrestart = canrestart && ((int)regs->exs >= 0);

	/*
	 * The common case should go fast, which is why this point is
	 * reached from kernel-mode. If that's the case, just return
	 * without doing anything.
	 */
	if (!user_mode(regs))
		return;

	if (get_signal(&ksig)) {
		/* Whee!  Actually deliver the signal.  */
		handle_signal(canrestart, &ksig, regs);
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
	restore_saved_sigmask();
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
