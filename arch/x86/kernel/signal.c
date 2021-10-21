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
#include <linux/kstrtox.h>
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
#include <linux/entry-common.h>
#include <linux/syscalls.h>

#include <asm/processor.h>
#include <asm/ucontext.h>
#include <asm/fpu/signal.h>
#include <asm/vdso.h>
#include <asm/mce.h>
#include <asm/sighandling.h>
#include <asm/vm86.h>

#ifdef CONFIG_X86_64
#include <linux/compat.h>
#include <asm/proto.h>
#include <asm/ia32_unistd.h>
#include <asm/fpu/xstate.h>
#endif /* CONFIG_X86_64 */

#include <asm/syscall.h>
#include <asm/sigframe.h>
#include <asm/signal.h>

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
# define CONTEXT_COPY_SIZE	offsetof(struct sigcontext, reserved1)
#else
# define CONTEXT_COPY_SIZE	sizeof(struct sigcontext)
#endif

static bool restore_sigcontext(struct pt_regs *regs,
			       struct sigcontext __user *usc,
			       unsigned long uc_flags)
{
	struct sigcontext sc;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	if (copy_from_user(&sc, usc, CONTEXT_COPY_SIZE))
		return false;

#ifdef CONFIG_X86_32
	set_user_gs(regs, sc.gs);
	regs->fs = sc.fs;
	regs->es = sc.es;
	regs->ds = sc.ds;
#endif /* CONFIG_X86_32 */

	regs->bx = sc.bx;
	regs->cx = sc.cx;
	regs->dx = sc.dx;
	regs->si = sc.si;
	regs->di = sc.di;
	regs->bp = sc.bp;
	regs->ax = sc.ax;
	regs->sp = sc.sp;
	regs->ip = sc.ip;

#ifdef CONFIG_X86_64
	regs->r8 = sc.r8;
	regs->r9 = sc.r9;
	regs->r10 = sc.r10;
	regs->r11 = sc.r11;
	regs->r12 = sc.r12;
	regs->r13 = sc.r13;
	regs->r14 = sc.r14;
	regs->r15 = sc.r15;
#endif /* CONFIG_X86_64 */

	/* Get CS/SS and force CPL3 */
	regs->cs = sc.cs | 0x03;
	regs->ss = sc.ss | 0x03;

	regs->flags = (regs->flags & ~FIX_EFLAGS) | (sc.flags & FIX_EFLAGS);
	/* disable syscall checks */
	regs->orig_ax = -1;

#ifdef CONFIG_X86_64
	/*
	 * Fix up SS if needed for the benefit of old DOSEMU and
	 * CRIU.
	 */
	if (unlikely(!(uc_flags & UC_STRICT_RESTORE_SS) && user_64bit_mode(regs)))
		force_valid_ss(regs);
#endif

	return fpu__restore_sig((void __user *)sc.fpstate,
			       IS_ENABLED(CONFIG_X86_32));
}

static __always_inline int
__unsafe_setup_sigcontext(struct sigcontext __user *sc, void __user *fpstate,
		     struct pt_regs *regs, unsigned long mask)
{
#ifdef CONFIG_X86_32
	unsafe_put_user(get_user_gs(regs),
				  (unsigned int __user *)&sc->gs, Efault);
	unsafe_put_user(regs->fs, (unsigned int __user *)&sc->fs, Efault);
	unsafe_put_user(regs->es, (unsigned int __user *)&sc->es, Efault);
	unsafe_put_user(regs->ds, (unsigned int __user *)&sc->ds, Efault);
#endif /* CONFIG_X86_32 */

	unsafe_put_user(regs->di, &sc->di, Efault);
	unsafe_put_user(regs->si, &sc->si, Efault);
	unsafe_put_user(regs->bp, &sc->bp, Efault);
	unsafe_put_user(regs->sp, &sc->sp, Efault);
	unsafe_put_user(regs->bx, &sc->bx, Efault);
	unsafe_put_user(regs->dx, &sc->dx, Efault);
	unsafe_put_user(regs->cx, &sc->cx, Efault);
	unsafe_put_user(regs->ax, &sc->ax, Efault);
#ifdef CONFIG_X86_64
	unsafe_put_user(regs->r8, &sc->r8, Efault);
	unsafe_put_user(regs->r9, &sc->r9, Efault);
	unsafe_put_user(regs->r10, &sc->r10, Efault);
	unsafe_put_user(regs->r11, &sc->r11, Efault);
	unsafe_put_user(regs->r12, &sc->r12, Efault);
	unsafe_put_user(regs->r13, &sc->r13, Efault);
	unsafe_put_user(regs->r14, &sc->r14, Efault);
	unsafe_put_user(regs->r15, &sc->r15, Efault);
#endif /* CONFIG_X86_64 */

