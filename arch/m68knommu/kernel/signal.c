/*
 *  linux/arch/m68knommu/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * Linux/m68k support by Hamish Macdonald
 *
 * 68060 fixes by Jesper Skov
 *
 * 1997-12-01  Modified for POSIX.1b signals by Andreas Schwab
 *
 * mathemu support by Roman Zippel
 *  (Note: fpstate in the signal context is completely ignored for the emulator
 *         and the internal floating point format is put on stack)
 */

/*
 * ++roman (07/09/96): implemented signal stacks (specially for tosemu on
 * Atari :-) Current limitation: Only one sigstack can be active at one time.
 * If a second signal with SA_ONSTACK set arrives while working on a sigstack,
 * SA_ONSTACK is ignored. This behaviour avoids lots of trouble with nested
 * signal handlers!
 */

#include <linux/sched.h>
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
#include <linux/tty.h>
#include <linux/personality.h>
#include <linux/binfmts.h>

#include <asm/setup.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/traps.h>
#include <asm/ucontext.h>

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

asmlinkage int do_signal(sigset_t *oldset, struct pt_regs *regs);

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int do_sigsuspend(struct pt_regs *regs)
{
	old_sigset_t mask = regs->d3;
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->d0 = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs))
			return -EINTR;
	}
}

asmlinkage int
do_rt_sigsuspend(struct pt_regs *regs)
{
	sigset_t *unewset = (sigset_t *)regs->d1;
	size_t sigsetsize = (size_t)regs->d2;
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

	regs->d0 = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs))
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
sys_sigaltstack(const stack_t *uss, stack_t *uoss)
{
	return do_sigaltstack(uss, uoss, rdusp());
}


/*
 * Do a signal return; undo the signal stack.
 *
 * Keep the return code on the stack quadword aligned!
 * That makes the cache flush below easier.
 */

struct sigframe
{
	char *pretcode;
	int sig;
	int code;
	struct sigcontext *psc;
	char retcode[8];
	unsigned long extramask[_NSIG_WORDS-1];
	struct sigcontext sc;
};

struct rt_sigframe
{
	char *pretcode;
	int sig;
	struct siginfo *pinfo;
	void *puc;
	char retcode[8];
	struct siginfo info;
	struct ucontext uc;
};

#ifdef CONFIG_FPU

static unsigned char fpu_version = 0;	/* version number of fpu, set by setup_frame */

static inline int restore_fpu_state(struct sigcontext *sc)
{
	int err = 1;

	if (FPU_IS_EMU) {
	    /* restore registers */
	    memcpy(current->thread.fpcntl, sc->sc_fpcntl, 12);
	    memcpy(current->thread.fp, sc->sc_fpregs, 24);
	    return 0;
	}

	if (sc->sc_fpstate[0]) {
	    /* Verify the frame format.  */
	    if (sc->sc_fpstate[0] != fpu_version)
		goto out;

	    __asm__ volatile (".chip 68k/68881\n\t"
			      "fmovemx %0,%/fp0-%/fp1\n\t"
			      "fmoveml %1,%/fpcr/%/fpsr/%/fpiar\n\t"
			      ".chip 68k"
			      : /* no outputs */
			      : "m" (*sc->sc_fpregs), "m" (*sc->sc_fpcntl));
	}
	__asm__ volatile (".chip 68k/68881\n\t"
			  "frestore %0\n\t"
			  ".chip 68k" : : "m" (*sc->sc_fpstate));
	err = 0;

out:
	return err;
}

#define FPCONTEXT_SIZE	216
#define uc_fpstate	uc_filler[0]
#define uc_formatvec	uc_filler[FPCONTEXT_SIZE/4]
#define uc_extra	uc_filler[FPCONTEXT_SIZE/4+1]

