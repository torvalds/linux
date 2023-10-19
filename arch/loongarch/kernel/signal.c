// SPDX-License-Identifier: GPL-2.0+
/*
 * Author: Hanlu Li <lihanlu@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 1994 - 2000  Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2014, Imagination Technologies Ltd.
 */
#include <linux/audit.h>
#include <linux/cache.h>
#include <linux/context_tracking.h>
#include <linux/irqflags.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/personality.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/compiler.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#include <asm/asm.h>
#include <asm/cacheflush.h>
#include <asm/cpu-features.h>
#include <asm/fpu.h>
#include <asm/ucontext.h>
#include <asm/vdso.h>

#ifdef DEBUG_SIG
#  define DEBUGP(fmt, args...) printk("%s: " fmt, __func__, ##args)
#else
#  define DEBUGP(fmt, args...)
#endif

/* Make sure we will not lose FPU ownership */
#define lock_fpu_owner()	({ preempt_disable(); pagefault_disable(); })
#define unlock_fpu_owner()	({ pagefault_enable(); preempt_enable(); })

/* Assembly functions to move context to/from the FPU */
extern asmlinkage int
_save_fp_context(void __user *fpregs, void __user *fcc, void __user *csr);
extern asmlinkage int
_restore_fp_context(void __user *fpregs, void __user *fcc, void __user *csr);

struct rt_sigframe {
	struct siginfo rs_info;
	struct ucontext rs_uctx;
};

struct _ctx_layout {
	struct sctx_info *addr;
	unsigned int size;
};

struct extctx_layout {
	unsigned long size;
	unsigned int flags;
	struct _ctx_layout fpu;
	struct _ctx_layout end;
};

static void __user *get_ctx_through_ctxinfo(struct sctx_info *info)
{
	return (void __user *)((char *)info + sizeof(struct sctx_info));
}

/*
 * Thread saved context copy to/from a signal context presumed to be on the
 * user stack, and therefore accessed with appropriate macros from uaccess.h.
 */
static int copy_fpu_to_sigcontext(struct fpu_context __user *ctx)
{
	int i;
	int err = 0;
	uint64_t __user *regs	= (uint64_t *)&ctx->regs;
	uint64_t __user *fcc	= &ctx->fcc;
	uint32_t __user *fcsr	= &ctx->fcsr;

	for (i = 0; i < NUM_FPU_REGS; i++) {
		err |=
		    __put_user(get_fpr64(&current->thread.fpu.fpr[i], 0),
			       &regs[i]);
	}
	err |= __put_user(current->thread.fpu.fcc, fcc);
	err |= __put_user(current->thread.fpu.fcsr, fcsr);

	return err;
}

static int copy_fpu_from_sigcontext(struct fpu_context __user *ctx)
{
	int i;
	int err = 0;
	u64 fpr_val;
	uint64_t __user *regs	= (uint64_t *)&ctx->regs;
	uint64_t __user *fcc	= &ctx->fcc;
	uint32_t __user *fcsr	= &ctx->fcsr;

	for (i = 0; i < NUM_FPU_REGS; i++) {
		err |= __get_user(fpr_val, &regs[i]);
		set_fpr64(&current->thread.fpu.fpr[i], 0, fpr_val);
	}
	err |= __get_user(current->thread.fpu.fcc, fcc);
	err |= __get_user(current->thread.fpu.fcsr, fcsr);

	return err;
}

/*
 * Wrappers for the assembly _{save,restore}_fp_context functions.
 */
static int save_hw_fpu_context(struct fpu_context __user *ctx)
{
	uint64_t __user *regs	= (uint64_t *)&ctx->regs;
	uint64_t __user *fcc	= &ctx->fcc;
	uint32_t __user *fcsr	= &ctx->fcsr;

	return _save_fp_context(regs, fcc, fcsr);
}

static int restore_hw_fpu_context(struct fpu_context __user *ctx)
{
	uint64_t __user *regs	= (uint64_t *)&ctx->regs;
	uint64_t __user *fcc	= &ctx->fcc;
	uint32_t __user *fcsr	= &ctx->fcsr;

	return _restore_fp_context(regs, fcc, fcsr);
}