	unsafe_put_user(current->thread.trap_nr, &sc->trapno, Efault);
	unsafe_put_user(current->thread.error_code, &sc->err, Efault);
	unsafe_put_user(regs->ip, &sc->ip, Efault);
#ifdef CONFIG_X86_32
	unsafe_put_user(regs->cs, (unsigned int __user *)&sc->cs, Efault);
	unsafe_put_user(regs->flags, &sc->flags, Efault);
	unsafe_put_user(regs->sp, &sc->sp_at_signal, Efault);
	unsafe_put_user(regs->ss, (unsigned int __user *)&sc->ss, Efault);
#else /* !CONFIG_X86_32 */
	unsafe_put_user(regs->flags, &sc->flags, Efault);
	unsafe_put_user(regs->cs, &sc->cs, Efault);
	unsafe_put_user(0, &sc->gs, Efault);
	unsafe_put_user(0, &sc->fs, Efault);
	unsafe_put_user(regs->ss, &sc->ss, Efault);
#endif /* CONFIG_X86_32 */

	unsafe_put_user(fpstate, (unsigned long __user *)&sc->fpstate, Efault);

	/* non-iBCS2 extensions.. */
	unsafe_put_user(mask, &sc->oldmask, Efault);
	unsafe_put_user(current->thread.cr2, &sc->cr2, Efault);
	return 0;
Efault:
	return -EFAULT;
}

#define unsafe_put_sigcontext(sc, fp, regs, set, label)			\
do {									\
	if (__unsafe_setup_sigcontext(sc, fp, regs, set->sig[0]))	\
		goto label;						\
} while(0);

#define unsafe_put_sigmask(set, frame, label) \
	unsafe_put_user(*(__u64 *)(set), \
			(__u64 __user *)&(frame)->uc.uc_sigmask, \
			label)

/*
 * Set up a signal frame.
 */

/* x86 ABI requires 16-byte alignment */
#define FRAME_ALIGNMENT	16UL

#define MAX_FRAME_PADDING	(FRAME_ALIGNMENT - 1)

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
	sp = ((sp + 4) & -FRAME_ALIGNMENT) - 4;
#else /* !CONFIG_X86_32 */
	sp = round_down(sp, FRAME_ALIGNMENT) - 8;
#endif
	return sp;
}

static void __user *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, size_t frame_size,
	     void __user **fpstate)
{
	/* Default to using normal stack */
	bool nested_altstack = on_sig_stack(regs->sp);
	bool entering_altstack = false;
	unsigned long math_size = 0;
	unsigned long sp = regs->sp;
	unsigned long buf_fx = 0;

	/* redzone */
	if (IS_ENABLED(CONFIG_X86_64))
		sp -= 128;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		/*
		 * This checks nested_altstack via sas_ss_flags(). Sensible
		 * programs use SS_AUTODISARM, which disables that check, and
		 * programs that don't use SS_AUTODISARM get compatible.
		 */
		if (sas_ss_flags(sp) == 0) {
			sp = current->sas_ss_sp + current->sas_ss_size;
			entering_altstack = true;
		}
	} else if (IS_ENABLED(CONFIG_X86_32) &&
		   !nested_altstack &&
		   regs->ss != __USER_DS &&
		   !(ka->sa.sa_flags & SA_RESTORER) &&
		   ka->sa.sa_restorer) {
		/* This is the legacy signal stack switching. */
		sp = (unsigned long) ka->sa.sa_restorer;
		entering_altstack = true;
	}

	sp = fpu__alloc_mathframe(sp, IS_ENABLED(CONFIG_X86_32),
				  &buf_fx, &math_size);
	*fpstate = (void __user *)sp;

	sp = align_sigframe(sp - frame_size);

	/*
	 * If we are on the alternate signal stack and would overflow it, don't.
	 * Return an always-bogus address instead so we will die with SIGSEGV.
	 */
	if (unlikely((nested_altstack || entering_altstack) &&
		     !__on_sig_stack(sp))) {

		if (show_unhandled_signals && printk_ratelimit())
			pr_info("%s[%d] overflowed sigaltstack\n",
				current->comm, task_pid_nr(current));

		return (void __user *)-1L;
	}

	/* save i387 and extended state */
	if (!copy_fpstate_to_sigframe(*fpstate, (void __user *)buf_fx, math_size))
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
	void __user *fp = NULL;

	frame = get_sigframe(&ksig->ka, regs, sizeof(*frame), &fp);

	if (!user_access_begin(frame, sizeof(*frame)))
		return -EFAULT;

	unsafe_put_user(sig, &frame->sig, Efault);
	unsafe_put_sigcontext(&frame->sc, fp, regs, set, Efault);
	unsafe_put_user(set->sig[1], &frame->extramask[0], Efault);
	if (current->mm->context.vdso)
		restorer = current->mm->context.vdso +
			vdso_image_32.sym___kernel_sigreturn;
	else
		restorer = &frame->retcode;
	if (ksig->ka.sa.sa_flags & SA_RESTORER)
		restorer = ksig->ka.sa.sa_restorer;

	/* Set up to return from userspace.  */
	unsafe_put_user(restorer, &frame->pretcode, Efault);

	/*
	 * This is popl %eax ; movl $__NR_sigreturn, %eax ; int $0x80
	 *
	 * WE DO NOT USE IT ANY MORE! It's only left here for historical
	 * reasons and because gdb uses it as a signature to notice
	 * signal handler stack frames.
	 */
	unsafe_put_user(*((u64 *)&retcode), (u64 *)frame->retcode, Efault);
	user_access_end();

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