static inline int rt_restore_fpu_state(struct ucontext *uc)
{
	unsigned char fpstate[FPCONTEXT_SIZE];
	int context_size = 0;
	fpregset_t fpregs;
	int err = 1;

	if (FPU_IS_EMU) {
		/* restore fpu control register */
		if (__copy_from_user(current->thread.fpcntl,
				&uc->uc_mcontext.fpregs.f_pcr, 12))
			goto out;
		/* restore all other fpu register */
		if (__copy_from_user(current->thread.fp,
				uc->uc_mcontext.fpregs.f_fpregs, 96))
			goto out;
		return 0;
	}

	if (__get_user(*(long *)fpstate, (long *)&uc->uc_fpstate))
		goto out;
	if (fpstate[0]) {
		context_size = fpstate[1];

		/* Verify the frame format.  */
		if (fpstate[0] != fpu_version)
			goto out;
		if (__copy_from_user(&fpregs, &uc->uc_mcontext.fpregs,
		     sizeof(fpregs)))
			goto out;
		__asm__ volatile (".chip 68k/68881\n\t"
				  "fmovemx %0,%/fp0-%/fp7\n\t"
				  "fmoveml %1,%/fpcr/%/fpsr/%/fpiar\n\t"
				  ".chip 68k"
				  : /* no outputs */
				  : "m" (*fpregs.f_fpregs),
				    "m" (fpregs.f_pcr));
	}
	if (context_size &&
	    __copy_from_user(fpstate + 4, (long *)&uc->uc_fpstate + 1,
			     context_size))
		goto out;
	__asm__ volatile (".chip 68k/68881\n\t"
			  "frestore %0\n\t"
			  ".chip 68k" : : "m" (*fpstate));
	err = 0;

out:
	return err;
}

#endif

static inline int
restore_sigcontext(struct pt_regs *regs, struct sigcontext *usc, void *fp,
		   int *pd0)
{
	int formatvec;
	struct sigcontext context;
	int err = 0;

	/* get previous context */
	if (copy_from_user(&context, usc, sizeof(context)))
		goto badframe;
	
	/* restore passed registers */
	regs->d1 = context.sc_d1;
	regs->a0 = context.sc_a0;
	regs->a1 = context.sc_a1;
	((struct switch_stack *)regs - 1)->a5 = context.sc_a5;
	regs->sr = (regs->sr & 0xff00) | (context.sc_sr & 0xff);
	regs->pc = context.sc_pc;
	regs->orig_d0 = -1;		/* disable syscall checks */
	wrusp(context.sc_usp);
	formatvec = context.sc_formatvec;
	regs->format = formatvec >> 12;
	regs->vector = formatvec & 0xfff;

#ifdef CONFIG_FPU
	err = restore_fpu_state(&context);
#endif

	*pd0 = context.sc_d0;
	return err;

badframe:
	return 1;
}

static inline int
rt_restore_ucontext(struct pt_regs *regs, struct switch_stack *sw,
		    struct ucontext *uc, int *pd0)
{
	int temp;
	greg_t *gregs = uc->uc_mcontext.gregs;
	unsigned long usp;
	int err;

	err = __get_user(temp, &uc->uc_mcontext.version);
	if (temp != MCONTEXT_VERSION)
		goto badframe;
	/* restore passed registers */
	err |= __get_user(regs->d0, &gregs[0]);
	err |= __get_user(regs->d1, &gregs[1]);
	err |= __get_user(regs->d2, &gregs[2]);
	err |= __get_user(regs->d3, &gregs[3]);
	err |= __get_user(regs->d4, &gregs[4]);
	err |= __get_user(regs->d5, &gregs[5]);
	err |= __get_user(sw->d6, &gregs[6]);
	err |= __get_user(sw->d7, &gregs[7]);
	err |= __get_user(regs->a0, &gregs[8]);
	err |= __get_user(regs->a1, &gregs[9]);
	err |= __get_user(regs->a2, &gregs[10]);
	err |= __get_user(sw->a3, &gregs[11]);
	err |= __get_user(sw->a4, &gregs[12]);
	err |= __get_user(sw->a5, &gregs[13]);
	err |= __get_user(sw->a6, &gregs[14]);
	err |= __get_user(usp, &gregs[15]);
	wrusp(usp);
	err |= __get_user(regs->pc, &gregs[16]);
	err |= __get_user(temp, &gregs[17]);
	regs->sr = (regs->sr & 0xff00) | (temp & 0xff);
	regs->orig_d0 = -1;		/* disable syscall checks */
	regs->format = temp >> 12;
	regs->vector = temp & 0xfff;

	if (do_sigaltstack(&uc->uc_stack, NULL, usp) == -EFAULT)
		goto badframe;

	*pd0 = regs->d0;
	return err;

badframe:
	return 1;
}

