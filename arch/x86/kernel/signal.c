/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen SuSE Labs
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *  2000-06-20  Pentium III FXSR, SSE support by Gareth Hughes
 *  2000-2002   x86-64 support by Andi Kleen
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/tracehook.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/personality.h>
#include <linux/uaccess.h>

#include <asm/processor.h>
#include <asm/ucontext.h>
#include <asm/i387.h>
#include <asm/vdso.h>

#ifdef CONFIG_X86_64
#include <asm/proto.h>
#include <asm/ia32_unistd.h>
#include <asm/mce.h>
#endif /* CONFIG_X86_64 */

#include <asm/syscall.h>
#include <asm/syscalls.h>

#include <asm/sigframe.h>

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

#define __FIX_EFLAGS	(X86_EFLAGS_AC | X86_EFLAGS_OF | \
			 X86_EFLAGS_DF | X86_EFLAGS_TF | X86_EFLAGS_SF | \
			 X86_EFLAGS_ZF | X86_EFLAGS_AF | X86_EFLAGS_PF | \
			 X86_EFLAGS_CF)

#ifdef CONFIG_X86_32
# define FIX_EFLAGS	(__FIX_EFLAGS | X86_EFLAGS_RF)
#else
# define FIX_EFLAGS	__FIX_EFLAGS
#endif

#define COPY(x)			{		\
	err |= __get_user(regs->x, &sc->x);	\
}

#define COPY_SEG(seg)		{			\
		unsigned short tmp;			\
		err |= __get_user(tmp, &sc->seg);	\
		regs->seg = tmp;			\
}

#define COPY_SEG_CPL3(seg)	{			\
		unsigned short tmp;			\
		err |= __get_user(tmp, &sc->seg);	\
		regs->seg = tmp | 3;			\
}

#define GET_SEG(seg)		{			\
		unsigned short tmp;			\
		err |= __get_user(tmp, &sc->seg);	\
		loadsegment(seg, tmp);			\
}

static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc,
		   unsigned long *pax)
{
	void __user *buf;
	unsigned int tmpflags;
	unsigned int err = 0;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

#ifdef CONFIG_X86_32
	GET_SEG(gs);
	COPY_SEG(fs);
	COPY_SEG(es);
	COPY_SEG(ds);
#endif /* CONFIG_X86_32 */

	COPY(di); COPY(si); COPY(bp); COPY(sp); COPY(bx);
	COPY(dx); COPY(cx); COPY(ip);

#ifdef CONFIG_X86_64
	COPY(r8);
	COPY(r9);
	COPY(r10);
	COPY(r11);
	COPY(r12);
	COPY(r13);
	COPY(r14);
	COPY(r15);
#endif /* CONFIG_X86_64 */

#ifdef CONFIG_X86_32
	COPY_SEG_CPL3(cs);
	COPY_SEG_CPL3(ss);
#else /* !CONFIG_X86_32 */
	/* Kernel saves and restores only the CS segment register on signals,
	 * which is the bare minimum needed to allow mixed 32/64-bit code.
	 * App's signal handler can save/restore other segments if needed. */
	COPY_SEG_CPL3(cs);
#endif /* CONFIG_X86_32 */

	err |= __get_user(tmpflags, &sc->flags);
	regs->flags = (regs->flags & ~FIX_EFLAGS) | (tmpflags & FIX_EFLAGS);
	regs->orig_ax = -1;		/* disable syscall checks */

	err |= __get_user(buf, &sc->fpstate);
	err |= restore_i387_xstate(buf);

	err |= __get_user(*pax, &sc->ax);
	return err;
}

static int
setup_sigcontext(struct sigcontext __user *sc, void __user *fpstate,
		 struct pt_regs *regs, unsigned long mask)
{
	int err = 0;

#ifdef CONFIG_X86_32
	{
		unsigned int tmp;

		savesegment(gs, tmp);
		err |= __put_user(tmp, (unsigned int __user *)&sc->gs);
	}
	err |= __put_user(regs->fs, (unsigned int __user *)&sc->fs);
	err |= __put_user(regs->es, (unsigned int __user *)&sc->es);
	err |= __put_user(regs->ds, (unsigned int __user *)&sc->ds);
#endif /* CONFIG_X86_32 */

