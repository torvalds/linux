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

#include <linux/ptrace.h>
#include <linux/signal.h>
#include <asm/unistd.h>

#include "signal.h"


#ifdef CONFIG_PPC64
static inline int is_32bit_task(void)
{
	return test_thread_flag(TIF_32BIT);
}
#else
static inline int is_32bit_task(void)
{
	return 1;
}
#endif


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

int do_signal(sigset_t *oldset, struct pt_regs *regs)
{
	siginfo_t info;
	int signr;
	struct k_sigaction ka;
	int ret;
	int is32 = is_32bit_task();

	if (test_thread_flag(TIF_RESTORE_SIGMASK))
		oldset = &current->saved_sigmask;
	else if (!oldset)
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);

	/* Is there any syscall restart business here ? */
	check_syscall_restart(regs, &ka, signr > 0);

	if (signr <= 0) {
		/* No signal to deliver -- put the saved sigmask back */
		if (test_thread_flag(TIF_RESTORE_SIGMASK)) {
			clear_thread_flag(TIF_RESTORE_SIGMASK);
			sigprocmask(SIG_SETMASK, &current->saved_sigmask, NULL);
		}
		return 0;               /* no signals delivered */
	}

#ifdef CONFIG_PPC64
        /*
	 * Reenable the DABR before delivering the signal to
	 * user space. The DABR will have been cleared if it
	 * triggered inside the kernel.
	 */
	if (current->thread.dabr)
		set_dabr(current->thread.dabr);
#endif

	if (is32) {
		unsigned int newsp;

		if ((ka.sa.sa_flags & SA_ONSTACK) &&
		    current->sas_ss_size && !on_sig_stack(regs->gpr[1]))
			newsp = current->sas_ss_sp + current->sas_ss_size;
		else
			newsp = regs->gpr[1];

        	if (ka.sa.sa_flags & SA_SIGINFO)
			ret = handle_rt_signal32(signr, &ka, &info, oldset,
					regs, newsp);
		else
			ret = handle_signal32(signr, &ka, &info, oldset,
					regs, newsp);
#ifdef CONFIG_PPC64
	} else {
		ret = handle_rt_signal64(signr, &ka, &info, oldset, regs);
#endif
	}

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
		 * its frame, and we can clear the TIF_RESTORE_SIGMASK flag.
		 */
		if (test_thread_flag(TIF_RESTORE_SIGMASK))
			clear_thread_flag(TIF_RESTORE_SIGMASK);
	}

	return ret;
}

long sys_sigaltstack(const stack_t __user *uss, stack_t __user *uoss,
		unsigned long r5, unsigned long r6, unsigned long r7,
		unsigned long r8, struct pt_regs *regs)
{
	return do_sigaltstack(uss, uoss, regs->gpr[1]);
}
