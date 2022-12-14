/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 1994 - 2000  Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2014, Imagination Technologies Ltd.
 */
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
#include <linux/uprobes.h>
#include <linux/compiler.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/resume_user_mode.h>

#include <asm/abi.h>
#include <asm/asm.h>
#include <linux/bitops.h>
#include <asm/cacheflush.h>
#include <asm/fpu.h>
#include <asm/sim.h>
#include <asm/ucontext.h>
#include <asm/cpu-features.h>
#include <asm/dsp.h>
#include <asm/inst.h>
#include <asm/msa.h>

#include "signal-common.h"

static int (*save_fp_context)(void __user *sc);
static int (*restore_fp_context)(void __user *sc);

struct sigframe {
	u32 sf_ass[4];		/* argument save space for o32 */
	u32 sf_pad[2];		/* Was: signal trampoline */

	/* Matches struct ucontext from its uc_mcontext field onwards */
	struct sigcontext sf_sc;
	sigset_t sf_mask;
	unsigned long long sf_extcontext[];
};

struct rt_sigframe {
	u32 rs_ass[4];		/* argument save space for o32 */
	u32 rs_pad[2];		/* Was: signal trampoline */
	struct siginfo rs_info;
	struct ucontext rs_uc;
};

#ifdef CONFIG_MIPS_FP_SUPPORT

/*
 * Thread saved context copy to/from a signal context presumed to be on the
 * user stack, and therefore accessed with appropriate macros from uaccess.h.
 */
static int copy_fp_to_sigcontext(void __user *sc)
{
	struct mips_abi *abi = current->thread.abi;
	uint64_t __user *fpregs = sc + abi->off_sc_fpregs;
	uint32_t __user *csr = sc + abi->off_sc_fpc_csr;
	int i;
	int err = 0;
	int inc = test_thread_flag(TIF_32BIT_FPREGS) ? 2 : 1;

	for (i = 0; i < NUM_FPU_REGS; i += inc) {
		err |=
		    __put_user(get_fpr64(&current->thread.fpu.fpr[i], 0),
			       &fpregs[i]);
	}
	err |= __put_user(current->thread.fpu.fcr31, csr);

	return err;
}

static int copy_fp_from_sigcontext(void __user *sc)
{
	struct mips_abi *abi = current->thread.abi;
	uint64_t __user *fpregs = sc + abi->off_sc_fpregs;
	uint32_t __user *csr = sc + abi->off_sc_fpc_csr;
	int i;
	int err = 0;
	int inc = test_thread_flag(TIF_32BIT_FPREGS) ? 2 : 1;
	u64 fpr_val;

	for (i = 0; i < NUM_FPU_REGS; i += inc) {
		err |= __get_user(fpr_val, &fpregs[i]);
		set_fpr64(&current->thread.fpu.fpr[i], 0, fpr_val);
	}
	err |= __get_user(current->thread.fpu.fcr31, csr);

	return err;
}

#else /* !CONFIG_MIPS_FP_SUPPORT */

static int copy_fp_to_sigcontext(void __user *sc)
{
	return 0;
}

static int copy_fp_from_sigcontext(void __user *sc)
{
	return 0;
}

#endif /* !CONFIG_MIPS_FP_SUPPORT */

/*
 * Wrappers for the assembly _{save,restore}_fp_context functions.
 */
static int save_hw_fp_context(void __user *sc)
{
	struct mips_abi *abi = current->thread.abi;
	uint64_t __user *fpregs = sc + abi->off_sc_fpregs;
	uint32_t __user *csr = sc + abi->off_sc_fpc_csr;

	return _save_fp_context(fpregs, csr);
}

static int restore_hw_fp_context(void __user *sc)
{
	struct mips_abi *abi = current->thread.abi;
	uint64_t __user *fpregs = sc + abi->off_sc_fpregs;
	uint32_t __user *csr = sc + abi->off_sc_fpc_csr;

	return _restore_fp_context(fpregs, csr);
}

/*
 * Extended context handling.
 */