	err |= __put_user(regs->di, &sc->di);
	err |= __put_user(regs->si, &sc->si);
	err |= __put_user(regs->bp, &sc->bp);
	err |= __put_user(regs->sp, &sc->sp);
	err |= __put_user(regs->bx, &sc->bx);
	err |= __put_user(regs->dx, &sc->dx);
	err |= __put_user(regs->cx, &sc->cx);
	err |= __put_user(regs->ax, &sc->ax);
#ifdef CONFIG_X86_64
	err |= __put_user(regs->r8, &sc->r8);
	err |= __put_user(regs->r9, &sc->r9);
	err |= __put_user(regs->r10, &sc->r10);
	err |= __put_user(regs->r11, &sc->r11);
	err |= __put_user(regs->r12, &sc->r12);
	err |= __put_user(regs->r13, &sc->r13);
	err |= __put_user(regs->r14, &sc->r14);
	err |= __put_user(regs->r15, &sc->r15);
#endif /* CONFIG_X86_64 */

	err |= __put_user(current->thread.trap_no, &sc->trapno);
	err |= __put_user(current->thread.error_code, &sc->err);
	err |= __put_user(regs->ip, &sc->ip);
#ifdef CONFIG_X86_32
	err |= __put_user(regs->cs, (unsigned int __user *)&sc->cs);
	err |= __put_user(regs->flags, &sc->flags);
	err |= __put_user(regs->sp, &sc->sp_at_signal);
	err |= __put_user(regs->ss, (unsigned int __user *)&sc->ss);
#else /* !CONFIG_X86_32 */
	err |= __put_user(regs->flags, &sc->flags);
	err |= __put_user(regs->cs, &sc->cs);
	err |= __put_user(0, &sc->gs);
	err |= __put_user(0, &sc->fs);
#endif /* CONFIG_X86_32 */

	err |= __put_user(fpstate, &sc->fpstate);

	/* non-iBCS2 extensions.. */
	err |= __put_user(mask, &sc->oldmask);
	err |= __put_user(current->thread.cr2, &sc->cr2);

	return err;
}

/*
 * Set up a signal frame.
 */
#ifdef CONFIG_X86_32
static const struct {
	u16 poplmovl;
	u32 val;
	u16 int80;
} __attribute__((packed)) retcode = {
	0xb858,		/* popl %eax; movl $..., %eax */
	__NR_sigreturn,
	0x80cd,		/* int $0x80 */
};

static const struct {
	u8  movl;
	u32 val;
	u16 int80;
	u8  pad;
} __attribute__((packed)) rt_retcode = {
	0xb8,		/* movl $..., %eax */
	__NR_rt_sigreturn,
	0x80cd,		/* int $0x80 */
	0
};

/*
 * Determine which stack to use..
 */
static inline void __user *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, size_t frame_size,
	     void **fpstate)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = regs->sp;

	/*
	 * If we are on the alternate signal stack and would overflow it, don't.
	 * Return an always-bogus address instead so we will die with SIGSEGV.
	 */
	if (on_sig_stack(sp) && !likely(on_sig_stack(sp - frame_size)))
		return (void __user *) -1L;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (sas_ss_flags(sp) == 0)
			sp = current->sas_ss_sp + current->sas_ss_size;
	} else {
		/* This is the legacy signal stack switching. */
		if ((regs->ss & 0xffff) != __USER_DS &&
			!(ka->sa.sa_flags & SA_RESTORER) &&
				ka->sa.sa_restorer)
			sp = (unsigned long) ka->sa.sa_restorer;
	}

	if (used_math()) {
		sp = sp - sig_xstate_size;
		*fpstate = (struct _fpstate *) sp;
		if (save_i387_xstate(*fpstate) < 0)
			return (void __user *)-1L;
	}

	sp -= frame_size;
	/*
	 * Align the stack pointer according to the i386 ABI,
	 * i.e. so that on function entry ((sp + 4) & 15) == 0.
	 */
	sp = ((sp + 4) & -16ul) - 4;

	return (void __user *) sp;
}

static int
__setup_frame(int sig, struct k_sigaction *ka, sigset_t *set,
	      struct pt_regs *regs)
{
	struct sigframe __user *frame;
	void __user *restorer;
	int err = 0;
	void __user *fpstate = NULL;

