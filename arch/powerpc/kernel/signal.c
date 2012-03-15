/*
 * Common signal handling code for both 32 and 64 bits
 *
 *    Copyright (c) 2007 Benjamin Herrenschmidt, IBM Coproration
 *    Extracted from signal_32.c and signal_64.c
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
 * this archive for more details.
 */

#include <linux/tracehook.h>
#include <linux/signal.h>
#include <linux/key.h>
#include <asm/hw_breakpoint.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>

#include "signal.h"

/* Log an error when sending an unhandled signal to a process. Controlled
 * through debug.exception-trace sysctl.
 */

int show_unhandled_signals = 0;

/*
 * Allocate space for the signal frame
 */
void __user * get_sigframe(struct k_sigaction *ka, struct pt_regs *regs,
			   size_t frame_size, int is_32)
{
        unsigned long oldsp, newsp;

        /* Default to using normal stack */
        oldsp = get_clean_sp(regs, is_32);

	/* Check for alt stack */
	if ((ka->sa.sa_flags & SA_ONSTACK) &&
	    current->sas_ss_size && !on_sig_stack(oldsp))
		oldsp = (current->sas_ss_sp + current->sas_ss_size);

	/* Get aligned frame */
	newsp = (oldsp - frame_size) & ~0xFUL;

	/* Check access */
	if (!access_ok(VERIFY_WRITE, (void __user *)newsp, oldsp - newsp))
		return NULL;

        return (void __user *)newsp;
}


/*
 * Restore the user process's signal mask
 */
void restore_sigmask(sigset_t *set)
{
	sigdelsetmask(set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = *set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
}

static void check_syscall_restart(struct pt_regs *regs, struct k_sigaction *ka,
				  int has_handler)
{
	unsigned long ret = regs->gpr[3];
	int restart = 1;

	/* syscall ? */
	if (TRAP(regs) != 0x0C00)
		return;

	/* error signalled ? */
	if (!(regs->ccr & 0x10000000))
		return;

	switch (ret) {
	case ERESTART_RESTARTBLOCK:
	case ERESTARTNOHAND:
		/* ERESTARTNOHAND means that the syscall should only be
		 * restarted if there was no handler for the signal, and since
		 * we only get here if there is a handler, we dont restart.
		 */
		restart = !has_handler;
		break;
	case ERESTARTSYS:
		/* ERESTARTSYS means to restart the syscall if there is no
		 * handler or the handler was registered with SA_RESTART
		 */
		restart = !has_handler || (ka->sa.sa_flags & SA_RESTART) != 0;
		break;
	case ERESTARTNOINTR:
		/* ERESTARTNOINTR means that the syscall should be
		 * called again after the signal handler returns.
		 */
		break;
	default:
		return;
	}
	if (restart) {
		if (ret == ERESTART_RESTARTBLOCK)
			regs->gpr[0] = __NR_restart_syscall;
		else
			regs->gpr[3] = regs->orig_gpr3;
		regs->nip -= 4;
		regs->result = 0;
	} else {
		regs->result = -EINTR;
		regs->gpr[3] = EINTR;
		regs->ccr |= 0x10000000;
	}
}

static int do_signal(struct pt_regs *regs)
{
	sigset_t *oldset;
	siginfo_t info;
	int signr;
	struct k_sigaction ka;
	int ret;
	int is32 = is_32bit_task();

	if (current_thread_info()->local_flags & _TLF_RESTORE_SIGMASK)
		oldset = &current->saved_sigmask;
	else
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);

	/* Is there any syscall restart business here ? */
	check_syscall_restart(regs, &ka, signr > 0);

	if (signr <= 0) {
		struct thread_info *ti = current_thread_info();
		/* No signal to deliver -- put the saved sigmask back */
		if (ti->local_flags & _TLF_RESTORE_SIGMASK) {
			ti->local_flags &= ~_TLF_RESTORE_SIGMASK;
			sigprocmask(SIG_SETMASK, &current->saved_sigmask, NULL);
		}
		regs->trap = 0;
		return 0;               /* no signals delivered */
	}

#ifndef CONFIG_PPC_ADV_DEBUG_REGS
        /*
	 * Reenable the DABR before delivering the signal to
	 * user space. The DABR will have been cleared if it
	 * triggered inside the kernel.
	 */
	if (current->thread.dabr)
		set_dabr(current->thread.dabr);
#endif
	/* Re-enable the breakpoints for the signal stack */
	thread_change_pc(current, regs);

	if (is32) {
        	if (ka.sa.sa_flags & SA_SIGINFO)
			ret = handle_rt_signal32(signr, &ka, &info, oldset,
					regs);
		else
			ret = handle_signal32(signr, &ka, &info, oldset,
					regs);
	} else {
		ret = handle_rt_signal64(signr, &ka, &info, oldset, regs);
	}

	regs->trap = 0;
	if (ret) {
		spin_lock_irq(&current->sighand->siglock);
		sigorsets(&current->blocked, &current->blocked,
			  &ka.sa.sa_mask);
		if (!(ka.sa.sa_flags & SA_NODEFER))
			sigaddset(&current->blocked, signr);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);

		/*
		 * A signal was successfully delivered; the saved sigmask is in
		 * its frame, and we can clear the TLF_RESTORE_SIGMASK flag.
		 */
		current_thread_info()->local_flags &= ~_TLF_RESTORE_SIGMASK;

		/*
		 * Let tracing know that we've done the handler setup.
		 */
		tracehook_signal_handler(signr, &info, &ka, regs,
					 test_thread_flag(TIF_SINGLESTEP));
	}

	return ret;
}

void do_notify_resume(struct pt_regs *regs, unsigned long thread_info_flags)
{
	if (thread_info_flags & _TIF_SIGPENDING)
		do_signal(regs);

	if (thread_info_flags & _TIF_NOTIFY_RESUME) {
		clear_thread_flag(TIF_NOTIFY_RESUME);
		tracehook_notify_resume(regs);
		if (current->replacement_session_keyring)
			key_replace_session_keyring();
	}
}

long sys_sigaltstack(const stack_t __user *uss, stack_t __user *uoss,
		unsigned long r5, unsigned long r6, unsigned long r7,
		unsigned long r8, struct pt_regs *regs)
{
	return do_sigaltstack(uss, uoss, regs->gpr[1]);
}