static inline void __user *sc_to_extcontext(void __user *sc)
{
	struct ucontext __user *uc;

	/*
	 * We can just pretend the sigcontext is always embedded in a struct
	 * ucontext here, because the offset from sigcontext to extended
	 * context is the same in the struct sigframe case.
	 */
	uc = container_of(sc, struct ucontext, uc_mcontext);
	return &uc->uc_extcontext;
}

#ifdef CONFIG_CPU_HAS_MSA

static int save_msa_extcontext(void __user *buf)
{
	struct msa_extcontext __user *msa = buf;
	uint64_t val;
	int i, err;

	if (!thread_msa_context_live())
		return 0;

	/*
	 * Ensure that we can't lose the live MSA context between checking
	 * for it & writing it to memory.
	 */
	preempt_disable();

	if (is_msa_enabled()) {
		/*
		 * There are no EVA versions of the vector register load/store
		 * instructions, so MSA context has to be saved to kernel memory
		 * and then copied to user memory. The save to kernel memory
		 * should already have been done when handling scalar FP
		 * context.
		 */
		BUG_ON(IS_ENABLED(CONFIG_EVA));

		err = __put_user(read_msa_csr(), &msa->csr);
		err |= _save_msa_all_upper(&msa->wr);

		preempt_enable();
	} else {
		preempt_enable();

		err = __put_user(current->thread.fpu.msacsr, &msa->csr);

		for (i = 0; i < NUM_FPU_REGS; i++) {
			val = get_fpr64(&current->thread.fpu.fpr[i], 1);
			err |= __put_user(val, &msa->wr[i]);
		}
	}

	err |= __put_user(MSA_EXTCONTEXT_MAGIC, &msa->ext.magic);
	err |= __put_user(sizeof(*msa), &msa->ext.size);

	return err ? -EFAULT : sizeof(*msa);
}

static int restore_msa_extcontext(void __user *buf, unsigned int size)
{
	struct msa_extcontext __user *msa = buf;
	unsigned long long val;
	unsigned int csr;
	int i, err;

	if (size != sizeof(*msa))
		return -EINVAL;

	err = get_user(csr, &msa->csr);
	if (err)
		return err;

	preempt_disable();

	if (is_msa_enabled()) {
		/*
		 * There are no EVA versions of the vector register load/store
		 * instructions, so MSA context has to be copied to kernel
		 * memory and later loaded to registers. The same is true of
		 * scalar FP context, so FPU & MSA should have already been
		 * disabled whilst handling scalar FP context.
		 */
		BUG_ON(IS_ENABLED(CONFIG_EVA));

		write_msa_csr(csr);
		err |= _restore_msa_all_upper(&msa->wr);
		preempt_enable();
	} else {
		preempt_enable();

		current->thread.fpu.msacsr = csr;

		for (i = 0; i < NUM_FPU_REGS; i++) {
			err |= __get_user(val, &msa->wr[i]);
			set_fpr64(&current->thread.fpu.fpr[i], 1, val);
		}
	}

	return err;
}

#else /* !CONFIG_CPU_HAS_MSA */

static int save_msa_extcontext(void __user *buf)
{
	return 0;
}

static int restore_msa_extcontext(void __user *buf, unsigned int size)
{
	return SIGSYS;
}

#endif /* !CONFIG_CPU_HAS_MSA */

static int save_extcontext(void __user *buf)
{
	int sz;

	sz = save_msa_extcontext(buf);
	if (sz < 0)
		return sz;
	buf += sz;

	/* If no context was saved then trivially return */
	if (!sz)
		return 0;

	/* Write the end marker */
	if (__put_user(END_EXTCONTEXT_MAGIC, (u32 *)buf))
		return -EFAULT;

	sz += sizeof(((struct extcontext *)NULL)->magic);
	return sz;
}

static int restore_extcontext(void __user *buf)
{
	struct extcontext ext;
	int err;

	while (1) {
		err = __get_user(ext.magic, (unsigned int *)buf);
		if (err)
			return err;

		if (ext.magic == END_EXTCONTEXT_MAGIC)
			return 0;

		err = __get_user(ext.size, (unsigned int *)(buf
			+ offsetof(struct extcontext, size)));
		if (err)
			return err;

		switch (ext.magic) {
		case MSA_EXTCONTEXT_MAGIC:
			err = restore_msa_extcontext(buf, ext.size);
			break;

		default:
			err = -EINVAL;
			break;
		}

		if (err)
			return err;

		buf += ext.size;
	}
}