	frame = get_sigframe(ka, regs, sizeof(*frame), &fpstate);

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		return -EFAULT;

	if (__put_user(sig, &frame->sig))
		return -EFAULT;

	if (setup_sigcontext(&frame->sc, fpstate, regs, set->sig[0]))
		return -EFAULT;

	if (_NSIG_WORDS > 1) {
		if (__copy_to_user(&frame->extramask, &set->sig[1],
				   sizeof(frame->extramask)))
			return -EFAULT;
	}

	if (current->mm->context.vdso)
		restorer = VDSO32_SYMBOL(current->mm->context.vdso, sigreturn);
	else
		restorer = &frame->retcode;
	if (ka->sa.sa_flags & SA_RESTORER)
		restorer = ka->sa.sa_restorer;

	/* Set up to return from userspace.  */
	err |= __put_user(restorer, &frame->pretcode);

	/*
	 * This is popl %eax ; movl $__NR_sigreturn, %eax ; int $0x80
	 *
	 * WE DO NOT USE IT ANY MORE! It's only left here for historical
	 * reasons and because gdb uses it as a signature to notice
	 * signal handler stack frames.
	 */
	err |= __put_user(*((u64 *)&retcode), (u64 *)frame->retcode);

	if (err)
		return -EFAULT;

	/* Set up registers for signal handler */
	regs->sp = (unsigned long)frame;
	regs->ip = (unsigned long)ka->sa.sa_handler;
	regs->ax = (unsigned long)sig;
	regs->dx = 0;
	regs->cx = 0;

	regs->ds = __USER_DS;
	regs->es = __USER_DS;
	regs->ss = __USER_DS;
	regs->cs = __USER_CS;

	return 0;
}

static int __setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			    sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	void __user *restorer;
	int err = 0;
	void __user *fpstate = NULL;

	frame = get_sigframe(ka, regs, sizeof(*frame), &fpstate);

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		return -EFAULT;

	err |= __put_user(sig, &frame->sig);
	err |= __put_user(&frame->info, &frame->pinfo);
	err |= __put_user(&frame->uc, &frame->puc);
	err |= copy_siginfo_to_user(&frame->info, info);
	if (err)
		return -EFAULT;

	/* Create the ucontext.  */
	if (cpu_has_xsave)
		err |= __put_user(UC_FP_XSTATE, &frame->uc.uc_flags);
	else
		err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(current->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->sp),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, fpstate,
				regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		return -EFAULT;

	/* Set up to return from userspace.  */
	restorer = VDSO32_SYMBOL(current->mm->context.vdso, rt_sigreturn);
	if (ka->sa.sa_flags & SA_RESTORER)
		restorer = ka->sa.sa_restorer;
	err |= __put_user(restorer, &frame->pretcode);

	/*
	 * This is movl $__NR_rt_sigreturn, %ax ; int $0x80
	 *
	 * WE DO NOT USE IT ANY MORE! It's only left here for historical
	 * reasons and because gdb uses it as a signature to notice
	 * signal handler stack frames.
	 */
	err |= __put_user(*((u64 *)&rt_retcode), (u64 *)frame->retcode);

	if (err)
		return -EFAULT;

	/* Set up registers for signal handler */
	regs->sp = (unsigned long)frame;
	regs->ip = (unsigned long)ka->sa.sa_handler;
	regs->ax = (unsigned long)sig;
	regs->dx = (unsigned long)&frame->info;
	regs->cx = (unsigned long)&frame->uc;

	regs->ds = __USER_DS;
	regs->es = __USER_DS;
	regs->ss = __USER_DS;
	regs->cs = __USER_CS;

	return 0;
}
#else /* !CONFIG_X86_32 */
/*
 * Determine which stack to use..
 */
static void __user *
get_stack(struct k_sigaction *ka, unsigned long sp, unsigned long size)
{
	/* Default to using normal stack - redzone*/
	sp -= 128;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (sas_ss_flags(sp) == 0)
			sp = current->sas_ss_sp + current->sas_ss_size;
	}

	return (void __user *)round_down(sp - size, 64);
}

