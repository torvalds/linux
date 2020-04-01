// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen SuSE Labs
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *  2000-06-20  Pentium III FXSR, SSE support by Gareth Hughes
 *  2000-2002   x86-64 support by Andi Kleen
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/tracehook.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/personality.h>
#include <linux/uaccess.h>
#include <linux/user-return-notifier.h>
#include <linux/uprobes.h>
#include <linux/context_tracking.h>
#include <linux/syscalls.h>

#include <asm/processor.h>
#include <asm/ucontext.h>
#include <asm/fpu/internal.h>
#include <asm/fpu/signal.h>
#include <asm/vdso.h>
#include <asm/mce.h>
#include <asm/sighandling.h>
#include <asm/vm86.h>

#ifdef CONFIG_X86_64
#include <asm/proto.h>
#include <asm/ia32_unistd.h>
#endif /* CONFIG_X86_64 */

#include <asm/syscall.h>
#include <asm/syscalls.h>

#include <asm/sigframe.h>
#include <asm/signal.h>

#define COPY(x)			do {			\
	get_user_ex(regs->x, &sc->x);			\
} while (0)

#define GET_SEG(seg)		({			\
	unsigned short tmp;				\
	get_user_ex(tmp, &sc->seg);			\
	tmp;						\
})

#define COPY_SEG(seg)		do {			\
	regs->seg = GET_SEG(seg);			\
} while (0)

#define COPY_SEG_CPL3(seg)	do {			\
	regs->seg = GET_SEG(seg) | 3;			\
} while (0)

#ifdef CONFIG_X86_64
/*
 * If regs->ss will cause an IRET fault, change it.  Otherwise leave it
 * alone.  Using this generally makes no sense unless
 * user_64bit_mode(regs) would return true.
 */
static void force_valid_ss(struct pt_regs *regs)
{
	u32 ar;
	asm volatile ("lar %[old_ss], %[ar]\n\t"
		      "jz 1f\n\t"		/* If invalid: */
		      "xorl %[ar], %[ar]\n\t"	/* set ar = 0 */
		      "1:"
		      : [ar] "=r" (ar)
		      : [old_ss] "rm" ((u16)regs->ss));

	/*
	 * For a valid 64-bit user context, we need DPL 3, type
	 * read-write data or read-write exp-down data, and S and P
	 * set.  We can't use VERW because VERW doesn't check the
	 * P bit.
	 */
	ar &= AR_DPL_MASK | AR_S | AR_P | AR_TYPE_MASK;
	if (ar != (AR_DPL3 | AR_S | AR_P | AR_TYPE_RWDATA) &&
	    ar != (AR_DPL3 | AR_S | AR_P | AR_TYPE_RWDATA_EXPDOWN))
		regs->ss = __USER_DS;
}
#endif

static int restore_sigcontext(struct pt_regs *regs,
			      struct sigcontext __user *sc,
			      unsigned long uc_flags)
{
	unsigned long buf_val;
	void __user *buf;
	unsigned int tmpflags;
	unsigned int err = 0;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	get_user_try {

#ifdef CONFIG_X86_32
		set_user_gs(regs, GET_SEG(gs));
		COPY_SEG(fs);
		COPY_SEG(es);
		COPY_SEG(ds);
#endif /* CONFIG_X86_32 */

		COPY(di); COPY(si); COPY(bp); COPY(sp); COPY(bx);
		COPY(dx); COPY(cx); COPY(ip); COPY(ax);

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

		COPY_SEG_CPL3(cs);
		COPY_SEG_CPL3(ss);

		get_user_ex(tmpflags, &sc->flags);
		regs->flags = (regs->flags & ~FIX_EFLAGS) | (tmpflags & FIX_EFLAGS);
		regs->orig_ax = -1;		/* disable syscall checks */

		get_user_ex(buf_val, &sc->fpstate);
		buf = (void __user *)buf_val;
	} get_user_catch(err);

#ifdef CONFIG_X86_64
	/*
	 * Fix up SS if needed for the benefit of old DOSEMU and
	 * CRIU.
	 */
	if (unlikely(!(uc_flags & UC_STRICT_RESTORE_SS) && user_64bit_mode(regs)))
		force_valid_ss(regs);
#endif

	err |= fpu__restore_sig(buf, IS_ENABLED(CONFIG_X86_32));

	return err;
}