static int fcsr_pending(unsigned int __user *fcsr)
{
	int err, sig = 0;
	unsigned int csr, enabled;

	err = __get_user(csr, fcsr);
	enabled = ((csr & FPU_CSR_ALL_E) << 24);
	/*
	 * If the signal handler set some FPU exceptions, clear it and
	 * send SIGFPE.
	 */
	if (csr & enabled) {
		csr &= ~enabled;
		err |= __put_user(csr, fcsr);
		sig = SIGFPE;
	}
	return err ?: sig;
}

/*
 * Helper routines
 */
static int protected_save_fpu_context(struct extctx_layout *extctx)
{
	int err = 0;
	struct sctx_info __user *info = extctx->fpu.addr;
	struct fpu_context __user *fpu_ctx = (struct fpu_context *)get_ctx_through_ctxinfo(info);
	uint64_t __user *regs	= (uint64_t *)&fpu_ctx->regs;
	uint64_t __user *fcc	= &fpu_ctx->fcc;
	uint32_t __user *fcsr	= &fpu_ctx->fcsr;

	while (1) {
		lock_fpu_owner();
		if (is_fpu_owner())
			err = save_hw_fpu_context(fpu_ctx);
		else
			err = copy_fpu_to_sigcontext(fpu_ctx);
		unlock_fpu_owner();

		err |= __put_user(FPU_CTX_MAGIC, &info->magic);
		err |= __put_user(extctx->fpu.size, &info->size);

		if (likely(!err))
			break;
		/* Touch the FPU context and try again */
		err = __put_user(0, &regs[0]) |
			__put_user(0, &regs[31]) |
			__put_user(0, fcc) |
			__put_user(0, fcsr);
		if (err)
			return err;	/* really bad sigcontext */
	}

	return err;
}

static int protected_restore_fpu_context(struct extctx_layout *extctx)
{
	int err = 0, sig = 0, tmp __maybe_unused;
	struct sctx_info __user *info = extctx->fpu.addr;
	struct fpu_context __user *fpu_ctx = (struct fpu_context *)get_ctx_through_ctxinfo(info);
	uint64_t __user *regs	= (uint64_t *)&fpu_ctx->regs;
	uint64_t __user *fcc	= &fpu_ctx->fcc;
	uint32_t __user *fcsr	= &fpu_ctx->fcsr;

	err = sig = fcsr_pending(fcsr);
	if (err < 0)
		return err;

	while (1) {
		lock_fpu_owner();
		if (is_fpu_owner())
			err = restore_hw_fpu_context(fpu_ctx);
		else
			err = copy_fpu_from_sigcontext(fpu_ctx);
		unlock_fpu_owner();

		if (likely(!err))
			break;
		/* Touch the FPU context and try again */
		err = __get_user(tmp, &regs[0]) |
			__get_user(tmp, &regs[31]) |
			__get_user(tmp, fcc) |
			__get_user(tmp, fcsr);
		if (err)
			break;	/* really bad sigcontext */
	}

	return err ?: sig;
}

static int setup_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc,
			    struct extctx_layout *extctx)
{
	int i, err = 0;
	struct sctx_info __user *info;

	err |= __put_user(regs->csr_era, &sc->sc_pc);
	err |= __put_user(extctx->flags, &sc->sc_flags);

	err |= __put_user(0, &sc->sc_regs[0]);
	for (i = 1; i < 32; i++)
		err |= __put_user(regs->regs[i], &sc->sc_regs[i]);

	if (extctx->fpu.addr)
		err |= protected_save_fpu_context(extctx);

	/* Set the "end" magic */
	info = (struct sctx_info *)extctx->end.addr;
	err |= __put_user(0, &info->magic);
	err |= __put_user(0, &info->size);

	return err;
}

