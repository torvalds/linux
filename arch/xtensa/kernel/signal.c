/*
 * arch/xtensa/kernel/signal.c
 *
 * Default platform functions.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005, 2006 Tensilica Inc.
 * Copyright (C) 1991, 1992  Linus Torvalds
 * 1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *
 * Chris Zankel <chris@zankel.net>
 * Joe Taylor <joe@tensilica.com>
 */

#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/personality.h>
#include <linux/freezer.h>
#include <linux/tracehook.h>

#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/coprocessor.h>
#include <asm/unistd.h>

#define DEBUG_SIG  0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

extern struct task_struct *coproc_owners[];

struct rt_sigframe
{
	struct siginfo info;
	struct ucontext uc;
	struct {
		xtregs_opt_t opt;
		xtregs_user_t user;
#if XTENSA_HAVE_COPROCESSORS
		xtregs_coprocessor_t cp;
#endif
	} xtregs;
	unsigned char retcode[6];
	unsigned int window[4];
};

/* 
 * Flush register windows stored in pt_regs to stack.
 * Returns 1 for errors.
 */

int
flush_window_regs_user(struct pt_regs *regs)
{
	const unsigned long ws = regs->windowstart;
	const unsigned long wb = regs->windowbase;
	unsigned long sp = 0;
	unsigned long wm;
	int err = 1;
	int base;

	/* Return if no other frames. */

	if (regs->wmask == 1)
		return 0;

	/* Rotate windowmask and skip empty frames. */

	wm = (ws >> wb) | (ws << (XCHAL_NUM_AREGS / 4 - wb));
	base = (XCHAL_NUM_AREGS / 4) - (regs->wmask >> 4);
		
	/* For call8 or call12 frames, we need the previous stack pointer. */

	if ((regs->wmask & 2) == 0)
		if (__get_user(sp, (int*)(regs->areg[base * 4 + 1] - 12)))
			goto errout;

	/* Spill frames to stack. */

	while (base < XCHAL_NUM_AREGS / 4) {

		int m = (wm >> base);
		int inc = 0;

		/* Save registers a4..a7 (call8) or a4...a11 (call12) */

		if (m & 2) {			/* call4 */
			inc = 1;

		} else if (m & 4) {		/* call8 */
			if (copy_to_user((void*)(sp - 32),
					   &regs->areg[(base + 1) * 4], 16))
				goto errout;
			inc = 2;

		} else if (m & 8) {	/* call12 */
			if (copy_to_user((void*)(sp - 48),
					   &regs->areg[(base + 1) * 4], 32))
				goto errout;
			inc = 3;
		}

		/* Save current frame a0..a3 under next SP */

		sp = regs->areg[((base + inc) * 4 + 1) % XCHAL_NUM_AREGS];
		if (copy_to_user((void*)(sp - 16), &regs->areg[base * 4], 16))
			goto errout;

		/* Get current stack pointer for next loop iteration. */

		sp = regs->areg[base * 4 + 1];
		base += inc;
	}

	regs->wmask = 1;
	regs->windowstart = 1 << wb;

	return 0;

errout:
	return err;
}

/*
 * Note: We don't copy double exception 'regs', we have to finish double exc. 
 * first before we return to signal handler! This dbl.exc.handler might cause 
 * another double exception, but I think we are fine as the situation is the 
 * same as if we had returned to the signal handerl and got an interrupt 
 * immediately...
 */

static int
setup_sigcontext(struct rt_sigframe __user *frame, struct pt_regs *regs)
{
	struct sigcontext __user *sc = &frame->uc.uc_mcontext;
	struct thread_info *ti = current_thread_info();
	int err = 0;

#define COPY(x)	err |= __put_user(regs->x, &sc->sc_##x)
	COPY(pc);
	COPY(ps);
	COPY(lbeg);
	COPY(lend);
	COPY(lcount);
	COPY(sar);
#undef COPY

	err |= flush_window_regs_user(regs);
	err |= __copy_to_user (sc->sc_a, regs->areg, 16 * 4);
	err |= __put_user(0, &sc->sc_xtregs);

	if (err)
		return err;

#if XTENSA_HAVE_COPROCESSORS
	coprocessor_flush_all(ti);
	coprocessor_release_all(ti);
	err |= __copy_to_user(&frame->xtregs.cp, &ti->xtregs_cp,
			      sizeof (frame->xtregs.cp));
#endif
	err |= __copy_to_user(&frame->xtregs.opt, &regs->xtregs_opt,
			      sizeof (xtregs_opt_t));
	err |= __copy_to_user(&frame->xtregs.user, &ti->xtregs_user,
			      sizeof (xtregs_user_t));

	err |= __put_user(err ? NULL : &frame->xtregs, &sc->sc_xtregs);

	return err;
}