int setup_sigcontext(struct sigcontext __user *sc, void __user *fpstate,
		     struct pt_regs *regs, unsigned long mask)
{
	int err = 0;

	put_user_try {

#ifdef CONFIG_X86_32
		put_user_ex(get_user_gs(regs), (unsigned int __user *)&sc->gs);
		put_user_ex(regs->fs, (unsigned int __user *)&sc->fs);
		put_user_ex(regs->es, (unsigned int __user *)&sc->es);
		put_user_ex(regs->ds, (unsigned int __user *)&sc->ds);
#endif /* CONFIG_X86_32 */

		put_user_ex(regs->di, &sc->di);
		put_user_ex(regs->si, &sc->si);
		put_user_ex(regs->bp, &sc->bp);
		put_user_ex(regs->sp, &sc->sp);
		put_user_ex(regs->bx, &sc->bx);
		put_user_ex(regs->dx, &sc->dx);
		put_user_ex(regs->cx, &sc->cx);
		put_user_ex(regs->ax, &sc->ax);
#ifdef CONFIG_X86_64
		put_user_ex(regs->r8, &sc->r8);
		put_user_ex(regs->r9, &sc->r9);
		put_user_ex(regs->r10, &sc->r10);
		put_user_ex(regs->r11, &sc->r11);
		put_user_ex(regs->r12, &sc->r12);
		put_user_ex(regs->r13, &sc->r13);
		put_user_ex(regs->r14, &sc->r14);
		put_user_ex(regs->r15, &sc->r15);
#endif /* CONFIG_X86_64 */

		put_user_ex(current->thread.trap_nr, &sc->trapno);
		put_user_ex(current->thread.error_code, &sc->err);
		put_user_ex(regs->ip, &sc->ip);
#ifdef CONFIG_X86_32
		put_user_ex(regs->cs, (unsigned int __user *)&sc->cs);
		put_user_ex(regs->flags, &sc->flags);
		put_user_ex(regs->sp, &sc->sp_at_signal);
		put_user_ex(regs->ss, (unsigned int __user *)&sc->ss);
#else /* !CONFIG_X86_32 */
		put_user_ex(regs->flags, &sc->flags);
		put_user_ex(regs->cs, &sc->cs);
		put_user_ex(0, &sc->gs);
		put_user_ex(0, &sc->fs);
		put_user_ex(regs->ss, &sc->ss);
#endif /* CONFIG_X86_32 */

		put_user_ex(fpstate, (unsigned long __user *)&sc->fpstate);

		/* non-iBCS2 extensions.. */
		put_user_ex(mask, &sc->oldmask);
		put_user_ex(current->thread.cr2, &sc->cr2);
	} put_user_catch(err);

	return err;
}

/*
 * Set up a signal frame.
 */

/*
 * Determine which stack to use..
 */
static unsigned long align_sigframe(unsigned long sp)
{
#ifdef CONFIG_X86_32
	/*
	 * Align the stack pointer according to the i386 ABI,
	 * i.e. so that on function entry ((sp + 4) & 15) == 0.
	 */
	sp = ((sp + 4) & -16ul) - 4;
#else /* !CONFIG_X86_32 */
	sp = round_down(sp, 16) - 8;
#endif
	return sp;
}