static int __setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			    sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	void __user *fp = NULL;
	int err = 0;
	struct task_struct *me = current;

	if (used_math()) {
		fp = get_stack(ka, regs->sp, sig_xstate_size);
		frame = (void __user *)round_down(
			(unsigned long)fp - sizeof(struct rt_sigframe), 16) - 8;

		if (save_i387_xstate(fp) < 0)
			return -EFAULT;
	} else
		frame = get_stack(ka, regs->sp, sizeof(struct rt_sigframe)) - 8;

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		return -EFAULT;

	if (ka->sa.sa_flags & SA_SIGINFO) {
		if (copy_siginfo_to_user(&frame->info, info))
			return -EFAULT;
	}

	/* Create the ucontext.  */
	if (cpu_has_xsave)
		err |= __put_user(UC_FP_XSTATE, &frame->uc.uc_flags);
	else
		err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(me->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->sp),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(me->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, fp, regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	/* x86-64 should always use SA_RESTORER. */
	if (ka->sa.sa_flags & SA_RESTORER) {
		err |= __put_user(ka->sa.sa_restorer, &frame->pretcode);
	} else {
		/* could use a vstub here */
		return -EFAULT;
	}

	if (err)
		return -EFAULT;

	/* Set up registers for signal handler */
	regs->di = sig;
	/* In case the signal handler was declared without prototypes */
	regs->ax = 0;

	/* This also works for non SA_SIGINFO handlers because they expect the
	   next argument after the signal number on the stack. */
	regs->si = (unsigned long)&frame->info;
	regs->dx = (unsigned long)&frame->uc;
	regs->ip = (unsigned long) ka->sa.sa_handler;

	regs->sp = (unsigned long)frame;

	/* Set up the CS register to run signal handlers in 64-bit mode,
	   even if the handler happens to be interrupting 32-bit code. */
	regs->cs = __USER_CS;

	return 0;
}
#endif /* CONFIG_X86_32 */

#ifdef CONFIG_X86_32
/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int
sys_sigsuspend(int history0, int history1, old_sigset_t mask)
{
	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	current->saved_sigmask = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	current->state = TASK_INTERRUPTIBLE;
	schedule();
	set_restore_sigmask();

	return -ERESTARTNOHAND;
}

asmlinkage int
sys_sigaction(int sig, const struct old_sigaction __user *act,
	      struct old_sigaction __user *oact)
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
#endif /* CONFIG_X86_32 */

#ifdef CONFIG_X86_32
asmlinkage int sys_sigaltstack(unsigned long bx)
{
	/*
	 * This is needed to make gcc realize it doesn't own the
	 * "struct pt_regs"
	 */
	struct pt_regs *regs = (struct pt_regs *)&bx;
	const stack_t __user *uss = (const stack_t __user *)bx;
	stack_t __user *uoss = (stack_t __user *)regs->cx;

	return do_sigaltstack(uss, uoss, regs->sp);
}
#else /* !CONFIG_X86_32 */
asmlinkage long
sys_sigaltstack(const stack_t __user *uss, stack_t __user *uoss,
		struct pt_regs *regs)
{
	return do_sigaltstack(uss, uoss, regs->sp);
}
#endif /* CONFIG_X86_32 */

/*
 * Do a signal return; undo the signal stack.
 */
#ifdef CONFIG_X86_32
asmlinkage unsigned long sys_sigreturn(unsigned long __unused)
{
	struct sigframe __user *frame;
	struct pt_regs *regs;
	unsigned long ax;
	sigset_t set;

	regs = (struct pt_regs *) &__unused;
	frame = (struct sigframe __user *)(regs->sp - 8);

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.oldmask) || (_NSIG_WORDS > 1
		&& __copy_from_user(&set.sig[1], &frame->extramask,
				    sizeof(frame->extramask))))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(regs, &frame->sc, &ax))
		goto badframe;
	return ax;

badframe:
	signal_fault(regs, frame, "sigreturn");

	return 0;
}
#endif /* CONFIG_X86_32 */

static long do_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	unsigned long ax;
	sigset_t set;

	frame = (struct rt_sigframe __user *)(regs->sp - sizeof(long));
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext, &ax))
		goto badframe;

	if (do_sigaltstack(&frame->uc.uc_stack, NULL, regs->sp) == -EFAULT)
		goto badframe;

	return ax;

badframe:
	signal_fault(regs, frame, "rt_sigreturn");
	return 0;
}