asmlinkage int do_sigreturn(unsigned long __unused)
{
	struct switch_stack *sw = (struct switch_stack *) &__unused;
	struct pt_regs *regs = (struct pt_regs *) (sw + 1);
	unsigned long usp = rdusp();
	struct sigframe *frame = (struct sigframe *)(usp - 4);
	sigset_t set;
	int d0;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.sc_mask) ||
	    (_NSIG_WORDS > 1 &&
	     __copy_from_user(&set.sig[1], &frame->extramask,
			      sizeof(frame->extramask))))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	
	if (restore_sigcontext(regs, &frame->sc, frame + 1, &d0))
		goto badframe;
	return d0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage int do_rt_sigreturn(unsigned long __unused)
{
	struct switch_stack *sw = (struct switch_stack *) &__unused;
	struct pt_regs *regs = (struct pt_regs *) (sw + 1);
	unsigned long usp = rdusp();
	struct rt_sigframe *frame = (struct rt_sigframe *)(usp - 4);
	sigset_t set;
	int d0;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	
	if (rt_restore_ucontext(regs, sw, &frame->uc, &d0))
		goto badframe;
	return d0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

#ifdef CONFIG_FPU
/*
 * Set up a signal frame.
 */

static inline void save_fpu_state(struct sigcontext *sc, struct pt_regs *regs)
{
	if (FPU_IS_EMU) {
		/* save registers */
		memcpy(sc->sc_fpcntl, current->thread.fpcntl, 12);
		memcpy(sc->sc_fpregs, current->thread.fp, 24);
		return;
	}

	__asm__ volatile (".chip 68k/68881\n\t"
			  "fsave %0\n\t"
			  ".chip 68k"
			  : : "m" (*sc->sc_fpstate) : "memory");

	if (sc->sc_fpstate[0]) {
		fpu_version = sc->sc_fpstate[0];
		__asm__ volatile (".chip 68k/68881\n\t"
				  "fmovemx %/fp0-%/fp1,%0\n\t"
				  "fmoveml %/fpcr/%/fpsr/%/fpiar,%1\n\t"
				  ".chip 68k"
				  : /* no outputs */
				  : "m" (*sc->sc_fpregs),
				    "m" (*sc->sc_fpcntl)
				  : "memory");
	}
}

static inline int rt_save_fpu_state(struct ucontext *uc, struct pt_regs *regs)
{
	unsigned char fpstate[FPCONTEXT_SIZE];
	int context_size = 0;
	int err = 0;

	if (FPU_IS_EMU) {
		/* save fpu control register */
		err |= copy_to_user(&uc->uc_mcontext.fpregs.f_pcr,
				current->thread.fpcntl, 12);
		/* save all other fpu register */
		err |= copy_to_user(uc->uc_mcontext.fpregs.f_fpregs,
				current->thread.fp, 96);
		return err;
	}

	__asm__ volatile (".chip 68k/68881\n\t"
			  "fsave %0\n\t"
			  ".chip 68k"
			  : : "m" (*fpstate) : "memory");

	err |= __put_user(*(long *)fpstate, (long *)&uc->uc_fpstate);
	if (fpstate[0]) {
		fpregset_t fpregs;
		context_size = fpstate[1];
		fpu_version = fpstate[0];
		__asm__ volatile (".chip 68k/68881\n\t"
				  "fmovemx %/fp0-%/fp7,%0\n\t"
				  "fmoveml %/fpcr/%/fpsr/%/fpiar,%1\n\t"
				  ".chip 68k"
				  : /* no outputs */
				  : "m" (*fpregs.f_fpregs),
				    "m" (fpregs.f_pcr)
				  : "memory");
		err |= copy_to_user(&uc->uc_mcontext.fpregs, &fpregs,
				    sizeof(fpregs));
	}
	if (context_size)
		err |= copy_to_user((long *)&uc->uc_fpstate + 1, fpstate + 4,
				    context_size);
	return err;
}

#endif

static void setup_sigcontext(struct sigcontext *sc, struct pt_regs *regs,
			     unsigned long mask)
{
	sc->sc_mask = mask;
	sc->sc_usp = rdusp();
	sc->sc_d0 = regs->d0;
	sc->sc_d1 = regs->d1;
	sc->sc_a0 = regs->a0;
	sc->sc_a1 = regs->a1;
	sc->sc_a5 = ((struct switch_stack *)regs - 1)->a5;
	sc->sc_sr = regs->sr;
	sc->sc_pc = regs->pc;
	sc->sc_formatvec = regs->format << 12 | regs->vector;
#ifdef CONFIG_FPU
	save_fpu_state(sc, regs);
#endif
}

static inline int rt_setup_ucontext(struct ucontext *uc, struct pt_regs *regs)
{
	struct switch_stack *sw = (struct switch_stack *)regs - 1;
	greg_t *gregs = uc->uc_mcontext.gregs;
	int err = 0;

	err |= __put_user(MCONTEXT_VERSION, &uc->uc_mcontext.version);
	err |= __put_user(regs->d0, &gregs[0]);
	err |= __put_user(regs->d1, &gregs[1]);
	err |= __put_user(regs->d2, &gregs[2]);
	err |= __put_user(regs->d3, &gregs[3]);
	err |= __put_user(regs->d4, &gregs[4]);
	err |= __put_user(regs->d5, &gregs[5]);
	err |= __put_user(sw->d6, &gregs[6]);
	err |= __put_user(sw->d7, &gregs[7]);
	err |= __put_user(regs->a0, &gregs[8]);
	err |= __put_user(regs->a1, &gregs[9]);
	err |= __put_user(regs->a2, &gregs[10]);
	err |= __put_user(sw->a3, &gregs[11]);
	err |= __put_user(sw->a4, &gregs[12]);
	err |= __put_user(sw->a5, &gregs[13]);
	err |= __put_user(sw->a6, &gregs[14]);
	err |= __put_user(rdusp(), &gregs[15]);
	err |= __put_user(regs->pc, &gregs[16]);
	err |= __put_user(regs->sr, &gregs[17]);
#ifdef CONFIG_FPU
	err |= rt_save_fpu_state(uc, regs);
#endif
	return err;
}

static inline void push_cache (unsigned long vaddr)
{
}

static inline void *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, size_t frame_size)
{
	unsigned long usp;

	/* Default to using normal stack.  */
	usp = rdusp();

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (!sas_ss_flags(usp))
			usp = current->sas_ss_sp + current->sas_ss_size;
	}
	return (void *)((usp - frame_size) & -8UL);
}