/*
 * Helper routines
 */
int protected_save_fp_context(void __user *sc)
{
	struct mips_abi *abi = current->thread.abi;
	uint64_t __user *fpregs = sc + abi->off_sc_fpregs;
	uint32_t __user *csr = sc + abi->off_sc_fpc_csr;
	uint32_t __user *used_math = sc + abi->off_sc_used_math;
	unsigned int used, ext_sz;
	int err;

	used = used_math() ? USED_FP : 0;
	if (!used)
		goto fp_done;

	if (!test_thread_flag(TIF_32BIT_FPREGS))
		used |= USED_FR1;
	if (test_thread_flag(TIF_HYBRID_FPREGS))
		used |= USED_HYBRID_FPRS;

	/*
	 * EVA does not have userland equivalents of ldc1 or sdc1, so
	 * save to the kernel FP context & copy that to userland below.
	 */
	if (IS_ENABLED(CONFIG_EVA))
		lose_fpu(1);

	while (1) {
		lock_fpu_owner();
		if (is_fpu_owner()) {
			err = save_fp_context(sc);
			unlock_fpu_owner();
		} else {
			unlock_fpu_owner();
			err = copy_fp_to_sigcontext(sc);
		}
		if (likely(!err))
			break;
		/* touch the sigcontext and try again */
		err = __put_user(0, &fpregs[0]) |
			__put_user(0, &fpregs[31]) |
			__put_user(0, csr);
		if (err)
			return err;	/* really bad sigcontext */
	}

fp_done:
	ext_sz = err = save_extcontext(sc_to_extcontext(sc));
	if (err < 0)
		return err;
	used |= ext_sz ? USED_EXTCONTEXT : 0;

	return __put_user(used, used_math);
}

int protected_restore_fp_context(void __user *sc)
{
	struct mips_abi *abi = current->thread.abi;
	uint64_t __user *fpregs = sc + abi->off_sc_fpregs;
	uint32_t __user *csr = sc + abi->off_sc_fpc_csr;
	uint32_t __user *used_math = sc + abi->off_sc_used_math;
	unsigned int used;
	int err, sig = 0, tmp __maybe_unused;

	err = __get_user(used, used_math);
	conditional_used_math(used & USED_FP);

	/*
	 * The signal handler may have used FPU; give it up if the program
	 * doesn't want it following sigreturn.
	 */
	if (err || !(used & USED_FP))
		lose_fpu(0);
	if (err)
		return err;
	if (!(used & USED_FP))
		goto fp_done;

	err = sig = fpcsr_pending(csr);
	if (err < 0)
		return err;

	/*
	 * EVA does not have userland equivalents of ldc1 or sdc1, so we
	 * disable the FPU here such that the code below simply copies to
	 * the kernel FP context.
	 */
	if (IS_ENABLED(CONFIG_EVA))
		lose_fpu(0);

	while (1) {
		lock_fpu_owner();
		if (is_fpu_owner()) {
			err = restore_fp_context(sc);
			unlock_fpu_owner();
		} else {
			unlock_fpu_owner();
			err = copy_fp_from_sigcontext(sc);
		}
		if (likely(!err))
			break;
		/* touch the sigcontext and try again */
		err = __get_user(tmp, &fpregs[0]) |
			__get_user(tmp, &fpregs[31]) |
			__get_user(tmp, csr);
		if (err)
			break;	/* really bad sigcontext */
	}

fp_done:
	if (!err && (used & USED_EXTCONTEXT))
		err = restore_extcontext(sc_to_extcontext(sc));

	return err ?: sig;
}