Efault:
	user_access_end();
	return -EFAULT;
}

static int __setup_rt_frame(int sig, struct ksignal *ksig,
			    sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	void __user *restorer;
	void __user *fp = NULL;

	frame = get_sigframe(&ksig->ka, regs, sizeof(*frame), &fp);

	if (!user_access_begin(frame, sizeof(*frame)))
		return -EFAULT;

	unsafe_put_user(sig, &frame->sig, Efault);
	unsafe_put_user(&frame->info, &frame->pinfo, Efault);
	unsafe_put_user(&frame->uc, &frame->puc, Efault);

	/* Create the ucontext.  */
	if (static_cpu_has(X86_FEATURE_XSAVE))
		unsafe_put_user(UC_FP_XSTATE, &frame->uc.uc_flags, Efault);
	else
		unsafe_put_user(0, &frame->uc.uc_flags, Efault);
	unsafe_put_user(0, &frame->uc.uc_link, Efault);
	unsafe_save_altstack(&frame->uc.uc_stack, regs->sp, Efault);

	/* Set up to return from userspace.  */
	restorer = current->mm->context.vdso +
		vdso_image_32.sym___kernel_rt_sigreturn;
	if (ksig->ka.sa.sa_flags & SA_RESTORER)
		restorer = ksig->ka.sa.sa_restorer;
	unsafe_put_user(restorer, &frame->pretcode, Efault);

	/*
	 * This is movl $__NR_rt_sigreturn, %ax ; int $0x80
	 *
	 * WE DO NOT USE IT ANY MORE! It's only left here for historical
	 * reasons and because gdb uses it as a signature to notice
	 * signal handler stack frames.
	 */
	unsafe_put_user(*((u64 *)&rt_retcode), (u64 *)frame->retcode, Efault);
	unsafe_put_sigcontext(&frame->uc.uc_mcontext, fp, regs, set, Efault);
	unsafe_put_sigmask(set, frame, Efault);
	user_access_end();
	
	if (copy_siginfo_to_user(&frame->info, &ksig->info))
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
Efault:
	user_access_end();
	return -EFAULT;
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

	/* x86-64 should always use SA_RESTORER. */
	if (!(ksig->ka.sa.sa_flags & SA_RESTORER))
		return -EFAULT;

	frame = get_sigframe(&ksig->ka, regs, sizeof(struct rt_sigframe), &fp);
	uc_flags = frame_uc_flags(regs);

	if (!user_access_begin(frame, sizeof(*frame)))
		return -EFAULT;

	/* Create the ucontext.  */
	unsafe_put_user(uc_flags, &frame->uc.uc_flags, Efault);
	unsafe_put_user(0, &frame->uc.uc_link, Efault);
	unsafe_save_altstack(&frame->uc.uc_stack, regs->sp, Efault);

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	unsafe_put_user(ksig->ka.sa.sa_restorer, &frame->pretcode, Efault);
	unsafe_put_sigcontext(&frame->uc.uc_mcontext, fp, regs, set, Efault);
	unsafe_put_sigmask(set, frame, Efault);
	user_access_end();

	if (ksig->ka.sa.sa_flags & SA_SIGINFO) {
		if (copy_siginfo_to_user(&frame->info, &ksig->info))
			return -EFAULT;
	}

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
	 * SS value.  We also have a compatibility issue here: DOSEMU
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

Efault:
	user_access_end();
	return -EFAULT;
}
#endif /* CONFIG_X86_32 */