static void setup_frame (int sig, struct k_sigaction *ka,
			 sigset_t *set, struct pt_regs *regs)
{
	struct sigframe *frame;
	struct sigcontext context;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	err |= __put_user((current_thread_info()->exec_domain
			   && current_thread_info()->exec_domain->signal_invmap
			   && sig < 32
			   ? current_thread_info()->exec_domain->signal_invmap[sig]
			   : sig),
			  &frame->sig);

	err |= __put_user(regs->vector, &frame->code);
	err |= __put_user(&frame->sc, &frame->psc);

	if (_NSIG_WORDS > 1)
		err |= copy_to_user(frame->extramask, &set->sig[1],
				    sizeof(frame->extramask));

	setup_sigcontext(&context, regs, set->sig[0]);
	err |= copy_to_user (&frame->sc, &context, sizeof(context));

	/* Set up to return from userspace.  */
	err |= __put_user(frame->retcode, &frame->pretcode);
	/* moveq #,d0; trap #0 */
	err |= __put_user(0x70004e40 + (__NR_sigreturn << 16),
			  (long *)(frame->retcode));

	if (err)
		goto give_sigsegv;

	push_cache ((unsigned long) &frame->retcode);

	/* Set up registers for signal handler */
	wrusp ((unsigned long) frame);
	regs->pc = (unsigned long) ka->sa.sa_handler;
	((struct switch_stack *)regs - 1)->a5 = current->mm->start_data;
	regs->format = 0x4; /*set format byte to make stack appear modulo 4 
						which it will be when doing the rte */

adjust_stack:
	/* Prepare to skip over the extra stuff in the exception frame.  */
	if (regs->stkadj) {
		struct pt_regs *tregs =
			(struct pt_regs *)((ulong)regs + regs->stkadj);
#if defined(DEBUG)
		printk(KERN_DEBUG "Performing stackadjust=%04x\n", regs->stkadj);
#endif
		/* This must be copied with decreasing addresses to
                   handle overlaps.  */
		tregs->vector = 0;
		tregs->format = 0;
		tregs->pc = regs->pc;
		tregs->sr = regs->sr;
	}
	return;

give_sigsegv:
	force_sigsegv(sig, current);
	goto adjust_stack;
}