static int
restore_sigcontext(struct pt_regs *regs, struct rt_sigframe __user *frame)
{
	struct sigcontext __user *sc = &frame->uc.uc_mcontext;
	struct thread_info *ti = current_thread_info();
	unsigned int err = 0;
	unsigned long ps;

#define COPY(x)	err |= __get_user(regs->x, &sc->sc_##x)
	COPY(pc);
	COPY(lbeg);
	COPY(lend);
	COPY(lcount);
	COPY(sar);
#undef COPY

	/* All registers were flushed to stack. Start with a prestine frame. */

	regs->wmask = 1;
	regs->windowbase = 0;
	regs->windowstart = 1;

	regs->syscall = -1;		/* disable syscall checks */

	/* For PS, restore only PS.CALLINC.
	 * Assume that all other bits are either the same as for the signal
	 * handler, or the user mode value doesn't matter (e.g. PS.OWB).
	 */
	err |= __get_user(ps, &sc->sc_ps);
	regs->ps = (regs->ps & ~PS_CALLINC_MASK) | (ps & PS_CALLINC_MASK);

	/* Additional corruption checks */

	if ((regs->lcount > 0)
	    && ((regs->lbeg > TASK_SIZE) || (regs->lend > TASK_SIZE)) )
		err = 1;

	err |= __copy_from_user(regs->areg, sc->sc_a, 16 * 4);

	if (err)
		return err;

 	/* The signal handler may have used coprocessors in which
	 * case they are still enabled.  We disable them to force a
	 * reloading of the original task's CP state by the lazy
	 * context-switching mechanisms of CP exception handling.
	 * Also, we essentially discard any coprocessor state that the
	 * signal handler created. */

#if XTENSA_HAVE_COPROCESSORS
	coprocessor_release_all(ti);
	err |= __copy_from_user(&ti->xtregs_cp, &frame->xtregs.cp,
				sizeof (frame->xtregs.cp));
#endif
	err |= __copy_from_user(&ti->xtregs_user, &frame->xtregs.user,
				sizeof (xtregs_user_t));
	err |= __copy_from_user(&regs->xtregs_opt, &frame->xtregs.opt,
				sizeof (xtregs_opt_t));

	return err;
}


/*
 * Do a signal return; undo the signal stack.
 */

asmlinkage long xtensa_rt_sigreturn(long a0, long a1, long a2, long a3,
				    long a4, long a5, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	sigset_t set;
	int ret;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	if (regs->depc > 64)
		panic("rt_sigreturn in double exception!\n");

	frame = (struct rt_sigframe __user *) regs->areg[1];

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;

	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	set_current_blocked(&set);

	if (restore_sigcontext(regs, frame))
		goto badframe;

	ret = regs->areg[2];

	if (do_sigaltstack(&frame->uc.uc_stack, NULL, regs->areg[1]) == -EFAULT)
		goto badframe;

	return ret;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}



/*
 * Set up a signal frame.
 */

static int
gen_return_code(unsigned char *codemem)
{
	int err = 0;

	/*
	 * The 12-bit immediate is really split up within the 24-bit MOVI
	 * instruction.  As long as the above system call numbers fit within
	 * 8-bits, the following code works fine. See the Xtensa ISA for
	 * details.
	 */

#if __NR_rt_sigreturn > 255
# error Generating the MOVI instruction below breaks!
#endif

#ifdef __XTENSA_EB__   /* Big Endian version */
	/* Generate instruction:  MOVI a2, __NR_rt_sigreturn */
	err |= __put_user(0x22, &codemem[0]);
	err |= __put_user(0x0a, &codemem[1]);
	err |= __put_user(__NR_rt_sigreturn, &codemem[2]);
	/* Generate instruction:  SYSCALL */
	err |= __put_user(0x00, &codemem[3]);
	err |= __put_user(0x05, &codemem[4]);
	err |= __put_user(0x00, &codemem[5]);

#elif defined __XTENSA_EL__   /* Little Endian version */
	/* Generate instruction:  MOVI a2, __NR_rt_sigreturn */
	err |= __put_user(0x22, &codemem[0]);
	err |= __put_user(0xa0, &codemem[1]);
	err |= __put_user(__NR_rt_sigreturn, &codemem[2]);
	/* Generate instruction:  SYSCALL */
	err |= __put_user(0x00, &codemem[3]);
	err |= __put_user(0x50, &codemem[4]);
	err |= __put_user(0x00, &codemem[5]);
#else
# error Must use compiler for Xtensa processors.
#endif

	/* Flush generated code out of the data cache */

	if (err == 0) {
		__invalidate_icache_range((unsigned long)codemem, 6UL);
		__flush_invalidate_dcache_range((unsigned long)codemem, 6UL);
	}

	return err;
}