static void __user *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, size_t frame_size,
	     void __user **fpstate)
{
	/* Default to using normal stack */
	unsigned long math_size = 0;
	unsigned long sp = regs->sp;
	unsigned long buf_fx = 0;
	int onsigstack = on_sig_stack(sp);
	int ret;

	/* redzone */
	if (IS_ENABLED(CONFIG_X86_64))
		sp -= 128;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (sas_ss_flags(sp) == 0)
			sp = current->sas_ss_sp + current->sas_ss_size;
	} else if (IS_ENABLED(CONFIG_X86_32) &&
		   !onsigstack &&
		   regs->ss != __USER_DS &&
		   !(ka->sa.sa_flags & SA_RESTORER) &&
		   ka->sa.sa_restorer) {
		/* This is the legacy signal stack switching. */
		sp = (unsigned long) ka->sa.sa_restorer;
	}

	sp = fpu__alloc_mathframe(sp, IS_ENABLED(CONFIG_X86_32),
				  &buf_fx, &math_size);
	*fpstate = (void __user *)sp;

	sp = align_sigframe(sp - frame_size);

	/*
	 * If we are on the alternate signal stack and would overflow it, don't.
	 * Return an always-bogus address instead so we will die with SIGSEGV.
	 */
	if (onsigstack && !likely(on_sig_stack(sp)))
		return (void __user *)-1L;

	/* save i387 and extended state */
	ret = copy_fpstate_to_sigframe(*fpstate, (void __user *)buf_fx, math_size);
	if (ret < 0)
		return (void __user *)-1L;

	return (void __user *)sp;
}

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

static int
__setup_frame(int sig, struct ksignal *ksig, sigset_t *set,
	      struct pt_regs *regs)
{
	struct sigframe __user *frame;
	void __user *restorer;
	int err = 0;
	void __user *fpstate = NULL;

	frame = get_sigframe(&ksig->ka, regs, sizeof(*frame), &fpstate);

	if (!access_ok(frame, sizeof(*frame)))
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
		restorer = current->mm->context.vdso +
			vdso_image_32.sym___kernel_sigreturn;
	else
		restorer = &frame->retcode;
	if (ksig->ka.sa.sa_flags & SA_RESTORER)
		restorer = ksig->ka.sa.sa_restorer;

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
	regs->ip = (unsigned long)ksig->ka.sa.sa_handler;
	regs->ax = (unsigned long)sig;
	regs->dx = 0;
	regs->cx = 0;

	regs->ds = __USER_DS;
	regs->es = __USER_DS;
	regs->ss = __USER_DS;
	regs->cs = __USER_CS;

	return 0;
}