int setup_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc)
{
	int err = 0;
	int i;

	err |= __put_user(regs->cp0_epc, &sc->sc_pc);

	err |= __put_user(0, &sc->sc_regs[0]);
	for (i = 1; i < 32; i++)
		err |= __put_user(regs->regs[i], &sc->sc_regs[i]);

#ifdef CONFIG_CPU_HAS_SMARTMIPS
	err |= __put_user(regs->acx, &sc->sc_acx);
#endif
	err |= __put_user(regs->hi, &sc->sc_mdhi);
	err |= __put_user(regs->lo, &sc->sc_mdlo);
	if (cpu_has_dsp) {
		err |= __put_user(mfhi1(), &sc->sc_hi1);
		err |= __put_user(mflo1(), &sc->sc_lo1);
		err |= __put_user(mfhi2(), &sc->sc_hi2);
		err |= __put_user(mflo2(), &sc->sc_lo2);
		err |= __put_user(mfhi3(), &sc->sc_hi3);
		err |= __put_user(mflo3(), &sc->sc_lo3);
		err |= __put_user(rddsp(DSP_MASK), &sc->sc_dsp);
	}


	/*
	 * Save FPU state to signal context. Signal handler
	 * will "inherit" current FPU state.
	 */
	err |= protected_save_fp_context(sc);

	return err;
}

static size_t extcontext_max_size(void)
{
	size_t sz = 0;

	/*
	 * The assumption here is that between this point & the point at which
	 * the extended context is saved the size of the context should only
	 * ever be able to shrink (if the task is preempted), but never grow.
	 * That is, what this function returns is an upper bound on the size of
	 * the extended context for the current task at the current time.
	 */

	if (thread_msa_context_live())
		sz += sizeof(struct msa_extcontext);

	/* If any context is saved then we'll append the end marker */
	if (sz)
		sz += sizeof(((struct extcontext *)NULL)->magic);

	return sz;
}

int fpcsr_pending(unsigned int __user *fpcsr)
{
	int err, sig = 0;
	unsigned int csr, enabled;

	err = __get_user(csr, fpcsr);
	enabled = FPU_CSR_UNI_X | ((csr & FPU_CSR_ALL_E) << 5);
	/*
	 * If the signal handler set some FPU exceptions, clear it and
	 * send SIGFPE.
	 */
	if (csr & enabled) {
		csr &= ~enabled;
		err |= __put_user(csr, fpcsr);
		sig = SIGFPE;
	}
	return err ?: sig;
}

int restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc)
{
	unsigned long treg;
	int err = 0;
	int i;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	err |= __get_user(regs->cp0_epc, &sc->sc_pc);

#ifdef CONFIG_CPU_HAS_SMARTMIPS
	err |= __get_user(regs->acx, &sc->sc_acx);
#endif
	err |= __get_user(regs->hi, &sc->sc_mdhi);
	err |= __get_user(regs->lo, &sc->sc_mdlo);
	if (cpu_has_dsp) {
		err |= __get_user(treg, &sc->sc_hi1); mthi1(treg);
		err |= __get_user(treg, &sc->sc_lo1); mtlo1(treg);
		err |= __get_user(treg, &sc->sc_hi2); mthi2(treg);
		err |= __get_user(treg, &sc->sc_lo2); mtlo2(treg);
		err |= __get_user(treg, &sc->sc_hi3); mthi3(treg);
		err |= __get_user(treg, &sc->sc_lo3); mtlo3(treg);
		err |= __get_user(treg, &sc->sc_dsp); wrdsp(treg, DSP_MASK);
	}

	for (i = 1; i < 32; i++)
		err |= __get_user(regs->regs[i], &sc->sc_regs[i]);

	return err ?: protected_restore_fp_context(sc);
}

#ifdef CONFIG_WAR_ICACHE_REFILLS
#define SIGMASK		~(cpu_icache_line_size()-1)
#else
#define SIGMASK		ALMASK
#endif

void __user *get_sigframe(struct ksignal *ksig, struct pt_regs *regs,
			  size_t frame_size)
{
	unsigned long sp;

	/* Leave space for potential extended context */
	frame_size += extcontext_max_size();

	/* Default to using normal stack */
	sp = regs->regs[29];

	/*
	 * If we are on the alternate signal stack and would overflow it, don't.
	 * Return an always-bogus address instead so we will die with SIGSEGV.
	 */
	if (on_sig_stack(sp) && !likely(on_sig_stack(sp - frame_size)))
		return (void __user __force *)(-1UL);

	/*
	 * FPU emulator may have it's own trampoline active just
	 * above the user stack, 16-bytes before the next lowest
	 * 16 byte boundary.  Try to avoid trashing it.
	 */
	sp -= 32;

	sp = sigsp(sp, ksig);

	return (void __user *)((sp - frame_size) & SIGMASK);
}

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */

#ifdef CONFIG_TRAD_SIGNALS
SYSCALL_DEFINE1(sigsuspend, sigset_t __user *, uset)
{
	return sys_rt_sigsuspend(uset, sizeof(sigset_t));
}
#endif

#ifdef CONFIG_TRAD_SIGNALS
SYSCALL_DEFINE3(sigaction, int, sig, const struct sigaction __user *, act,
	struct sigaction __user *, oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;
	int err = 0;

	if (act) {
		old_sigset_t mask;

		if (!access_ok(act, sizeof(*act)))
			return -EFAULT;
		err |= __get_user(new_ka.sa.sa_handler, &act->sa_handler);
		err |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		err |= __get_user(mask, &act->sa_mask.sig[0]);
		if (err)
			return -EFAULT;

		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(oact, sizeof(*oact)))
			return -EFAULT;
		err |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		err |= __put_user(old_ka.sa.sa_handler, &oact->sa_handler);
		err |= __put_user(old_ka.sa.sa_mask.sig[0], oact->sa_mask.sig);
		err |= __put_user(0, &oact->sa_mask.sig[1]);
		err |= __put_user(0, &oact->sa_mask.sig[2]);
		err |= __put_user(0, &oact->sa_mask.sig[3]);
		if (err)
			return -EFAULT;
	}

	return ret;
}
#endif

#ifdef CONFIG_TRAD_SIGNALS
asmlinkage void sys_sigreturn(void)
{
	struct sigframe __user *frame;
	struct pt_regs *regs;
	sigset_t blocked;
	int sig;

	regs = current_pt_regs();
	frame = (struct sigframe __user *)regs->regs[29];
	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&blocked, &frame->sf_mask, sizeof(blocked)))
		goto badframe;

	set_current_blocked(&blocked);

	sig = restore_sigcontext(regs, &frame->sf_sc);
	if (sig < 0)
		goto badframe;
	else if (sig)
		force_sig(sig);

	/*
	 * Don't let your children do this ...
	 */
	__asm__ __volatile__(
		"move\t$29, %0\n\t"
		"j\tsyscall_exit"
		: /* no outputs */
		: "r" (regs));
	/* Unreached */

badframe:
	force_sig(SIGSEGV);
}
#endif /* CONFIG_TRAD_SIGNALS */

asmlinkage void sys_rt_sigreturn(void)
{
	struct rt_sigframe __user *frame;
	struct pt_regs *regs;
	sigset_t set;
	int sig;

	regs = current_pt_regs();
	frame = (struct rt_sigframe __user *)regs->regs[29];
	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->rs_uc.uc_sigmask, sizeof(set)))
		goto badframe;

	set_current_blocked(&set);

	sig = restore_sigcontext(regs, &frame->rs_uc.uc_mcontext);
	if (sig < 0)
		goto badframe;
	else if (sig)
		force_sig(sig);

	if (restore_altstack(&frame->rs_uc.uc_stack))
		goto badframe;

	/*
	 * Don't let your children do this ...
	 */
	__asm__ __volatile__(
		"move\t$29, %0\n\t"
		"j\tsyscall_exit"
		: /* no outputs */
		: "r" (regs));
	/* Unreached */

badframe:
	force_sig(SIGSEGV);
}

#ifdef CONFIG_TRAD_SIGNALS
static int setup_frame(void *sig_return, struct ksignal *ksig,
		       struct pt_regs *regs, sigset_t *set)
{
	struct sigframe __user *frame;
	int err = 0;

	frame = get_sigframe(ksig, regs, sizeof(*frame));
	if (!access_ok(frame, sizeof (*frame)))
		return -EFAULT;