#ifdef CONFIG_X86_32
/*
 * Note: do not pass in pt_regs directly as with tail-call optimization
 * GCC will incorrectly stomp on the caller's frame and corrupt user-space
 * register state:
 */
asmlinkage int sys_rt_sigreturn(unsigned long __unused)
{
	struct pt_regs *regs = (struct pt_regs *)&__unused;

	return do_rt_sigreturn(regs);
}
#else /* !CONFIG_X86_32 */
asmlinkage long sys_rt_sigreturn(struct pt_regs *regs)
{
	return do_rt_sigreturn(regs);
}
#endif /* CONFIG_X86_32 */

/*
 * OK, we're invoking a handler:
 */
static int signr_convert(int sig)
{
#ifdef CONFIG_X86_32
	struct thread_info *info = current_thread_info();

	if (info->exec_domain && info->exec_domain->signal_invmap && sig < 32)
		return info->exec_domain->signal_invmap[sig];
#endif /* CONFIG_X86_32 */
	return sig;
}

#ifdef CONFIG_X86_32

#define is_ia32	1
#define ia32_setup_frame	__setup_frame
#define ia32_setup_rt_frame	__setup_rt_frame

#else /* !CONFIG_X86_32 */

#ifdef CONFIG_IA32_EMULATION
#define is_ia32	test_thread_flag(TIF_IA32)
#else /* !CONFIG_IA32_EMULATION */
#define is_ia32	0
#endif /* CONFIG_IA32_EMULATION */

int ia32_setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
		sigset_t *set, struct pt_regs *regs);
int ia32_setup_frame(int sig, struct k_sigaction *ka,
		sigset_t *set, struct pt_regs *regs);

#endif /* CONFIG_X86_32 */

static int
setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
	       sigset_t *set, struct pt_regs *regs)
{
	int usig = signr_convert(sig);
	int ret;

	/* Set up the stack frame */
	if (is_ia32) {
		if (ka->sa.sa_flags & SA_SIGINFO)
			ret = ia32_setup_rt_frame(usig, ka, info, set, regs);
		else
			ret = ia32_setup_frame(usig, ka, set, regs);
	} else
		ret = __setup_rt_frame(sig, ka, info, set, regs);

	if (ret) {
		force_sigsegv(sig, current);
		return -EFAULT;
	}

	return ret;
}

static int
handle_signal(unsigned long sig, siginfo_t *info, struct k_sigaction *ka,
	      sigset_t *oldset, struct pt_regs *regs)
{
	int ret;

	/* Are we from a system call? */
	if (syscall_get_nr(current, regs) >= 0) {
		/* If so, check system call restarting.. */
		switch (syscall_get_error(current, regs)) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->ax = -EINTR;
			break;

		case -ERESTARTSYS:
			if (!(ka->sa.sa_flags & SA_RESTART)) {
				regs->ax = -EINTR;
				break;
			}
		/* fallthrough */
		case -ERESTARTNOINTR:
			regs->ax = regs->orig_ax;
			regs->ip -= 2;
			break;
		}
	}

	/*
	 * If TF is set due to a debugger (TIF_FORCED_TF), clear the TF
	 * flag so that register information in the sigcontext is correct.
	 */
	if (unlikely(regs->flags & X86_EFLAGS_TF) &&
	    likely(test_and_clear_thread_flag(TIF_FORCED_TF)))
		regs->flags &= ~X86_EFLAGS_TF;

	ret = setup_rt_frame(sig, ka, info, oldset, regs);

	if (ret)
		return ret;

#ifdef CONFIG_X86_64
	/*
	 * This has nothing to do with segment registers,
	 * despite the name.  This magic affects uaccess.h
	 * macros' behavior.  Reset it to the normal setting.
	 */
	set_fs(USER_DS);
#endif

	/*
	 * Clear the direction flag as per the ABI for function entry.
	 */
	regs->flags &= ~X86_EFLAGS_DF;

	/*
	 * Clear TF when entering the signal handler, but
	 * notify any tracer that was single-stepping it.
	 * The tracer may want to single-step inside the
	 * handler too.
	 */
	regs->flags &= ~X86_EFLAGS_TF;

	spin_lock_irq(&current->sighand->siglock);
	sigorsets(&current->blocked, &current->blocked, &ka->sa.sa_mask);
	if (!(ka->sa.sa_flags & SA_NODEFER))
		sigaddset(&current->blocked, sig);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	tracehook_signal_handler(sig, info, ka, regs,
				 test_thread_flag(TIF_SINGLESTEP));

	return 0;
}

