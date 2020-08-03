// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/x86_64/ia32/ia32_signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *  2000-06-20  Pentium III FXSR, SSE support by Gareth Hughes
 *  2000-12-*   x86-64 compatibility mode signal handling by Andi Kleen
 */

#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/personality.h>
#include <linux/compat.h>
#include <linux/binfmts.h>
#include <linux/syscalls.h>
#include <asm/ucontext.h>
#include <linux/uaccess.h>
#include <asm/fpu/internal.h>
#include <asm/fpu/signal.h>
#include <asm/ptrace.h>
#include <asm/ia32_unistd.h>
#include <asm/user32.h>
#include <uapi/asm/sigcontext.h>
#include <asm/proto.h>
#include <asm/vdso.h>
#include <asm/sigframe.h>
#include <asm/sighandling.h>
#include <asm/smap.h>

static inline void reload_segments(struct sigcontext_32 *sc)
{
	unsigned int cur;

	savesegment(gs, cur);
	if ((sc->gs | 0x03) != cur)
		load_gs_index(sc->gs | 0x03);
	savesegment(fs, cur);
	if ((sc->fs | 0x03) != cur)
		loadsegment(fs, sc->fs | 0x03);
	savesegment(ds, cur);
	if ((sc->ds | 0x03) != cur)
		loadsegment(ds, sc->ds | 0x03);
	savesegment(es, cur);
	if ((sc->es | 0x03) != cur)
		loadsegment(es, sc->es | 0x03);
}

/*
 * Do a signal return; undo the signal stack.
 */
static int ia32_restore_sigcontext(struct pt_regs *regs,
				   struct sigcontext_32 __user *usc)
{
	struct sigcontext_32 sc;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	if (unlikely(copy_from_user(&sc, usc, sizeof(sc))))
		return -EFAULT;

	/* Get only the ia32 registers. */
	regs->bx = sc.bx;
	regs->cx = sc.cx;
	regs->dx = sc.dx;
	regs->si = sc.si;
	regs->di = sc.di;
	regs->bp = sc.bp;
	regs->ax = sc.ax;
	regs->sp = sc.sp;
	regs->ip = sc.ip;

	/* Get CS/SS and force CPL3 */
	regs->cs = sc.cs | 0x03;
	regs->ss = sc.ss | 0x03;

	regs->flags = (regs->flags & ~FIX_EFLAGS) | (sc.flags & FIX_EFLAGS);
	/* disable syscall checks */
	regs->orig_ax = -1;

	/*
	 * Reload fs and gs if they have changed in the signal
	 * handler.  This does not handle long fs/gs base changes in
	 * the handler, but does not clobber them at least in the
	 * normal case.
	 */
	reload_segments(&sc);
	return fpu__restore_sig(compat_ptr(sc.fpstate), 1);
}

COMPAT_SYSCALL_DEFINE0(sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct sigframe_ia32 __user *frame = (struct sigframe_ia32 __user *)(regs->sp-8);
	sigset_t set;

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.oldmask)
	    || __get_user(((__u32 *)&set)[1], &frame->extramask[0]))
		goto badframe;

	set_current_blocked(&set);

	if (ia32_restore_sigcontext(regs, &frame->sc))
		goto badframe;
	return regs->ax;

badframe:
	signal_fault(regs, frame, "32bit sigreturn");
	return 0;
}

COMPAT_SYSCALL_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe_ia32 __user *frame;
	sigset_t set;

	frame = (struct rt_sigframe_ia32 __user *)(regs->sp - 4);

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], (__u64 __user *)&frame->uc.uc_sigmask))
		goto badframe;

	set_current_blocked(&set);

	if (ia32_restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (compat_restore_altstack(&frame->uc.uc_stack))
		goto badframe;

	return regs->ax;

badframe:
	signal_fault(regs, frame, "32bit rt sigreturn");
	return 0;
}

/*
 * Set up a signal frame.
 */

#define get_user_seg(seg)	({ unsigned int v; savesegment(seg, v); v; })

