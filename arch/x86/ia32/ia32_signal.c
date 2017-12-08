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
#include <asm/sys_ia32.h>
#include <asm/smap.h>

/*
 * Do a signal return; undo the signal stack.
 */
#define loadsegment_gs(v)	load_gs_index(v)
#define loadsegment_fs(v)	loadsegment(fs, v)
#define loadsegment_ds(v)	loadsegment(ds, v)
#define loadsegment_es(v)	loadsegment(es, v)

#define get_user_seg(seg)	({ unsigned int v; savesegment(seg, v); v; })
#define set_user_seg(seg, v)	loadsegment_##seg(v)

#define COPY(x)			{		\
	get_user_ex(regs->x, &sc->x);		\
}

#define GET_SEG(seg)		({			\
	unsigned short tmp;				\
	get_user_ex(tmp, &sc->seg);			\
	tmp;						\
})

#define COPY_SEG_CPL3(seg)	do {			\
	regs->seg = GET_SEG(seg) | 3;			\
} while (0)

#define RELOAD_SEG(seg)		{		\
	unsigned int pre = GET_SEG(seg);	\
	unsigned int cur = get_user_seg(seg);	\
	pre |= 3;				\
	if (pre != cur)				\
		set_user_seg(seg, pre);		\
}

static int ia32_restore_sigcontext(struct pt_regs *regs,
				   struct sigcontext_32 __user *sc)
{
	unsigned int tmpflags, err = 0;
	void __user *buf;
	u32 tmp;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	get_user_try {
		/*
		 * Reload fs and gs if they have changed in the signal
		 * handler.  This does not handle long fs/gs base changes in
		 * the handler, but does not clobber them at least in the
		 * normal case.
		 */
		RELOAD_SEG(gs);
		RELOAD_SEG(fs);
		RELOAD_SEG(ds);
		RELOAD_SEG(es);

		COPY(di); COPY(si); COPY(bp); COPY(sp); COPY(bx);
		COPY(dx); COPY(cx); COPY(ip); COPY(ax);
		/* Don't touch extended registers */

		COPY_SEG_CPL3(cs);
		COPY_SEG_CPL3(ss);

		get_user_ex(tmpflags, &sc->flags);
		regs->flags = (regs->flags & ~FIX_EFLAGS) | (tmpflags & FIX_EFLAGS);
		/* disable syscall checks */
		regs->orig_ax = -1;

		get_user_ex(tmp, &sc->fpstate);
		buf = compat_ptr(tmp);
	} get_user_catch(err);

	err |= fpu__restore_sig(buf, 1);

	force_iret();

	return err;
}

asmlinkage long sys32_sigreturn(void)
{
	struct pt_regs *regs = current_pt_regs();
	struct sigframe_ia32 __user *frame = (struct sigframe_ia32 __user *)(regs->sp-8);
	sigset_t set;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.oldmask)
	    || (_COMPAT_NSIG_WORDS > 1
		&& __copy_from_user((((char *) &set.sig) + 4),
				    &frame->extramask,
				    sizeof(frame->extramask))))
		goto badframe;

	set_current_blocked(&set);

	if (ia32_restore_sigcontext(regs, &frame->sc))
		goto badframe;
	return regs->ax;

badframe:
	signal_fault(regs, frame, "32bit sigreturn");
	return 0;
}