static int setup_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
		       sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe *frame;
	int err = 0;
	int signal;
	unsigned long sp, ra;

	sp = regs->areg[1];

	if ((ka->sa.sa_flags & SA_ONSTACK) != 0 && ! on_sig_stack(sp)) {
		sp = current->sas_ss_sp + current->sas_ss_size;
	}

	frame = (void *)((sp - sizeof(*frame)) & -16ul);

	if (regs->depc > 64)
		panic ("Double exception sys_sigreturn\n");

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame))) {
		goto give_sigsegv;
	}

	signal = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	if (ka->sa.sa_flags & SA_SIGINFO) {
		err |= copy_siginfo_to_user(&frame->info, info);
	}

	/* Create the user context.  */

	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user((void *)current->sas_ss_sp,
			  &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->areg[1]),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(frame, regs);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	if (ka->sa.sa_flags & SA_RESTORER) {
		ra = (unsigned long)ka->sa.sa_restorer;
	} else {

		/* Create sys_rt_sigreturn syscall in stack frame */

		err |= gen_return_code(frame->retcode);

		if (err) {
			goto give_sigsegv;
		}
		ra = (unsigned long) frame->retcode;
	}

	/* 
	 * Create signal handler execution context.
	 * Return context not modified until this point.
	 */

	/* Set up registers for signal handler */
	start_thread(regs, (unsigned long) ka->sa.sa_handler, 
		     (unsigned long) frame);

	/* Set up a stack frame for a call4
	 * Note: PS.CALLINC is set to one by start_thread
	 */
	regs->areg[4] = (((unsigned long) ra) & 0x3fffffff) | 0x40000000;
	regs->areg[6] = (unsigned long) signal;
	regs->areg[7] = (unsigned long) &frame->info;
	regs->areg[8] = (unsigned long) &frame->uc;

	/* Set access mode to USER_DS.  Nomenclature is outdated, but
	 * functionality is used in uaccess.h
	 */
	set_fs(USER_DS);

#if DEBUG_SIG
	printk("SIG rt deliver (%s:%d): signal=%d sp=%p pc=%08x\n",
		current->comm, current->pid, signal, frame, regs->pc);
#endif

	return 0;

give_sigsegv:
	force_sigsegv(sig, current);
	return -EFAULT;
}

asmlinkage long xtensa_sigaltstack(const stack_t __user *uss, 
				   stack_t __user *uoss,
    				   long a2, long a3, long a4, long a5,
				   struct pt_regs *regs)
{
	return do_sigaltstack(uss, uoss, regs->areg[1]);
}



/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals that
 * the kernel can handle, and then we build all the user-level signal handling
 * stack-frames in one go after that.
 */
static void do_signal(struct pt_regs *regs)
{
	siginfo_t info;
	int signr;
	struct k_sigaction ka;
	sigset_t oldset;

	if (try_to_freeze())
		goto no_signal;

	if (test_thread_flag(TIF_RESTORE_SIGMASK))
		oldset = &current->saved_sigmask;
	else
		oldset = &current->blocked;

	task_pt_regs(current)->icountlevel = 0;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);

	if (signr > 0) {
		int ret;

		/* Are we from a system call? */

		if ((signed)regs->syscall >= 0) {

			/* If so, check system call restarting.. */

			switch (regs->areg[2]) {
				case -ERESTARTNOHAND:
				case -ERESTART_RESTARTBLOCK:
					regs->areg[2] = -EINTR;
					break;

				case -ERESTARTSYS:
					if (!(ka.sa.sa_flags & SA_RESTART)) {
						regs->areg[2] = -EINTR;
						break;
					}
					/* fallthrough */
				case -ERESTARTNOINTR:
					regs->areg[2] = regs->syscall;
					regs->pc -= 3;
					break;

				default:
					/* nothing to do */
					if (regs->areg[2] != 0)
					break;
			}
		}

		/* Whee!  Actually deliver the signal.  */
		/* Set up the stack frame */
		ret = setup_frame(signr, &ka, &info, oldset, regs);
		if (ret)
			return;

		clear_thread_flag(TIF_RESTORE_SIGMASK);
		block_sigmask(&ka, signr);
		if (current->ptrace & PT_SINGLESTEP)
			task_pt_regs(current)->icountlevel = 1;

		return;
	}

no_signal:
	/* Did we come from a system call? */
	if ((signed) regs->syscall >= 0) {
		/* Restart the system call - no handlers present */
		switch (regs->areg[2]) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->areg[2] = regs->syscall;
			regs->pc -= 3;
			break;
		case -ERESTART_RESTARTBLOCK:
			regs->areg[2] = __NR_restart_syscall;
			regs->pc -= 3;
			break;
		}
	}

	/* If there's no signal to deliver, we just restore the saved mask.  */
	if (test_and_clear_thread_flag(TIF_RESTORE_SIGMASK))
		set_current_blocked(&current->saved_sigmask);

	if (current->ptrace & PT_SINGLESTEP)
		task_pt_regs(current)->icountlevel = 1;
	return;
}

void do_notify_resume(struct pt_regs *regs)
{
	if (!user_mode(regs))
		return;

	if (test_thread_flag(TIF_SIGPENDING))
		do_signal(regs);

	if (test_and_clear_thread_flag(TIF_NOTIFY_RESUME)) {
		tracehook_notify_resume(regs);
		if (current->replacement_session_keyring)
			key_replace_session_keyring();
	}
}