static __always_inline int
__unsafe_setup_sigcontext32(struct sigcontext_32 __user *sc,
			    void __user *fpstate,
			    struct pt_regs *regs, unsigned int mask)
{
	unsafe_put_user(get_user_seg(gs), (unsigned int __user *)&sc->gs, Efault);
	unsafe_put_user(get_user_seg(fs), (unsigned int __user *)&sc->fs, Efault);
	unsafe_put_user(get_user_seg(ds), (unsigned int __user *)&sc->ds, Efault);
	unsafe_put_user(get_user_seg(es), (unsigned int __user *)&sc->es, Efault);

	unsafe_put_user(regs->di, &sc->di, Efault);
	unsafe_put_user(regs->si, &sc->si, Efault);
	unsafe_put_user(regs->bp, &sc->bp, Efault);
	unsafe_put_user(regs->sp, &sc->sp, Efault);
	unsafe_put_user(regs->bx, &sc->bx, Efault);
	unsafe_put_user(regs->dx, &sc->dx, Efault);
	unsafe_put_user(regs->cx, &sc->cx, Efault);
	unsafe_put_user(regs->ax, &sc->ax, Efault);
	unsafe_put_user(current->thread.trap_nr, &sc->trapno, Efault);
	unsafe_put_user(current->thread.error_code, &sc->err, Efault);
	unsafe_put_user(regs->ip, &sc->ip, Efault);
	unsafe_put_user(regs->cs, (unsigned int __user *)&sc->cs, Efault);
	unsafe_put_user(regs->flags, &sc->flags, Efault);
	unsafe_put_user(regs->sp, &sc->sp_at_signal, Efault);
	unsafe_put_user(regs->ss, (unsigned int __user *)&sc->ss, Efault);

	unsafe_put_user(ptr_to_compat(fpstate), &sc->fpstate, Efault);

	/* non-iBCS2 extensions.. */
	unsafe_put_user(mask, &sc->oldmask, Efault);
	unsafe_put_user(current->thread.cr2, &sc->cr2, Efault);
	return 0;

Efault:
	return -EFAULT;
}

#define unsafe_put_sigcontext32(sc, fp, regs, set, label)		\
do {									\
	if (__unsafe_setup_sigcontext32(sc, fp, regs, set->sig[0]))	\
		goto label;						\
} while(0)

/*
 * Determine which stack to use..
 */
static void __user *get_sigframe(struct ksignal *ksig, struct pt_regs *regs,
				 size_t frame_size,
				 void __user **fpstate)
{
	unsigned long sp, fx_aligned, math_size;

	/* Default to using normal stack */
	sp = regs->sp;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ksig->ka.sa.sa_flags & SA_ONSTACK)
		sp = sigsp(sp, ksig);
	/* This is the legacy signal stack switching. */
	else if (regs->ss != __USER32_DS &&
		!(ksig->ka.sa.sa_flags & SA_RESTORER) &&
		 ksig->ka.sa.sa_restorer)
		sp = (unsigned long) ksig->ka.sa.sa_restorer;

	sp = fpu__alloc_mathframe(sp, 1, &fx_aligned, &math_size);
	*fpstate = (struct _fpstate_32 __user *) sp;
	if (copy_fpstate_to_sigframe(*fpstate, (void __user *)fx_aligned,
				     math_size) < 0)
		return (void __user *) -1L;

	sp -= frame_size;
	/* Align the stack pointer according to the i386 ABI,
	 * i.e. so that on function entry ((sp + 4) & 15) == 0. */
	sp = ((sp + 4) & -16ul) - 4;
	return (void __user *) sp;
}