static int __setup_rt_frame(int sig, struct ksignal *ksig,
			    sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	void __user *restorer;
	int err = 0;
	void __user *fpstate = NULL;

	frame = get_sigframe(&ksig->ka, regs, sizeof(*frame), &fpstate);

	if (!access_ok(frame, sizeof(*frame)))
		return -EFAULT;

	put_user_try {
		put_user_ex(sig, &frame->sig);
		put_user_ex(&frame->info, &frame->pinfo);
		put_user_ex(&frame->uc, &frame->puc);

		/* Create the ucontext.  */
		if (static_cpu_has(X86_FEATURE_XSAVE))
			put_user_ex(UC_FP_XSTATE, &frame->uc.uc_flags);
		else
			put_user_ex(0, &frame->uc.uc_flags);
		put_user_ex(0, &frame->uc.uc_link);
		save_altstack_ex(&frame->uc.uc_stack, regs->sp);

		/* Set up to return from userspace.  */
		restorer = current->mm->context.vdso +
			vdso_image_32.sym___kernel_rt_sigreturn;
		if (ksig->ka.sa.sa_flags & SA_RESTORER)
			restorer = ksig->ka.sa.sa_restorer;
		put_user_ex(restorer, &frame->pretcode);

		/*
		 * This is movl $__NR_rt_sigreturn, %ax ; int $0x80
		 *
		 * WE DO NOT USE IT ANY MORE! It's only left here for historical
		 * reasons and because gdb uses it as a signature to notice
		 * signal handler stack frames.
		 */
		put_user_ex(*((u64 *)&rt_retcode), (u64 *)frame->retcode);
	} put_user_catch(err);
	
	err |= copy_siginfo_to_user(&frame->info, &ksig->info);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, fpstate,
				regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	if (err)
		return -EFAULT;

	/* Set up registers for signal handler */
	regs->sp = (unsigned long)frame;
	regs->ip = (unsigned long)ksig->ka.sa.sa_handler;
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
static unsigned long frame_uc_flags(struct pt_regs *regs)
{
	unsigned long flags;

	if (boot_cpu_has(X86_FEATURE_XSAVE))
		flags = UC_FP_XSTATE | UC_SIGCONTEXT_SS;
	else
		flags = UC_SIGCONTEXT_SS;

	if (likely(user_64bit_mode(regs)))
		flags |= UC_STRICT_RESTORE_SS;

	return flags;
}

static int __setup_rt_frame(int sig, struct ksignal *ksig,
			    sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	void __user *fp = NULL;
	unsigned long uc_flags;
	int err = 0;

	frame = get_sigframe(&ksig->ka, regs, sizeof(struct rt_sigframe), &fp);

	if (!access_ok(frame, sizeof(*frame)))
		return -EFAULT;

	if (ksig->ka.sa.sa_flags & SA_SIGINFO) {
		if (copy_siginfo_to_user(&frame->info, &ksig->info))
			return -EFAULT;
	}

	uc_flags = frame_uc_flags(regs);

	put_user_try {
		/* Create the ucontext.  */
		put_user_ex(uc_flags, &frame->uc.uc_flags);
		put_user_ex(0, &frame->uc.uc_link);
		save_altstack_ex(&frame->uc.uc_stack, regs->sp);

		/* Set up to return from userspace.  If provided, use a stub
		   already in userspace.  */
		/* x86-64 should always use SA_RESTORER. */
		if (ksig->ka.sa.sa_flags & SA_RESTORER) {
			put_user_ex(ksig->ka.sa.sa_restorer, &frame->pretcode);
		} else {
			/* could use a vstub here */
			err |= -EFAULT;
		}
	} put_user_catch(err);

	err |= setup_sigcontext(&frame->uc.uc_mcontext, fp, regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

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
	regs->ip = (unsigned long) ksig->ka.sa.sa_handler;

	regs->sp = (unsigned long)frame;

	/*
	 * Set up the CS and SS registers to run signal handlers in
	 * 64-bit mode, even if the handler happens to be interrupting
	 * 32-bit or 16-bit code.
	 *
	 * SS is subtle.  In 64-bit mode, we don't need any particular
	 * SS descriptor, but we do need SS to be valid.  It's possible
	 * that the old SS is entirely bogus -- this can happen if the
	 * signal we're trying to deliver is #GP or #SS caused by a bad
	 * SS value.  We also have a compatbility issue here: DOSEMU
	 * relies on the contents of the SS register indicating the
	 * SS value at the time of the signal, even though that code in
	 * DOSEMU predates sigreturn's ability to restore SS.  (DOSEMU
	 * avoids relying on sigreturn to restore SS; instead it uses
	 * a trampoline.)  So we do our best: if the old SS was valid,
	 * we keep it.  Otherwise we replace it.
	 */
	regs->cs = __USER_CS;

	if (unlikely(regs->ss != __USER_DS))
		force_valid_ss(regs);

	return 0;
}
#endif /* CONFIG_X86_32 */

static int x32_setup_rt_frame(struct ksignal *ksig,
			      compat_sigset_t *set,
			      struct pt_regs *regs)
{
#ifdef CONFIG_X86_X32_ABI
	struct rt_sigframe_x32 __user *frame;
	unsigned long uc_flags;
	void __user *restorer;
	int err = 0;
	void __user *fpstate = NULL;

	frame = get_sigframe(&ksig->ka, regs, sizeof(*frame), &fpstate);

	if (!access_ok(frame, sizeof(*frame)))
		return -EFAULT;

	if (ksig->ka.sa.sa_flags & SA_SIGINFO) {
		if (__copy_siginfo_to_user32(&frame->info, &ksig->info, true))
			return -EFAULT;
	}

	uc_flags = frame_uc_flags(regs);

	put_user_try {
		/* Create the ucontext.  */
		put_user_ex(uc_flags, &frame->uc.uc_flags);
		put_user_ex(0, &frame->uc.uc_link);
		compat_save_altstack_ex(&frame->uc.uc_stack, regs->sp);
		put_user_ex(0, &frame->uc.uc__pad0);

		if (ksig->ka.sa.sa_flags & SA_RESTORER) {
			restorer = ksig->ka.sa.sa_restorer;
		} else {
			/* could use a vstub here */
			restorer = NULL;
			err |= -EFAULT;
		}
		put_user_ex(restorer, (unsigned long __user *)&frame->pretcode);
	} put_user_catch(err);

	err |= setup_sigcontext(&frame->uc.uc_mcontext, fpstate,
				regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	if (err)
		return -EFAULT;

	/* Set up registers for signal handler */
	regs->sp = (unsigned long) frame;
	regs->ip = (unsigned long) ksig->ka.sa.sa_handler;

	/* We use the x32 calling convention here... */
	regs->di = ksig->sig;
	regs->si = (unsigned long) &frame->info;
	regs->dx = (unsigned long) &frame->uc;

	loadsegment(ds, __USER_DS);
	loadsegment(es, __USER_DS);

	regs->cs = __USER_CS;
	regs->ss = __USER_DS;
#endif	/* CONFIG_X86_X32_ABI */

	return 0;
}

/*
 * Do a signal return; undo the signal stack.
 */
#ifdef CONFIG_X86_32
SYSCALL_DEFINE0(sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct sigframe __user *frame;
	sigset_t set;

	frame = (struct sigframe __user *)(regs->sp - 8);

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.oldmask) || (_NSIG_WORDS > 1
		&& __copy_from_user(&set.sig[1], &frame->extramask,
				    sizeof(frame->extramask))))
		goto badframe;

	set_current_blocked(&set);

	/*
	 * x86_32 has no uc_flags bits relevant to restore_sigcontext.
	 * Save a few cycles by skipping the __get_user.
	 */
	if (restore_sigcontext(regs, &frame->sc, 0))
		goto badframe;
	return regs->ax;

badframe:
	signal_fault(regs, frame, "sigreturn");

	return 0;
}
#endif /* CONFIG_X86_32 */

SYSCALL_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe __user *frame;
	sigset_t set;
	unsigned long uc_flags;

	frame = (struct rt_sigframe __user *)(regs->sp - sizeof(long));
	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;
	if (__get_user(uc_flags, &frame->uc.uc_flags))
		goto badframe;

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext, uc_flags))
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->ax;