asmlinkage long sys32_rt_sigreturn(void)
{
	struct pt_regs *regs = current_pt_regs();
	struct rt_sigframe_ia32 __user *frame;
	sigset_t set;

	frame = (struct rt_sigframe_ia32 __user *)(regs->sp - 4);

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
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

static int ia32_setup_sigcontext(struct sigcontext_32 __user *sc,
				 void __user *fpstate,
				 struct pt_regs *regs, unsigned int mask)
{
	int err = 0;

	put_user_try {
		put_user_ex(get_user_seg(gs), (unsigned int __user *)&sc->gs);
		put_user_ex(get_user_seg(fs), (unsigned int __user *)&sc->fs);
		put_user_ex(get_user_seg(ds), (unsigned int __user *)&sc->ds);
		put_user_ex(get_user_seg(es), (unsigned int __user *)&sc->es);

		put_user_ex(regs->di, &sc->di);
		put_user_ex(regs->si, &sc->si);
		put_user_ex(regs->bp, &sc->bp);
		put_user_ex(regs->sp, &sc->sp);
		put_user_ex(regs->bx, &sc->bx);
		put_user_ex(regs->dx, &sc->dx);
		put_user_ex(regs->cx, &sc->cx);
		put_user_ex(regs->ax, &sc->ax);
		put_user_ex(current->thread.trap_nr, &sc->trapno);
		put_user_ex(current->thread.error_code, &sc->err);
		put_user_ex(regs->ip, &sc->ip);
		put_user_ex(regs->cs, (unsigned int __user *)&sc->cs);
		put_user_ex(regs->flags, &sc->flags);
		put_user_ex(regs->sp, &sc->sp_at_signal);
		put_user_ex(regs->ss, (unsigned int __user *)&sc->ss);

		put_user_ex(ptr_to_compat(fpstate), &sc->fpstate);

		/* non-iBCS2 extensions.. */
		put_user_ex(mask, &sc->oldmask);
		put_user_ex(current->thread.cr2, &sc->cr2);
	} put_user_catch(err);

	return err;
}

/*
 * Determine which stack to use..
 */
static void __user *get_sigframe(struct ksignal *ksig, struct pt_regs *regs,
				 size_t frame_size,
				 void __user **fpstate)
{
	struct fpu *fpu = &current->thread.fpu;
	unsigned long sp;

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

	if (fpu->initialized) {
		unsigned long fx_aligned, math_size;

		sp = fpu__alloc_mathframe(sp, 1, &fx_aligned, &math_size);
		*fpstate = (struct _fpstate_32 __user *) sp;
		if (copy_fpstate_to_sigframe(*fpstate, (void __user *)fx_aligned,
				    math_size) < 0)
			return (void __user *) -1L;
	}

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
	int err = 0;
	void __user *fpstate = NULL;

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

	frame = get_sigframe(ksig, regs, sizeof(*frame), &fpstate);

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		return -EFAULT;

	if (__put_user(sig, &frame->sig))
		return -EFAULT;

	if (ia32_setup_sigcontext(&frame->sc, fpstate, regs, set->sig[0]))
		return -EFAULT;

	if (_COMPAT_NSIG_WORDS > 1) {
		if (__copy_to_user(frame->extramask, &set->sig[1],
				   sizeof(frame->extramask)))
			return -EFAULT;
	}

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

	put_user_try {
		put_user_ex(ptr_to_compat(restorer), &frame->pretcode);

		/*
		 * These are actually not used anymore, but left because some
		 * gdb versions depend on them as a marker.
		 */
		put_user_ex(*((u64 *)&code), (u64 __user *)frame->retcode);
	} put_user_catch(err);

	if (err)
		return -EFAULT;

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
}

int ia32_setup_rt_frame(int sig, struct ksignal *ksig,
			compat_sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe_ia32 __user *frame;
	void __user *restorer;
	int err = 0;
	void __user *fpstate = NULL;

	/* __copy_to_user optimizes that into a single 8 byte store */
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

	frame = get_sigframe(ksig, regs, sizeof(*frame), &fpstate);

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		return -EFAULT;

	put_user_try {
		put_user_ex(sig, &frame->sig);
		put_user_ex(ptr_to_compat(&frame->info), &frame->pinfo);
		put_user_ex(ptr_to_compat(&frame->uc), &frame->puc);

		/* Create the ucontext.  */
		if (boot_cpu_has(X86_FEATURE_XSAVE))
			put_user_ex(UC_FP_XSTATE, &frame->uc.uc_flags);
		else
			put_user_ex(0, &frame->uc.uc_flags);
		put_user_ex(0, &frame->uc.uc_link);
		compat_save_altstack_ex(&frame->uc.uc_stack, regs->sp);

		if (ksig->ka.sa.sa_flags & SA_RESTORER)
			restorer = ksig->ka.sa.sa_restorer;
		else
			restorer = current->mm->context.vdso +
				vdso_image_32.sym___kernel_rt_sigreturn;
		put_user_ex(ptr_to_compat(restorer), &frame->pretcode);

		/*
		 * Not actually used anymore, but left because some gdb
		 * versions need it.
		 */
		put_user_ex(*((u64 *)&code), (u64 __user *)frame->retcode);
	} put_user_catch(err);

	err |= __copy_siginfo_to_user32(&frame->info, &ksig->info, false);
	err |= ia32_setup_sigcontext(&frame->uc.uc_mcontext, fpstate,
				     regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	if (err)
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
}