#ifdef CONFIG_X86_X32_ABI
static int x32_copy_siginfo_to_user(struct compat_siginfo __user *to,
		const struct kernel_siginfo *from)
{
	struct compat_siginfo new;

	copy_siginfo_to_external32(&new, from);
	if (from->si_signo == SIGCHLD) {
		new._sifields._sigchld_x32._utime = from->si_utime;
		new._sifields._sigchld_x32._stime = from->si_stime;
	}
	if (copy_to_user(to, &new, sizeof(struct compat_siginfo)))
		return -EFAULT;
	return 0;
}

int copy_siginfo_to_user32(struct compat_siginfo __user *to,
			   const struct kernel_siginfo *from)
{
	if (in_x32_syscall())
		return x32_copy_siginfo_to_user(to, from);
	return __copy_siginfo_to_user32(to, from);
}
#endif /* CONFIG_X86_X32_ABI */

static int x32_setup_rt_frame(struct ksignal *ksig,
			      compat_sigset_t *set,
			      struct pt_regs *regs)
{
#ifdef CONFIG_X86_X32_ABI
	struct rt_sigframe_x32 __user *frame;
	unsigned long uc_flags;
	void __user *restorer;
	void __user *fp = NULL;

	if (!(ksig->ka.sa.sa_flags & SA_RESTORER))
		return -EFAULT;

	frame = get_sigframe(&ksig->ka, regs, sizeof(*frame), &fp);

	uc_flags = frame_uc_flags(regs);

	if (!user_access_begin(frame, sizeof(*frame)))
		return -EFAULT;

	/* Create the ucontext.  */
	unsafe_put_user(uc_flags, &frame->uc.uc_flags, Efault);
	unsafe_put_user(0, &frame->uc.uc_link, Efault);
	unsafe_compat_save_altstack(&frame->uc.uc_stack, regs->sp, Efault);
	unsafe_put_user(0, &frame->uc.uc__pad0, Efault);
	restorer = ksig->ka.sa.sa_restorer;
	unsafe_put_user(restorer, (unsigned long __user *)&frame->pretcode, Efault);
	unsafe_put_sigcontext(&frame->uc.uc_mcontext, fp, regs, set, Efault);
	unsafe_put_sigmask(set, frame, Efault);
	user_access_end();

	if (ksig->ka.sa.sa_flags & SA_SIGINFO) {
		if (x32_copy_siginfo_to_user(&frame->info, &ksig->info))
			return -EFAULT;
	}

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
#ifdef CONFIG_X86_X32_ABI
Efault:
	user_access_end();
	return -EFAULT;
#endif
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
	if (__get_user(set.sig[0], &frame->sc.oldmask) ||
	    __get_user(set.sig[1], &frame->extramask[0]))
		goto badframe;

	set_current_blocked(&set);

	/*
	 * x86_32 has no uc_flags bits relevant to restore_sigcontext.
	 * Save a few cycles by skipping the __get_user.
	 */
	if (!restore_sigcontext(regs, &frame->sc, 0))
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
	if (__get_user(*(__u64 *)&set, (__u64 __user *)&frame->uc.uc_sigmask))
		goto badframe;
	if (__get_user(uc_flags, &frame->uc.uc_flags))
		goto badframe;

	set_current_blocked(&set);

	if (!restore_sigcontext(regs, &frame->uc.uc_mcontext, uc_flags))
		goto badframe;

	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->ax;

badframe:
	signal_fault(regs, frame, "rt_sigreturn");
	return 0;
}

/*
 * There are four different struct types for signal frame: sigframe_ia32,
 * rt_sigframe_ia32, rt_sigframe_x32, and rt_sigframe. Use the worst case
 * -- the largest size. It means the size for 64-bit apps is a bit more
 * than needed, but this keeps the code simple.
 */