	err |= setup_sigcontext(regs, &frame->sf_sc);
	err |= __copy_to_user(&frame->sf_mask, set, sizeof(*set));
	if (err)
		return -EFAULT;

	/*
	 * Arguments to signal handler:
	 *
	 *   a0 = signal number
	 *   a1 = 0 (should be cause)
	 *   a2 = pointer to struct sigcontext
	 *
	 * $25 and c0_epc point to the signal handler, $29 points to the
	 * struct sigframe.
	 */
	regs->regs[ 4] = ksig->sig;
	regs->regs[ 5] = 0;
	regs->regs[ 6] = (unsigned long) &frame->sf_sc;
	regs->regs[29] = (unsigned long) frame;
	regs->regs[31] = (unsigned long) sig_return;
	regs->cp0_epc = regs->regs[25] = (unsigned long) ksig->ka.sa.sa_handler;

	DEBUGP("SIG deliver (%s:%d): sp=0x%p pc=0x%lx ra=0x%lx\n",
	       current->comm, current->pid,
	       frame, regs->cp0_epc, regs->regs[31]);
	return 0;
}
#endif

static int setup_rt_frame(void *sig_return, struct ksignal *ksig,
			  struct pt_regs *regs, sigset_t *set)
{
	struct rt_sigframe __user *frame;

	frame = get_sigframe(ksig, regs, sizeof(*frame));
	if (!access_ok(frame, sizeof (*frame)))
		return -EFAULT;

	/* Create siginfo.  */
	if (copy_siginfo_to_user(&frame->rs_info, &ksig->info))
		return -EFAULT;

	/* Create the ucontext.	 */
	if (__put_user(0, &frame->rs_uc.uc_flags))
		return -EFAULT;
	if (__put_user(NULL, &frame->rs_uc.uc_link))
		return -EFAULT;
	if (__save_altstack(&frame->rs_uc.uc_stack, regs->regs[29]))
		return -EFAULT;
	if (setup_sigcontext(regs, &frame->rs_uc.uc_mcontext))
		return -EFAULT;
	if (__copy_to_user(&frame->rs_uc.uc_sigmask, set, sizeof(*set)))
		return -EFAULT;

	/*
	 * Arguments to signal handler:
	 *
	 *   a0 = signal number
	 *   a1 = 0 (should be cause)
	 *   a2 = pointer to ucontext
	 *
	 * $25 and c0_epc point to the signal handler, $29 points to
	 * the struct rt_sigframe.
	 */
	regs->regs[ 4] = ksig->sig;
	regs->regs[ 5] = (unsigned long) &frame->rs_info;
	regs->regs[ 6] = (unsigned long) &frame->rs_uc;
	regs->regs[29] = (unsigned long) frame;
	regs->regs[31] = (unsigned long) sig_return;
	regs->cp0_epc = regs->regs[25] = (unsigned long) ksig->ka.sa.sa_handler;

	DEBUGP("SIG deliver (%s:%d): sp=0x%p pc=0x%lx ra=0x%lx\n",
	       current->comm, current->pid,
	       frame, regs->cp0_epc, regs->regs[31]);

	return 0;
}

struct mips_abi mips_abi = {
#ifdef CONFIG_TRAD_SIGNALS
	.setup_frame	= setup_frame,
#endif
	.setup_rt_frame = setup_rt_frame,
	.restart	= __NR_restart_syscall,

	.off_sc_fpregs = offsetof(struct sigcontext, sc_fpregs),
	.off_sc_fpc_csr = offsetof(struct sigcontext, sc_fpc_csr),
	.off_sc_used_math = offsetof(struct sigcontext, sc_used_math),

	.vdso		= &vdso_image,
};