badframe:
	signal_fault(regs, frame, "rt_sigreturn");
	return 0;
}

static inline int is_ia32_compat_frame(struct ksignal *ksig)
{
	return IS_ENABLED(CONFIG_IA32_EMULATION) &&
		ksig->ka.sa.sa_flags & SA_IA32_ABI;
}

static inline int is_ia32_frame(struct ksignal *ksig)
{
	return IS_ENABLED(CONFIG_X86_32) || is_ia32_compat_frame(ksig);
}

static inline int is_x32_frame(struct ksignal *ksig)
{
	return IS_ENABLED(CONFIG_X86_X32_ABI) &&
		ksig->ka.sa.sa_flags & SA_X32_ABI;
}

static int
setup_rt_frame(struct ksignal *ksig, struct pt_regs *regs)
{
	int usig = ksig->sig;
	sigset_t *set = sigmask_to_save();
	compat_sigset_t *cset = (compat_sigset_t *) set;

	/* Perform fixup for the pre-signal frame. */
	rseq_signal_deliver(ksig, regs);

	/* Set up the stack frame */
	if (is_ia32_frame(ksig)) {
		if (ksig->ka.sa.sa_flags & SA_SIGINFO)
			return ia32_setup_rt_frame(usig, ksig, cset, regs);
		else
			return ia32_setup_frame(usig, ksig, cset, regs);
	} else if (is_x32_frame(ksig)) {
		return x32_setup_rt_frame(ksig, cset, regs);
	} else {
		return __setup_rt_frame(ksig->sig, ksig, set, regs);
	}
}