#if defined(CONFIG_X86_32) || defined(CONFIG_IA32_EMULATION)
# define MAX_FRAME_SIGINFO_UCTXT_SIZE	sizeof(struct sigframe_ia32)
#else
# define MAX_FRAME_SIGINFO_UCTXT_SIZE	sizeof(struct rt_sigframe)
#endif

/*
 * The FP state frame contains an XSAVE buffer which must be 64-byte aligned.
 * If a signal frame starts at an unaligned address, extra space is required.
 * This is the max alignment padding, conservatively.
 */
#define MAX_XSAVE_PADDING	63UL

/*
 * The frame data is composed of the following areas and laid out as:
 *
 * -------------------------
 * | alignment padding     |
 * -------------------------
 * | (f)xsave frame        |
 * -------------------------
 * | fsave header          |
 * -------------------------
 * | alignment padding     |
 * -------------------------
 * | siginfo + ucontext    |
 * -------------------------
 */

/* max_frame_size tells userspace the worst case signal stack size. */
static unsigned long __ro_after_init max_frame_size;

void __init init_sigframe_size(void)
{
	max_frame_size = MAX_FRAME_SIGINFO_UCTXT_SIZE + MAX_FRAME_PADDING;

	max_frame_size += fpu__get_fpstate_size() + MAX_XSAVE_PADDING;

	/* Userspace expects an aligned size. */
	max_frame_size = round_up(max_frame_size, FRAME_ALIGNMENT);

	pr_info("max sigframe size: %lu\n", max_frame_size);
}

unsigned long get_sigframe_size(void)
{
	return max_frame_size;
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
	if (syscall_get_nr(current, regs) != -1) {
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
			fallthrough;
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
		fpu__clear_user_states(fpu);
	}
	signal_setup_done(failed, ksig, stepping);
}

static inline unsigned long get_nr_restart_syscall(const struct pt_regs *regs)
{
#ifdef CONFIG_IA32_EMULATION
	if (current->restart_block.arch_data & TS_COMPAT)
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
void arch_do_signal_or_restart(struct pt_regs *regs, bool has_signal)
{
	struct ksignal ksig;

	if (has_signal && get_signal(&ksig)) {
		/* Whee! Actually deliver the signal.  */
		handle_signal(&ksig, regs);
		return;
	}

	/* Did we come from a system call? */
	if (syscall_get_nr(current, regs) != -1) {
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

#ifdef CONFIG_DYNAMIC_SIGFRAME
#ifdef CONFIG_STRICT_SIGALTSTACK_SIZE
static bool strict_sigaltstack_size __ro_after_init = true;
#else
static bool strict_sigaltstack_size __ro_after_init = false;
#endif

static int __init strict_sas_size(char *arg)
{
	return kstrtobool(arg, &strict_sigaltstack_size);
}
__setup("strict_sas_size", strict_sas_size);

/*
 * MINSIGSTKSZ is 2048 and can't be changed despite the fact that AVX512
 * exceeds that size already. As such programs might never use the
 * sigaltstack they just continued to work. While always checking against
 * the real size would be correct, this might be considered a regression.
 *
 * Therefore avoid the sanity check, unless enforced by kernel config or
 * command line option.
 */
bool sigaltstack_size_valid(size_t ss_size)
{
	lockdep_assert_held(&current->sighand->siglock);

	if (strict_sigaltstack_size)
		return ss_size > get_sigframe_size();

	return true;
}
#endif /* CONFIG_DYNAMIC_SIGFRAME */

#ifdef CONFIG_X86_X32_ABI
COMPAT_SYSCALL_DEFINE0(x32_rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe_x32 __user *frame;
	sigset_t set;
	unsigned long uc_flags;

	frame = (struct rt_sigframe_x32 __user *)(regs->sp - 8);

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], (__u64 __user *)&frame->uc.uc_sigmask))
		goto badframe;
	if (__get_user(uc_flags, &frame->uc.uc_flags))
		goto badframe;

	set_current_blocked(&set);

	if (!restore_sigcontext(regs, &frame->uc.uc_mcontext, uc_flags))
		goto badframe;

	if (compat_restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->ax;

badframe:
	signal_fault(regs, frame, "x32 rt_sigreturn");
	return 0;
}
#endif