static void setup_rt_frame (int sig, struct k_sigaction *ka, siginfo_t *info,
			    sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	err |= __put_user((current_thread_info()->exec_domain
			   && current_thread_info()->exec_domain->signal_invmap
			   && sig < 32
			   ? current_thread_info()->exec_domain->signal_invmap[sig]
			   : sig),
			  &frame->sig);
	err |= __put_user(&frame->info, &frame->pinfo);
	err |= __put_user(&frame->uc, &frame->puc);
	err |= copy_siginfo_to_user(&frame->info, info);

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user((void *)current->sas_ss_sp,
			  &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(rdusp()),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= rt_setup_ucontext(&frame->uc, regs);
	err |= copy_to_user (&frame->uc.uc_sigmask, set, sizeof(*set));

	/* Set up to return from userspace.  */
	err |= __put_user(frame->retcode, &frame->pretcode);
	/* moveq #,d0; notb d0; trap #0 */
	err |= __put_user(0x70004600 + ((__NR_rt_sigreturn ^ 0xff) << 16),
			  (long *)(frame->retcode + 0));
	err |= __put_user(0x4e40, (short *)(frame->retcode + 4));

	if (err)
		goto give_sigsegv;

	push_cache ((unsigned long) &frame->retcode);

	/* Set up registers for signal handler */
	wrusp ((unsigned long) frame);
	regs->pc = (unsigned long) ka->sa.sa_handler;
	((struct switch_stack *)regs - 1)->a5 = current->mm->start_data;
	regs->format = 0x4; /*set format byte to make stack appear modulo 4 
						which it will be when doing the rte */

adjust_stack:
	/* Prepare to skip over the extra stuff in the exception frame.  */
	if (regs->stkadj) {
		struct pt_regs *tregs =
			(struct pt_regs *)((ulong)regs + regs->stkadj);
#if defined(DEBUG)
		printk(KERN_DEBUG "Performing stackadjust=%04x\n", regs->stkadj);
#endif
		/* This must be copied with decreasing addresses to
                   handle overlaps.  */
		tregs->vector = 0;
		tregs->format = 0;
		tregs->pc = regs->pc;
		tregs->sr = regs->sr;
	}
	return;

give_sigsegv:
	force_sigsegv(sig, current);
	goto adjust_stack;
}

static inline void
handle_restart(struct pt_regs *regs, struct k_sigaction *ka, int has_handler)
{
	switch (regs->d0) {
	case -ERESTARTNOHAND:
		if (!has_handler)
			goto do_restart;
		regs->d0 = -EINTR;
		break;

	case -ERESTARTSYS:
		if (has_handler && !(ka->sa.sa_flags & SA_RESTART)) {
			regs->d0 = -EINTR;
			break;
		}
	/* fallthrough */
	case -ERESTARTNOINTR:
	do_restart:
		regs->d0 = regs->orig_d0;
		regs->pc -= 2;
		break;
	}
}

/*
 * OK, we're invoking a handler
 */
static void
handle_signal(int sig, struct k_sigaction *ka, siginfo_t *info,
	      sigset_t *oldset, struct pt_regs *regs)
{
	/* are we from a system call? */
	if (regs->orig_d0 >= 0)
		/* If so, check system call restarting.. */
		handle_restart(regs, ka, 1);

	/* set up the stack frame */
	if (ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame(sig, ka, info, oldset, regs);
	else
		setup_frame(sig, ka, oldset, regs);

	if (ka->sa.sa_flags & SA_ONESHOT)
		ka->sa.sa_handler = SIG_DFL;

	spin_lock_irq(&current->sighand->siglock);
	sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
	if (!(ka->sa.sa_flags & SA_NODEFER))
		sigaddset(&current->blocked,sig);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
asmlinkage int do_signal(sigset_t *oldset, struct pt_regs *regs)
{
	struct k_sigaction ka;
	siginfo_t info;
	int signr;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return 1;

	if (!oldset)
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		/* Whee!  Actually deliver the signal.  */
		handle_signal(signr, &ka, &info, oldset, regs);
		return 1;
	}

	/* Did we come from a system call? */
	if (regs->orig_d0 >= 0) {
		/* Restart the system call - no handlers present */
		if (regs->d0 == -ERESTARTNOHAND
		    || regs->d0 == -ERESTARTSYS
		    || regs->d0 == -ERESTARTNOINTR) {
			regs->d0 = regs->orig_d0;
			regs->pc -= 2;
		} else if (regs->d0 == -ERESTART_RESTARTBLOCK) {
			regs->d0 = __NR_restart_syscall;
			regs->pc -= 2;
		}
	}
	return 0;
}