static void
handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	bool stepping, failed;
	struct fpu *fpu = &current->thread.fpu;

	if (v8086_mode(regs))
		save_v86_state((struct kernel_vm86_regs *) regs, VM86_SIGNAL);

	/* Are we from a system call? */
	if (syscall_get_nr(current, regs) >= 0) {
		/* If so, check system call restarting.. */
		switch (syscall_get_error(current, regs)) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->ax = -EINTR;
			break;

		case -ERESTARTSYS:
			if (!(ksig->ka.sa.sa_flags & SA_RESTART)) {
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
	 * If TF is set due to a debugger (TIF_FORCED_TF), clear TF now
	 * so that register information in the sigcontext is correct and
	 * then notify the tracer before entering the signal handler.
	 */
	stepping = test_thread_flag(TIF_SINGLESTEP);
	if (stepping)
		user_disable_single_step(current);

	failed = (setup_rt_frame(ksig, regs) < 0);
	if (!failed) {
		/*
		 * Clear the direction flag as per the ABI for function entry.
		 *
		 * Clear RF when entering the signal handler, because
		 * it might disable possible debug exception from the
		 * signal handler.
		 *
		 * Clear TF for the case when it wasn't set by debugger to
		 * avoid the recursive send_sigtrap() in SIGTRAP handler.
		 */
		regs->flags &= ~(X86_EFLAGS_DF|X86_EFLAGS_RF|X86_EFLAGS_TF);
		/*
		 * Ensure the signal handler starts with the new fpu state.
		 */
		fpu__clear(fpu);
	}
	signal_setup_done(failed, ksig, stepping);
}

static inline unsigned long get_nr_restart_syscall(const struct pt_regs *regs)
{
	/*
	 * This function is fundamentally broken as currently
	 * implemented.
	 *
	 * The idea is that we want to trigger a call to the
	 * restart_block() syscall and that we want in_ia32_syscall(),
	 * in_x32_syscall(), etc. to match whatever they were in the
	 * syscall being restarted.  We assume that the syscall
	 * instruction at (regs->ip - 2) matches whatever syscall
	 * instruction we used to enter in the first place.
	 *
	 * The problem is that we can get here when ptrace pokes
	 * syscall-like values into regs even if we're not in a syscall
	 * at all.
	 *
	 * For now, we maintain historical behavior and guess based on
	 * stored state.  We could do better by saving the actual
	 * syscall arch in restart_block or (with caveats on x32) by
	 * checking if regs->ip points to 'int $0x80'.  The current
	 * behavior is incorrect if a tracer has a different bitness
	 * than the tracee.
	 */
#ifdef CONFIG_IA32_EMULATION
	if (current_thread_info()->status & (TS_COMPAT|TS_I386_REGS_POKED))
		return __NR_ia32_restart_syscall;
#endif
#ifdef CONFIG_X86_X32_ABI
	return __NR_restart_syscall | (regs->orig_ax & __X32_SYSCALL_BIT);
#else
	return __NR_restart_syscall;
#endif
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
void do_signal(struct pt_regs *regs)
{
	struct ksignal ksig;

	if (get_signal(&ksig)) {
		/* Whee! Actually deliver the signal.  */
		handle_signal(&ksig, regs);
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
			regs->ax = get_nr_restart_syscall(regs);
			regs->ip -= 2;
			break;
		}
	}

	/*
	 * If there's no signal to deliver, we just put the saved sigmask
	 * back.
	 */
	restore_saved_sigmask();
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
		print_vma_addr(KERN_CONT " in ", regs->ip);
		pr_cont("\n");
	}

	force_sig(SIGSEGV);
}

#ifdef CONFIG_X86_X32_ABI
asmlinkage long sys32_x32_rt_sigreturn(void)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe_x32 __user *frame;
	sigset_t set;
	unsigned long uc_flags;

	frame = (struct rt_sigframe_x32 __user *)(regs->sp - 8);

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;
	if (__get_user(uc_flags, &frame->uc.uc_flags))
		goto badframe;

	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext, uc_flags))
		goto badframe;

	if (compat_restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->ax;

badframe:
	signal_fault(regs, frame, "x32 rt_sigreturn");
	return 0;
}
#endif