static void handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
	int ret;
	struct mips_abi *abi = current->thread.abi;
	void *vdso = current->mm->context.vdso;

	/*
	 * If we were emulating a delay slot instruction, exit that frame such
	 * that addresses in the sigframe are as expected for userland and we
	 * don't have a problem if we reuse the thread's frame for an
	 * instruction within the signal handler.
	 */
	dsemul_thread_rollback(regs);

	if (regs->regs[0]) {
		switch(regs->regs[2]) {
		case ERESTART_RESTARTBLOCK:
		case ERESTARTNOHAND:
			regs->regs[2] = EINTR;
			break;
		case ERESTARTSYS:
			if (!(ksig->ka.sa.sa_flags & SA_RESTART)) {
				regs->regs[2] = EINTR;
				break;
			}
			fallthrough;
		case ERESTARTNOINTR:
			regs->regs[7] = regs->regs[26];
			regs->regs[2] = regs->regs[0];
			regs->cp0_epc -= 4;
		}

		regs->regs[0] = 0;		/* Don't deal with this again.	*/
	}

	rseq_signal_deliver(ksig, regs);

	if (sig_uses_siginfo(&ksig->ka, abi))
		ret = abi->setup_rt_frame(vdso + abi->vdso->off_rt_sigreturn,
					  ksig, regs, oldset);
	else
		ret = abi->setup_frame(vdso + abi->vdso->off_sigreturn,
				       ksig, regs, oldset);

	signal_setup_done(ret, ksig, 0);
}

static void do_signal(struct pt_regs *regs)
{
	struct ksignal ksig;

	if (get_signal(&ksig)) {
		/* Whee!  Actually deliver the signal.	*/
		handle_signal(&ksig, regs);
		return;
	}

	if (regs->regs[0]) {
		switch (regs->regs[2]) {
		case ERESTARTNOHAND:
		case ERESTARTSYS:
		case ERESTARTNOINTR:
			regs->regs[2] = regs->regs[0];
			regs->regs[7] = regs->regs[26];
			regs->cp0_epc -= 4;
			break;

		case ERESTART_RESTARTBLOCK:
			regs->regs[2] = current->thread.abi->restart;
			regs->regs[7] = regs->regs[26];
			regs->cp0_epc -= 4;
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

/*
 * notification of userspace execution resumption
 * - triggered by the TIF_WORK_MASK flags
 */
asmlinkage void do_notify_resume(struct pt_regs *regs, void *unused,
	__u32 thread_info_flags)
{
	local_irq_enable();

	user_exit();

	if (thread_info_flags & _TIF_UPROBE)
		uprobe_notify_resume(regs);

	/* deal with pending signal delivery */
	if (thread_info_flags & (_TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL))
		do_signal(regs);

	if (thread_info_flags & _TIF_NOTIFY_RESUME)
		resume_user_mode_work(regs);

	user_enter();
}

#if defined(CONFIG_SMP) && defined(CONFIG_MIPS_FP_SUPPORT)
static int smp_save_fp_context(void __user *sc)
{
	return raw_cpu_has_fpu
	       ? save_hw_fp_context(sc)
	       : copy_fp_to_sigcontext(sc);
}

static int smp_restore_fp_context(void __user *sc)
{
	return raw_cpu_has_fpu
	       ? restore_hw_fp_context(sc)
	       : copy_fp_from_sigcontext(sc);
}
#endif

static int signal_setup(void)
{
	/*
	 * The offset from sigcontext to extended context should be the same
	 * regardless of the type of signal, such that userland can always know
	 * where to look if it wishes to find the extended context structures.
	 */
	BUILD_BUG_ON((offsetof(struct sigframe, sf_extcontext) -
		      offsetof(struct sigframe, sf_sc)) !=
		     (offsetof(struct rt_sigframe, rs_uc.uc_extcontext) -
		      offsetof(struct rt_sigframe, rs_uc.uc_mcontext)));

#if defined(CONFIG_SMP) && defined(CONFIG_MIPS_FP_SUPPORT)
	/* For now just do the cpu_has_fpu check when the functions are invoked */
	save_fp_context = smp_save_fp_context;
	restore_fp_context = smp_restore_fp_context;
#else
	if (cpu_has_fpu) {
		save_fp_context = save_hw_fp_context;
		restore_fp_context = restore_hw_fp_context;
	} else {
		save_fp_context = copy_fp_to_sigcontext;
		restore_fp_context = copy_fp_from_sigcontext;
	}
#endif /* CONFIG_SMP */

	return 0;
}

arch_initcall(signal_setup);