static int parse_extcontext(struct sigcontext __user *sc, struct extctx_layout *extctx)
{
	int err = 0;
	unsigned int magic, size;
	struct sctx_info __user *info = (struct sctx_info __user *)&sc->sc_extcontext;

	while(1) {
		err |= __get_user(magic, &info->magic);
		err |= __get_user(size, &info->size);
		if (err)
			return err;

		switch (magic) {
		case 0: /* END */
			goto done;

		case FPU_CTX_MAGIC:
			if (size < (sizeof(struct sctx_info) +
				    sizeof(struct fpu_context)))
				goto invalid;
			extctx->fpu.addr = info;
			break;

		default:
			goto invalid;
		}

		info = (struct sctx_info *)((char *)info + size);
	}

done:
	return 0;

invalid:
	return -EINVAL;
}

static int restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc)
{
	int i, err = 0;
	struct extctx_layout extctx;

	memset(&extctx, 0, sizeof(struct extctx_layout));

	err = __get_user(extctx.flags, &sc->sc_flags);
	if (err)
		goto bad;

	err = parse_extcontext(sc, &extctx);
	if (err)
		goto bad;

	conditional_used_math(extctx.flags & SC_USED_FP);

	/*
	 * The signal handler may have used FPU; give it up if the program
	 * doesn't want it following sigreturn.
	 */
	if (!(extctx.flags & SC_USED_FP))
		lose_fpu(0);

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	err |= __get_user(regs->csr_era, &sc->sc_pc);
	for (i = 1; i < 32; i++)
		err |= __get_user(regs->regs[i], &sc->sc_regs[i]);

	if (extctx.fpu.addr)
		err |= protected_restore_fpu_context(&extctx);

bad:
	return err;
}

static unsigned int handle_flags(void)
{
	unsigned int flags = 0;

	flags = used_math() ? SC_USED_FP : 0;

	switch (current->thread.error_code) {
	case 1:
		flags |= SC_ADDRERR_RD;
		break;
	case 2:
		flags |= SC_ADDRERR_WR;
		break;
	}

	return flags;
}

static unsigned long extframe_alloc(struct extctx_layout *extctx,
				    struct _ctx_layout *layout,
				    size_t size, unsigned int align, unsigned long base)
{
	unsigned long new_base = base - size;

	new_base = round_down(new_base, (align < 16 ? 16 : align));
	new_base -= sizeof(struct sctx_info);

	layout->addr = (void *)new_base;
	layout->size = (unsigned int)(base - new_base);
	extctx->size += layout->size;

	return new_base;
}

static unsigned long setup_extcontext(struct extctx_layout *extctx, unsigned long sp)
{
	unsigned long new_sp = sp;

	memset(extctx, 0, sizeof(struct extctx_layout));

	extctx->flags = handle_flags();

	/* Grow down, alloc "end" context info first. */
	new_sp -= sizeof(struct sctx_info);
	extctx->end.addr = (void *)new_sp;
	extctx->end.size = (unsigned int)sizeof(struct sctx_info);
	extctx->size += extctx->end.size;

	if (extctx->flags & SC_USED_FP) {
		if (cpu_has_fpu)
			new_sp = extframe_alloc(extctx, &extctx->fpu,
			  sizeof(struct fpu_context), FPU_CTX_ALIGN, new_sp);
	}

	return new_sp;
}

void __user *get_sigframe(struct ksignal *ksig, struct pt_regs *regs,
			  struct extctx_layout *extctx)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = regs->regs[3];

	/*
	 * If we are on the alternate signal stack and would overflow it, don't.
	 * Return an always-bogus address instead so we will die with SIGSEGV.
	 */
	if (on_sig_stack(sp) &&
	    !likely(on_sig_stack(sp - sizeof(struct rt_sigframe))))
		return (void __user __force *)(-1UL);

	sp = sigsp(sp, ksig);
	sp = round_down(sp, 16);
	sp = setup_extcontext(extctx, sp);
	sp -= sizeof(struct rt_sigframe);

	if (!IS_ALIGNED(sp, 16))
		BUG();

	return (void __user *)sp;
}

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */

asmlinkage long sys_rt_sigreturn(void)
{
	int sig;
	sigset_t set;
	struct pt_regs *regs;
	struct rt_sigframe __user *frame;

	regs = current_pt_regs();
	frame = (struct rt_sigframe __user *)regs->regs[3];
	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->rs_uctx.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	sig = restore_sigcontext(regs, &frame->rs_uctx.uc_mcontext);
	if (sig < 0)
		goto badframe;
	else if (sig)
		force_sig(sig);

	regs->regs[0] = 0; /* No syscall restarting */
	if (restore_altstack(&frame->rs_uctx.uc_stack))
		goto badframe;

	return regs->regs[4];

badframe:
	force_sig(SIGSEGV);
	return 0;
}

static int setup_rt_frame(void *sig_return, struct ksignal *ksig,
			  struct pt_regs *regs, sigset_t *set)
{
	int err = 0;
	struct extctx_layout extctx;
	struct rt_sigframe __user *frame;

	frame = get_sigframe(ksig, regs, &extctx);
	if (!access_ok(frame, sizeof(*frame) + extctx.size))
		return -EFAULT;

	/* Create siginfo.  */
	err |= copy_siginfo_to_user(&frame->rs_info, &ksig->info);

	/* Create the ucontext.	 */
	err |= __put_user(0, &frame->rs_uctx.uc_flags);
	err |= __put_user(NULL, &frame->rs_uctx.uc_link);
	err |= __save_altstack(&frame->rs_uctx.uc_stack, regs->regs[3]);
	err |= setup_sigcontext(regs, &frame->rs_uctx.uc_mcontext, &extctx);
	err |= __copy_to_user(&frame->rs_uctx.uc_sigmask, set, sizeof(*set));

	if (err)
		return -EFAULT;

	/*
	 * Arguments to signal handler:
	 *
	 *   a0 = signal number
	 *   a1 = pointer to siginfo
	 *   a2 = pointer to ucontext
	 *
	 * c0_era point to the signal handler, $r3 (sp) points to
	 * the struct rt_sigframe.
	 */
	regs->regs[4] = ksig->sig;
	regs->regs[5] = (unsigned long) &frame->rs_info;
	regs->regs[6] = (unsigned long) &frame->rs_uctx;
	regs->regs[3] = (unsigned long) frame;
	regs->regs[1] = (unsigned long) sig_return;
	regs->csr_era = (unsigned long) ksig->ka.sa.sa_handler;

	DEBUGP("SIG deliver (%s:%d): sp=0x%p pc=0x%lx ra=0x%lx\n",
	       current->comm, current->pid,
	       frame, regs->csr_era, regs->regs[1]);

	return 0;
}

static void handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	int ret;
	sigset_t *oldset = sigmask_to_save();
	void *vdso = current->mm->context.vdso;

	/* Are we from a system call? */
	if (regs->regs[0]) {
		switch (regs->regs[4]) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->regs[4] = -EINTR;
			break;
		case -ERESTARTSYS:
			if (!(ksig->ka.sa.sa_flags & SA_RESTART)) {
				regs->regs[4] = -EINTR;
				break;
			}
			fallthrough;
		case -ERESTARTNOINTR:
			regs->regs[4] = regs->orig_a0;
			regs->csr_era -= 4;
		}

		regs->regs[0] = 0;	/* Don't deal with this again.	*/
	}

	rseq_signal_deliver(ksig, regs);

	ret = setup_rt_frame(vdso + current->thread.vdso->offset_sigreturn, ksig, regs, oldset);

	signal_setup_done(ret, ksig, 0);
}

void arch_do_signal_or_restart(struct pt_regs *regs)
{
	struct ksignal ksig;

	if (get_signal(&ksig)) {
		/* Whee!  Actually deliver the signal.	*/
		handle_signal(&ksig, regs);
		return;
	}

	/* Are we from a system call? */
	if (regs->regs[0]) {
		switch (regs->regs[4]) {
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			regs->regs[4] = regs->orig_a0;
			regs->csr_era -= 4;
			break;

		case -ERESTART_RESTARTBLOCK:
			regs->regs[4] = regs->orig_a0;
			regs->regs[11] = __NR_restart_syscall;
			regs->csr_era -= 4;
			break;
		}
		regs->regs[0] = 0;	/* Don't deal with this again.	*/
	}

	/*
	 * If there's no signal to deliver, we just put the saved sigmask
	 * back
	 */
	restore_saved_sigmask();
}
