// TODO coprocessor stuff
/*
 *  linux/arch/xtensa/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *
 *  Joe Taylor <joe@tensilica.com>
 *  Chris Zankel <chris@zankel.net>
 *
 *
 *
 */

#include <xtensa/config/core.h>
#include <xtensa/hal.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/personality.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>

#define DEBUG_SIG  0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

asmlinkage long sys_wait4(pid_t pid,unsigned int * stat_addr, int options,
			  struct rusage * ru);
asmlinkage int do_signal(struct pt_regs *regs, sigset_t *oldset);

extern struct task_struct *coproc_owners[];


/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */

int sys_sigsuspend(struct pt_regs *regs)
{
	old_sigset_t mask = (old_sigset_t) regs->areg[3];
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->areg[2] = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(regs, &saveset))
			return -EINTR;
	}
}

asmlinkage int
sys_rt_sigsuspend(struct pt_regs *regs)
{
	sigset_t *unewset = (sigset_t *) regs->areg[4];
	size_t sigsetsize = (size_t) regs->areg[3];
	sigset_t saveset, newset;
	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->areg[2] = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(regs, &saveset))
			return -EINTR;
	}
}

asmlinkage int
sys_sigaction(int sig, const struct old_sigaction *act,
	      struct old_sigaction *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (!access_ok(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

asmlinkage int
sys_sigaltstack(struct pt_regs *regs)
{
	const stack_t *uss = (stack_t *) regs->areg[4];
	stack_t *uoss = (stack_t *) regs->areg[3];

	if (regs->depc > 64)
		panic ("Double exception sys_sigreturn\n");


	return do_sigaltstack(uss, uoss, regs->areg[1]);
}


/*
 * Do a signal return; undo the signal stack.
 */

struct sigframe
{
	struct sigcontext sc;
	struct _cpstate cpstate;
	unsigned long extramask[_NSIG_WORDS-1];
	unsigned char retcode[6];
	unsigned int reserved[4]; /* Reserved area for chaining */
	unsigned int window[4]; /* Window of 4 registers for initial context */
};

struct rt_sigframe
{
	struct siginfo info;
	struct ucontext uc;
	struct _cpstate cpstate;
	unsigned char retcode[6];
	unsigned int reserved[4]; /* Reserved area for chaining */
	unsigned int window[4]; /* Window of 4 registers for initial context */
};

extern void release_all_cp (struct task_struct *);


// FIXME restore_cpextra
static inline int
restore_cpextra (struct _cpstate *buf)
{
#if 0
	/* The signal handler may have used coprocessors in which
	 * case they are still enabled.  We disable them to force a
	 * reloading of the original task's CP state by the lazy
	 * context-switching mechanisms of CP exception handling.
	 * Also, we essentially discard any coprocessor state that the
	 * signal handler created. */

	struct task_struct *tsk = current;
	release_all_cp(tsk);
	return __copy_from_user(tsk->thread.cpextra, buf, XTENSA_CP_EXTRA_SIZE);
#endif
	return 0;
}

/* Note: We don't copy double exception 'tregs', we have to finish double exc. first before we return to signal handler! This dbl.exc.handler might cause another double exception, but I think we are fine as the situation is the same as if we had returned to the signal handerl and got an interrupt immediately...
 */


static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext *sc)
{
	struct thread_struct *thread;
	unsigned int err = 0;
	unsigned long ps;
	struct _cpstate *buf;

#define COPY(x)	err |= __get_user(regs->x, &sc->sc_##x)
	COPY(pc);
	COPY(depc);
	COPY(wmask);
	COPY(lbeg);
	COPY(lend);
	COPY(lcount);
	COPY(sar);
	COPY(windowbase);
	COPY(windowstart);
#undef COPY

	/* For PS, restore only PS.CALLINC.
	 * Assume that all other bits are either the same as for the signal
	 * handler, or the user mode value doesn't matter (e.g. PS.OWB).
	 */
	err |= __get_user(ps, &sc->sc_ps);
	regs->ps = (regs->ps & ~XCHAL_PS_CALLINC_MASK)
		| (ps & XCHAL_PS_CALLINC_MASK);

	/* Additional corruption checks */

	if ((regs->windowbase >= (XCHAL_NUM_AREGS/4))
	|| ((regs->windowstart & ~((1<<(XCHAL_NUM_AREGS/4)) - 1)) != 0) )
		err = 1;
	if ((regs->lcount > 0)
	&& ((regs->lbeg > TASK_SIZE) || (regs->lend > TASK_SIZE)) )
		err = 1;

	/* Restore extended register state.
	 * See struct thread_struct in processor.h.
	 */
	thread = &current->thread;

	err |= __copy_from_user (regs->areg, sc->sc_areg, XCHAL_NUM_AREGS*4);
	err |= __get_user(buf, &sc->sc_cpstate);
	if (buf) {
		if (!access_ok(VERIFY_READ, buf, sizeof(*buf)))
			goto badframe;
		err |= restore_cpextra(buf);
	}

	regs->syscall = -1;		/* disable syscall checks */
	return err;

badframe:
	return 1;
}

static inline void
flush_my_cpstate(struct task_struct *tsk)
{
	unsigned long flags;
	local_irq_save(flags);

#if 0	// FIXME
	for (i = 0; i < XCHAL_CP_NUM; i++) {
		if (tsk == coproc_owners[i]) {
			xthal_validate_cp(i);
			xthal_save_cpregs(tsk->thread.cpregs_ptr[i], i);

			/* Invalidate and "disown" the cp to allow
			 * callers the chance to reset cp state in the
			 * task_struct. */

			xthal_invalidate_cp(i);
			coproc_owners[i] = 0;
		}
	}
#endif
	local_irq_restore(flags);
}

/* Return codes:
	0:  nothing saved
	1:  stuff to save, successful
       -1:  stuff to save, error happened
*/
static int
save_cpextra (struct _cpstate *buf)
{
#if (XCHAL_EXTRA_SA_SIZE == 0) && (XCHAL_CP_NUM == 0)
	return 0;
#else

	/* FIXME: If a task has never used a coprocessor, there is
	 * no need to save and restore anything.  Tracking this
	 * information would allow us to optimize this section.
	 * Perhaps we can use current->used_math or (current->flags &
	 * PF_USEDFPU) or define a new field in the thread
	 * structure. */

	/* We flush any live, task-owned cp state to the task_struct,
	 * then copy it all to the sigframe.  Then we clear all
	 * cp/extra state in the task_struct, effectively
	 * clearing/resetting all cp/extra state for the signal
	 * handler (cp-exception handling will load these new values
	 * into the cp/extra registers.)  This step is important for
	 * things like a floating-point cp, where the OS must reset
	 * the FCR to the default rounding mode. */

	int err = 0;
	struct task_struct *tsk = current;

	flush_my_cpstate(tsk);
	/* Note that we just copy everything: 'extra' and 'cp' state together.*/
	err |= __copy_to_user(buf, tsk->thread.cp_save, XTENSA_CP_EXTRA_SIZE);
	memset(tsk->thread.cp_save, 0, XTENSA_CP_EXTRA_SIZE);

#if (XTENSA_CP_EXTRA_SIZE == 0)
#error Sanity check on memset above, cpextra_size should not be zero.
#endif

	return err ? -1 : 1;
#endif
}

static int
setup_sigcontext(struct sigcontext *sc, struct _cpstate *cpstate,
		 struct pt_regs *regs, unsigned long mask)
{
	struct thread_struct *thread;
	int err = 0;

//printk("setup_sigcontext\n");
#define COPY(x)	err |= __put_user(regs->x, &sc->sc_##x)
	COPY(pc);
	COPY(ps);
	COPY(depc);
	COPY(wmask);
	COPY(lbeg);
	COPY(lend);
	COPY(lcount);
	COPY(sar);
	COPY(windowbase);
	COPY(windowstart);
#undef COPY

	/* Save extended register state.
	 * See struct thread_struct in processor.h.
	 */
	thread = &current->thread;
	err |= __copy_to_user (sc->sc_areg, regs->areg, XCHAL_NUM_AREGS * 4);
	err |= save_cpextra(cpstate);
	err |= __put_user(err ? NULL : cpstate, &sc->sc_cpstate);
	/* non-iBCS2 extensions.. */
	err |= __put_user(mask, &sc->oldmask);

	return err;
}

asmlinkage int sys_sigreturn(struct pt_regs *regs)
{
	struct sigframe *frame = (struct sigframe *)regs->areg[1];
	sigset_t set;
	if (regs->depc > 64)
		panic ("Double exception sys_sigreturn\n");

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;

	if (__get_user(set.sig[0], &frame->sc.oldmask)
	    || (_NSIG_WORDS > 1
		&& __copy_from_user(&set.sig[1], &frame->extramask,
				    sizeof(frame->extramask))))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(regs, &frame->sc))
		goto badframe;
	return regs->areg[2];

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage int sys_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_sigframe *frame = (struct rt_sigframe *)regs->areg[1];
	sigset_t set;
	stack_t st;
	int ret;
	if (regs->depc > 64)
	{
		printk("!!!!!!! DEPC !!!!!!!\n");
		return 0;
	}

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;

	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;
	ret = regs->areg[2];

	if (__copy_from_user(&st, &frame->uc.uc_stack, sizeof(st)))
		goto badframe;
	/* It is more difficult to avoid calling this function than to
	   call it and ignore errors.  */
	do_sigaltstack(&st, NULL, regs->areg[1]);

	return ret;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

/*
 * Set up a signal frame.
 */

/*
 * Determine which stack to use..
 */
static inline void *
get_sigframe(struct k_sigaction *ka, unsigned long sp, size_t frame_size)
{
	if ((ka->sa.sa_flags & SA_ONSTACK) != 0 && ! on_sig_stack(sp))
		sp = current->sas_ss_sp + current->sas_ss_size;

	return (void *)((sp - frame_size) & -16ul);
}

#define USE_SIGRETURN		0
#define USE_RT_SIGRETURN	1

static int
gen_return_code(unsigned char *codemem, unsigned int use_rt_sigreturn)
{
	unsigned int retcall;
	int err = 0;

#if 0
	/* Ignoring SA_RESTORER for now; it's supposed to be obsolete,
	 * and the xtensa glibc doesn't use it.
	 */
	if (ka->sa.sa_flags & SA_RESTORER) {
		regs->pr = (unsigned long) ka->sa.sa_restorer;
	} else
#endif /* 0 */
	{

#if (__NR_sigreturn > 255) || (__NR_rt_sigreturn > 255)

/* The 12-bit immediate is really split up within the 24-bit MOVI
 * instruction.  As long as the above system call numbers fit within
 * 8-bits, the following code works fine. See the Xtensa ISA for
 * details.
 */

#error Generating the MOVI instruction below breaks!
#endif

		retcall = use_rt_sigreturn ? __NR_rt_sigreturn : __NR_sigreturn;

#ifdef __XTENSA_EB__   /* Big Endian version */
		/* Generate instruction:  MOVI a2, retcall */
		err |= __put_user(0x22, &codemem[0]);
		err |= __put_user(0x0a, &codemem[1]);
		err |= __put_user(retcall, &codemem[2]);
		/* Generate instruction:  SYSCALL */
		err |= __put_user(0x00, &codemem[3]);
		err |= __put_user(0x05, &codemem[4]);
		err |= __put_user(0x00, &codemem[5]);

#elif defined __XTENSA_EL__   /* Little Endian version */
		/* Generate instruction:  MOVI a2, retcall */
		err |= __put_user(0x22, &codemem[0]);
		err |= __put_user(0xa0, &codemem[1]);
		err |= __put_user(retcall, &codemem[2]);
		/* Generate instruction:  SYSCALL */
		err |= __put_user(0x00, &codemem[3]);
		err |= __put_user(0x50, &codemem[4]);
		err |= __put_user(0x00, &codemem[5]);
#else
#error Must use compiler for Xtensa processors.
#endif
	}

	/* Flush generated code out of the data cache */

	if (err == 0)
		__flush_invalidate_cache_range((unsigned long)codemem, 6UL);

	return err;
}

static void
set_thread_state(struct pt_regs *regs, void *stack, unsigned char *retaddr,
	void *handler, unsigned long arg1, void *arg2, void *arg3)
{
	/* Set up registers for signal handler */
	start_thread(regs, (unsigned long) handler, (unsigned long) stack);

	/* Set up a stack frame for a call4
	 * Note: PS.CALLINC is set to one by start_thread
	 */
	regs->areg[4] = (((unsigned long) retaddr) & 0x3fffffff) | 0x40000000;
	regs->areg[6] = arg1;
	regs->areg[7] = (unsigned long) arg2;
	regs->areg[8] = (unsigned long) arg3;
}

static void setup_frame(int sig, struct k_sigaction *ka,
			sigset_t *set, struct pt_regs *regs)
{
	struct sigframe *frame;
	int err = 0;
	int signal;

	frame = get_sigframe(ka, regs->areg[1], sizeof(*frame));
	if (regs->depc > 64)
	{
		printk("!!!!!!! DEPC !!!!!!!\n");
		return;
	}


	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	signal = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	err |= setup_sigcontext(&frame->sc, &frame->cpstate, regs, set->sig[0]);

	if (_NSIG_WORDS > 1) {
		err |= __copy_to_user(frame->extramask, &set->sig[1],
				      sizeof(frame->extramask));
	}

	/* Create sys_sigreturn syscall in stack frame */
	err |= gen_return_code(frame->retcode, USE_SIGRETURN);

	if (err)
		goto give_sigsegv;

	/* Create signal handler execution context.
	 * Return context not modified until this point.
	 */
	set_thread_state(regs, frame, frame->retcode,
		ka->sa.sa_handler, signal, &frame->sc, NULL);

	/* Set access mode to USER_DS.  Nomenclature is outdated, but
	 * functionality is used in uaccess.h
	 */
	set_fs(USER_DS);


#if DEBUG_SIG
	printk("SIG deliver (%s:%d): signal=%d sp=%p pc=%08x\n",
		current->comm, current->pid, signal, frame, regs->pc);
#endif

	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

static void setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe *frame;
	int err = 0;
	int signal;

	frame = get_sigframe(ka, regs->areg[1], sizeof(*frame));
	if (regs->depc > 64)
		panic ("Double exception sys_sigreturn\n");

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	signal = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	err |= copy_siginfo_to_user(&frame->info, info);

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user((void *)current->sas_ss_sp,
			  &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->areg[1]),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, &frame->cpstate,
			        regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	/* Create sys_rt_sigreturn syscall in stack frame */
	err |= gen_return_code(frame->retcode, USE_RT_SIGRETURN);

	if (err)
		goto give_sigsegv;

	/* Create signal handler execution context.
	 * Return context not modified until this point.
	 */
	set_thread_state(regs, frame, frame->retcode,
		ka->sa.sa_handler, signal, &frame->info, &frame->uc);

	/* Set access mode to USER_DS.  Nomenclature is outdated, but
	 * functionality is used in uaccess.h
	 */
	set_fs(USER_DS);

#if DEBUG_SIG
	printk("SIG rt deliver (%s:%d): signal=%d sp=%p pc=%08x\n",
		current->comm, current->pid, signal, frame, regs->pc);
#endif

	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
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
int do_signal(struct pt_regs *regs, sigset_t *oldset)
{
	siginfo_t info;
	int signr;
	struct k_sigaction ka;

	if (!oldset)
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);

	/* Are we from a system call? */
	if (regs->syscall >= 0) {
		/* If so, check system call restarting.. */
		switch (regs->areg[2]) {
			case ERESTARTNOHAND:
			case ERESTART_RESTARTBLOCK:
				regs->areg[2] = -EINTR;
				break;

			case ERESTARTSYS:
				if (!(ka.sa.sa_flags & SA_RESTART)) {
					regs->areg[2] = -EINTR;
					break;
				}
			/* fallthrough */
			case ERESTARTNOINTR:
				regs->areg[2] = regs->syscall;
				regs->pc -= 3;
		}
	}

	if (signr == 0)
		return 0;		/* no signals delivered */

	/* Whee!  Actually deliver the signal.  */

	/* Set up the stack frame */
	if (ka.sa.sa_flags & SA_SIGINFO)
		setup_rt_frame(signr, &ka, &info, oldset, regs);
	else
		setup_frame(signr, &ka, oldset, regs);

	if (ka.sa.sa_flags & SA_ONESHOT)
		ka.sa.sa_handler = SIG_DFL;

	spin_lock_irq(&current->sighand->siglock);
	sigorsets(&current->blocked, &current->blocked, &ka.sa.sa_mask);
	if (!(ka.sa.sa_flags & SA_NODEFER))
		sigaddset(&current->blocked, signr);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	return 1;
}