int ia32_setup_frame(int sig, struct ksignal *ksig,
		     compat_sigset_t *set, struct pt_regs *regs)
{
	struct sigframe_ia32 __user *frame;
	void __user *restorer;
	void __user *fp = NULL;

	/* copy_to_user optimizes that into a single 8 byte store */
	static const struct {
		u16 poplmovl;
		u32 val;
		u16 int80;
	} __attribute__((packed)) code = {
		0xb858,		 /* popl %eax ; movl $...,%eax */
		__NR_ia32_sigreturn,
		0x80cd,		/* int $0x80 */
	};

	frame = get_sigframe(ksig, regs, sizeof(*frame), &fp);

	if (ksig->ka.sa.sa_flags & SA_RESTORER) {
		restorer = ksig->ka.sa.sa_restorer;
	} else {
		/* Return stub is in 32bit vsyscall page */
		if (current->mm->context.vdso)
			restorer = current->mm->context.vdso +
				vdso_image_32.sym___kernel_sigreturn;
		else
			restorer = &frame->retcode;
	}

	if (!user_access_begin(frame, sizeof(*frame)))
		return -EFAULT;

	unsafe_put_user(sig, &frame->sig, Efault);
	unsafe_put_sigcontext32(&frame->sc, fp, regs, set, Efault);
	unsafe_put_user(set->sig[1], &frame->extramask[0], Efault);
	unsafe_put_user(ptr_to_compat(restorer), &frame->pretcode, Efault);
	/*
	 * These are actually not used anymore, but left because some
	 * gdb versions depend on them as a marker.
	 */
	unsafe_put_user(*((u64 *)&code), (u64 __user *)frame->retcode, Efault);
	user_access_end();

	/* Set up registers for signal handler */
	regs->sp = (unsigned long) frame;
	regs->ip = (unsigned long) ksig->ka.sa.sa_handler;

	/* Make -mregparm=3 work */
	regs->ax = sig;
	regs->dx = 0;
	regs->cx = 0;

	loadsegment(ds, __USER32_DS);
	loadsegment(es, __USER32_DS);

	regs->cs = __USER32_CS;
	regs->ss = __USER32_DS;

	return 0;
Efault:
	user_access_end();
	return -EFAULT;
}

int ia32_setup_rt_frame(int sig, struct ksignal *ksig,
			compat_sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe_ia32 __user *frame;
	void __user *restorer;
	void __user *fp = NULL;

	/* unsafe_put_user optimizes that into a single 8 byte store */
	static const struct {
		u8 movl;
		u32 val;
		u16 int80;
		u8  pad;
	} __attribute__((packed)) code = {
		0xb8,
		__NR_ia32_rt_sigreturn,
		0x80cd,
		0,
	};

	frame = get_sigframe(ksig, regs, sizeof(*frame), &fp);

	if (!user_access_begin(frame, sizeof(*frame)))
		return -EFAULT;

	unsafe_put_user(sig, &frame->sig, Efault);
	unsafe_put_user(ptr_to_compat(&frame->info), &frame->pinfo, Efault);
	unsafe_put_user(ptr_to_compat(&frame->uc), &frame->puc, Efault);

	/* Create the ucontext.  */
	if (static_cpu_has(X86_FEATURE_XSAVE))
		unsafe_put_user(UC_FP_XSTATE, &frame->uc.uc_flags, Efault);
	else
		unsafe_put_user(0, &frame->uc.uc_flags, Efault);
	unsafe_put_user(0, &frame->uc.uc_link, Efault);
	unsafe_compat_save_altstack(&frame->uc.uc_stack, regs->sp, Efault);

	if (ksig->ka.sa.sa_flags & SA_RESTORER)
		restorer = ksig->ka.sa.sa_restorer;
	else
		restorer = current->mm->context.vdso +
			vdso_image_32.sym___kernel_rt_sigreturn;
	unsafe_put_user(ptr_to_compat(restorer), &frame->pretcode, Efault);

	/*
	 * Not actually used anymore, but left because some gdb
	 * versions need it.
	 */
	unsafe_put_user(*((u64 *)&code), (u64 __user *)frame->retcode, Efault);
	unsafe_put_sigcontext32(&frame->uc.uc_mcontext, fp, regs, set, Efault);
	unsafe_put_user(*(__u64 *)set, (__u64 *)&frame->uc.uc_sigmask, Efault);
	user_access_end();

	if (__copy_siginfo_to_user32(&frame->info, &ksig->info))
		return -EFAULT;

	/* Set up registers for signal handler */
	regs->sp = (unsigned long) frame;
	regs->ip = (unsigned long) ksig->ka.sa.sa_handler;

	/* Make -mregparm=3 work */
	regs->ax = sig;
	regs->dx = (unsigned long) &frame->info;
	regs->cx = (unsigned long) &frame->uc;

	loadsegment(ds, __USER32_DS);
	loadsegment(es, __USER32_DS);

	regs->cs = __USER32_CS;
	regs->ss = __USER32_DS;

	return 0;
Efault:
	user_access_end();
	return -EFAULT;
}