#ifdef CONFIG_X86_32
#define NR_restart_syscall	__NR_restart_syscall
#else /* !CONFIG_X86_32 */
#define NR_restart_syscall	\
	test_thread_flag(TIF_IA32) ? __NR_ia32_restart_syscall : __NR_restart_syscall
#endif /* CONFIG_X86_32 */

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
static void do_signal(struct pt_regs *regs)
{
	struct k_sigaction ka;
	siginfo_t info;
	int signr;
	sigset_t *oldset;

	/*
	 * We want the common case to go fast, which is why we may in certain
	 * cases get here from kernel mode. Just return without doing anything
	 * if so.
	 * X86_32: vm86 regs switched out by assembly code before reaching
	 * here, so testing against kernel CS suffices.
	 */
	if (!user_mode(regs))
		return;

	if (current_thread_info()->status & TS_RESTORE_SIGMASK)
		oldset = &current->saved_sigmask;
	else
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		/*
		 * Re-enable any watchpoints before delivering the
		 * signal to user space. The processor register will
		 * have been cleared if the watchpoint triggered
		 * inside the kernel.
		 */
		if (current->thread.debugreg7)
			set_debugreg(current->thread.debugreg7, 7);

		/* Whee! Actually deliver the signal.  */
		if (handle_signal(signr, &info, &ka, oldset, regs) == 0) {
			/*
			 * A signal was successfully delivered; the saved
			 * sigmask will have been stored in the signal frame,
			 * and will be restored by sigreturn, so we can simply
			 * clear the TS_RESTORE_SIGMASK flag.
			 */
			current_thread_info()->status &= ~TS_RESTORE_SIGMASK;
		}
		return;
	}

	/* Did we come from a system call? */
	if (syscall_get_nr(current, regs) >= 0) {
		/* Restart the system call - no handlers present */
		switch (syscall_get_error(current, regs)) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->ax = regs->orig_ax;
			regs->ip -= 2;
			break;

		case -ERESTART_RESTARTBLOCK:
			regs->ax = NR_restart_syscall;
			regs->ip -= 2;
			break;
		}
	}

	/*
	 * If there's no signal to deliver, we just put the saved sigmask
	 * back.
	 */
	if (current_thread_info()->status & TS_RESTORE_SIGMASK) {
		current_thread_info()->status &= ~TS_RESTORE_SIGMASK;
		sigprocmask(SIG_SETMASK, &current->saved_sigmask, NULL);
	}
}

/*
 * notification of userspace execution resumption
 * - triggered by the TIF_WORK_MASK flags
 */
void
do_notify_resume(struct pt_regs *regs, void *unused, __u32 thread_info_flags)
{
#if defined(CONFIG_X86_64) && defined(CONFIG_X86_MCE)
	/* notify userspace of pending MCEs */
	if (thread_info_flags & _TIF_MCE_NOTIFY)
		mce_notify_user();
#endif /* CONFIG_X86_64 && CONFIG_X86_MCE */

	/* deal with pending signal delivery */
	if (thread_info_flags & _TIF_SIGPENDING)
		do_signal(regs);

	if (thread_info_flags & _TIF_NOTIFY_RESUME) {
		clear_thread_flag(TIF_NOTIFY_RESUME);
		tracehook_notify_resume(regs);
	}

#ifdef CONFIG_X86_32
	clear_thread_flag(TIF_IRET);
#endif /* CONFIG_X86_32 */
}

void signal_fault(struct pt_regs *regs, void __user *frame, char *where)
{
	struct task_struct *me = current;

	if (show_unhandled_signals && printk_ratelimit()) {
		printk("%s"
		       "%s[%d] bad frame in %s frame:%p ip:%lx sp:%lx orax:%lx",
		       task_pid_nr(current) > 1 ? KERN_INFO : KERN_EMERG,
		       me->comm, me->pid, where, frame,
		       regs->ip, regs->sp, regs->orig_ax);
		print_vma_addr(" in ", regs->ip);
		printk(KERN_CONT "\n");
	}

	force_sig(SIGSEGV, me);
}
