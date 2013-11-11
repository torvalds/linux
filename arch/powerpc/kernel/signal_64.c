/*
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/i386/kernel/signal.c"
 *    Copyright (C) 1991, 1992 Linus Torvalds
 *    1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/elf.h>
#include <linux/ptrace.h>
#include <linux/ratelimit.h>

#include <asm/sigcontext.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/unistd.h>
#include <asm/cacheflush.h>
#include <asm/syscalls.h>
#include <asm/vdso.h>
#include <asm/switch_to.h>
#include <asm/tm.h>

#include "signal.h"

#define DEBUG_SIG 0

#define GP_REGS_SIZE	min(sizeof(elf_gregset_t), sizeof(struct pt_regs))
#define FP_REGS_SIZE	sizeof(elf_fpregset_t)

#define TRAMP_TRACEBACK	3
#define TRAMP_SIZE	6

/*
 * When we have signals to deliver, we set up on the user stack,
 * going down from the original stack pointer:
 *	1) a rt_sigframe struct which contains the ucontext	
 *	2) a gap of __SIGNAL_FRAMESIZE bytes which acts as a dummy caller
 *	   frame for the signal handler.
 */

struct rt_sigframe {
	/* sys_rt_sigreturn requires the ucontext be the first field */
	struct ucontext uc;
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	struct ucontext uc_transact;
#endif
	unsigned long _unused[2];
	unsigned int tramp[TRAMP_SIZE];
	struct siginfo __user *pinfo;
	void __user *puc;
	struct siginfo info;
	/* 64 bit ABI allows for 288 bytes below sp before decrementing it. */
	char abigap[288];
} __attribute__ ((aligned (16)));

static const char fmt32[] = KERN_INFO \
	"%s[%d]: bad frame in %s: %08lx nip %08lx lr %08lx\n";
static const char fmt64[] = KERN_INFO \
	"%s[%d]: bad frame in %s: %016lx nip %016lx lr %016lx\n";

/*
 * Set up the sigcontext for the signal frame.
 */

static long setup_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs,
		 int signr, sigset_t *set, unsigned long handler,
		 int ctx_has_vsx_region)
{
	/* When CONFIG_ALTIVEC is set, we _always_ setup v_regs even if the
	 * process never used altivec yet (MSR_VEC is zero in pt_regs of
	 * the context). This is very important because we must ensure we
	 * don't lose the VRSAVE content that may have been set prior to
	 * the process doing its first vector operation
	 * Userland shall check AT_HWCAP to know whether it can rely on the
	 * v_regs pointer or not
	 */
#ifdef CONFIG_ALTIVEC
	elf_vrreg_t __user *v_regs = (elf_vrreg_t __user *)(((unsigned long)sc->vmx_reserve + 15) & ~0xful);
#endif
	unsigned long msr = regs->msr;
	long err = 0;

	flush_fp_to_thread(current);

#ifdef CONFIG_ALTIVEC
	err |= __put_user(v_regs, &sc->v_regs);

	/* save altivec registers */
	if (current->thread.used_vr) {
		flush_altivec_to_thread(current);
		/* Copy 33 vec registers (vr0..31 and vscr) to the stack */
		err |= __copy_to_user(v_regs, current->thread.vr, 33 * sizeof(vector128));
		/* set MSR_VEC in the MSR value in the frame to indicate that sc->v_reg)
		 * contains valid data.
		 */
		msr |= MSR_VEC;
	}
	/* We always copy to/from vrsave, it's 0 if we don't have or don't
	 * use altivec.
	 */
	err |= __put_user(current->thread.vrsave, (u32 __user *)&v_regs[33]);
#else /* CONFIG_ALTIVEC */
	err |= __put_user(0, &sc->v_regs);
#endif /* CONFIG_ALTIVEC */
	flush_fp_to_thread(current);
	/* copy fpr regs and fpscr */
	err |= copy_fpr_to_user(&sc->fp_regs, current);
#ifdef CONFIG_VSX
	/*
	 * Copy VSX low doubleword to local buffer for formatting,
	 * then out to userspace.  Update v_regs to point after the
	 * VMX data.
	 */
	if (current->thread.used_vsr && ctx_has_vsx_region) {
		__giveup_vsx(current);
		v_regs += ELF_NVRREG;
		err |= copy_vsx_to_user(v_regs, current);
		/* set MSR_VSX in the MSR value in the frame to
		 * indicate that sc->vs_reg) contains valid data.
		 */
		msr |= MSR_VSX;
	}
#endif /* CONFIG_VSX */
	err |= __put_user(&sc->gp_regs, &sc->regs);
	WARN_ON(!FULL_REGS(regs));
	err |= __copy_to_user(&sc->gp_regs, regs, GP_REGS_SIZE);
	err |= __put_user(msr, &sc->gp_regs[PT_MSR]);
	err |= __put_user(signr, &sc->signal);
	err |= __put_user(handler, &sc->handler);
	if (set != NULL)
		err |=  __put_user(set->sig[0], &sc->oldmask);

	return err;
}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
/*
 * As above, but Transactional Memory is in use, so deliver sigcontexts
 * containing checkpointed and transactional register states.
 *
 * To do this, we treclaim (done before entering here) to gather both sets of
 * registers and set up the 'normal' sigcontext registers with rolled-back
 * register values such that a simple signal handler sees a correct
 * checkpointed register state.  If interested, a TM-aware sighandler can
 * examine the transactional registers in the 2nd sigcontext to determine the
 * real origin of the signal.
 */
static long setup_tm_sigcontexts(struct sigcontext __user *sc,
				 struct sigcontext __user *tm_sc,
				 struct pt_regs *regs,
				 int signr, sigset_t *set, unsigned long handler)
{
	/* When CONFIG_ALTIVEC is set, we _always_ setup v_regs even if the
	 * process never used altivec yet (MSR_VEC is zero in pt_regs of
	 * the context). This is very important because we must ensure we
	 * don't lose the VRSAVE content that may have been set prior to
	 * the process doing its first vector operation
	 * Userland shall check AT_HWCAP to know wether it can rely on the
	 * v_regs pointer or not.
	 */
#ifdef CONFIG_ALTIVEC
	elf_vrreg_t __user *v_regs = (elf_vrreg_t __user *)
		(((unsigned long)sc->vmx_reserve + 15) & ~0xful);
	elf_vrreg_t __user *tm_v_regs = (elf_vrreg_t __user *)
		(((unsigned long)tm_sc->vmx_reserve + 15) & ~0xful);
#endif
	unsigned long msr = regs->msr;
	long err = 0;

	BUG_ON(!MSR_TM_ACTIVE(regs->msr));

	flush_fp_to_thread(current);

#ifdef CONFIG_ALTIVEC
	err |= __put_user(v_regs, &sc->v_regs);
	err |= __put_user(tm_v_regs, &tm_sc->v_regs);

	/* save altivec registers */
	if (current->thread.used_vr) {
		flush_altivec_to_thread(current);
		/* Copy 33 vec registers (vr0..31 and vscr) to the stack */
		err |= __copy_to_user(v_regs, current->thread.vr,
				      33 * sizeof(vector128));
		/* If VEC was enabled there are transactional VRs valid too,
		 * else they're a copy of the checkpointed VRs.
		 */
		if (msr & MSR_VEC)
			err |= __copy_to_user(tm_v_regs,
					      current->thread.transact_vr,
					      33 * sizeof(vector128));
		else
			err |= __copy_to_user(tm_v_regs,
					      current->thread.vr,
					      33 * sizeof(vector128));

		/* set MSR_VEC in the MSR value in the frame to indicate
		 * that sc->v_reg contains valid data.
		 */
		msr |= MSR_VEC;
	}
	/* We always copy to/from vrsave, it's 0 if we don't have or don't
	 * use altivec.
	 */
	err |= __put_user(current->thread.vrsave, (u32 __user *)&v_regs[33]);
	if (msr & MSR_VEC)
		err |= __put_user(current->thread.transact_vrsave,
				  (u32 __user *)&tm_v_regs[33]);
	else
		err |= __put_user(current->thread.vrsave,
				  (u32 __user *)&tm_v_regs[33]);

#else /* CONFIG_ALTIVEC */
	err |= __put_user(0, &sc->v_regs);
	err |= __put_user(0, &tm_sc->v_regs);
#endif /* CONFIG_ALTIVEC */

	/* copy fpr regs and fpscr */
	err |= copy_fpr_to_user(&sc->fp_regs, current);
	if (msr & MSR_FP)
		err |= copy_transact_fpr_to_user(&tm_sc->fp_regs, current);
	else
		err |= copy_fpr_to_user(&tm_sc->fp_regs, current);

#ifdef CONFIG_VSX
	/*
	 * Copy VSX low doubleword to local buffer for formatting,
	 * then out to userspace.  Update v_regs to point after the
	 * VMX data.
	 */
	if (current->thread.used_vsr) {
		__giveup_vsx(current);
		v_regs += ELF_NVRREG;
		tm_v_regs += ELF_NVRREG;

		err |= copy_vsx_to_user(v_regs, current);

		if (msr & MSR_VSX)
			err |= copy_transact_vsx_to_user(tm_v_regs, current);
		else
			err |= copy_vsx_to_user(tm_v_regs, current);

		/* set MSR_VSX in the MSR value in the frame to
		 * indicate that sc->vs_reg) contains valid data.
		 */
		msr |= MSR_VSX;
	}
#endif /* CONFIG_VSX */

	err |= __put_user(&sc->gp_regs, &sc->regs);
	err |= __put_user(&tm_sc->gp_regs, &tm_sc->regs);
	WARN_ON(!FULL_REGS(regs));
	err |= __copy_to_user(&tm_sc->gp_regs, regs, GP_REGS_SIZE);
	err |= __copy_to_user(&sc->gp_regs,
			      &current->thread.ckpt_regs, GP_REGS_SIZE);
	err |= __put_user(msr, &tm_sc->gp_regs[PT_MSR]);
	err |= __put_user(msr, &sc->gp_regs[PT_MSR]);
	err |= __put_user(signr, &sc->signal);
	err |= __put_user(handler, &sc->handler);
	if (set != NULL)
		err |=  __put_user(set->sig[0], &sc->oldmask);

	return err;
}
#endif

/*
 * Restore the sigcontext from the signal frame.
 */

static long restore_sigcontext(struct pt_regs *regs, sigset_t *set, int sig,
			      struct sigcontext __user *sc)
{
#ifdef CONFIG_ALTIVEC
	elf_vrreg_t __user *v_regs;
#endif
	unsigned long err = 0;
	unsigned long save_r13 = 0;
	unsigned long msr;
#ifdef CONFIG_VSX
	int i;
#endif

	/* If this is not a signal return, we preserve the TLS in r13 */
	if (!sig)
		save_r13 = regs->gpr[13];

	/* copy the GPRs */
	err |= __copy_from_user(regs->gpr, sc->gp_regs, sizeof(regs->gpr));
	err |= __get_user(regs->nip, &sc->gp_regs[PT_NIP]);
	/* get MSR separately, transfer the LE bit if doing signal return */
	err |= __get_user(msr, &sc->gp_regs[PT_MSR]);
	if (sig)
		regs->msr = (regs->msr & ~MSR_LE) | (msr & MSR_LE);
	err |= __get_user(regs->orig_gpr3, &sc->gp_regs[PT_ORIG_R3]);
	err |= __get_user(regs->ctr, &sc->gp_regs[PT_CTR]);
	err |= __get_user(regs->link, &sc->gp_regs[PT_LNK]);
	err |= __get_user(regs->xer, &sc->gp_regs[PT_XER]);
	err |= __get_user(regs->ccr, &sc->gp_regs[PT_CCR]);
	/* skip SOFTE */
	regs->trap = 0;
	err |= __get_user(regs->dar, &sc->gp_regs[PT_DAR]);
	err |= __get_user(regs->dsisr, &sc->gp_regs[PT_DSISR]);
	err |= __get_user(regs->result, &sc->gp_regs[PT_RESULT]);

	if (!sig)
		regs->gpr[13] = save_r13;
	if (set != NULL)
		err |=  __get_user(set->sig[0], &sc->oldmask);

	/*
	 * Do this before updating the thread state in
	 * current->thread.fpr/vr.  That way, if we get preempted
	 * and another task grabs the FPU/Altivec, it won't be
	 * tempted to save the current CPU state into the thread_struct
	 * and corrupt what we are writing there.
	 */
	discard_lazy_cpu_state();

	/*
	 * Force reload of FP/VEC.
	 * This has to be done before copying stuff into current->thread.fpr/vr
	 * for the reasons explained in the previous comment.
	 */
	regs->msr &= ~(MSR_FP | MSR_FE0 | MSR_FE1 | MSR_VEC | MSR_VSX);

#ifdef CONFIG_ALTIVEC
	err |= __get_user(v_regs, &sc->v_regs);
	if (err)
		return err;
	if (v_regs && !access_ok(VERIFY_READ, v_regs, 34 * sizeof(vector128)))
		return -EFAULT;
	/* Copy 33 vec registers (vr0..31 and vscr) from the stack */
	if (v_regs != 0 && (msr & MSR_VEC) != 0)
		err |= __copy_from_user(current->thread.vr, v_regs,
					33 * sizeof(vector128));
	else if (current->thread.used_vr)
		memset(current->thread.vr, 0, 33 * sizeof(vector128));
	/* Always get VRSAVE back */
	if (v_regs != 0)
		err |= __get_user(current->thread.vrsave, (u32 __user *)&v_regs[33]);
	else
		current->thread.vrsave = 0;
#endif /* CONFIG_ALTIVEC */
	/* restore floating point */
	err |= copy_fpr_from_user(current, &sc->fp_regs);
#ifdef CONFIG_VSX
	/*
	 * Get additional VSX data. Update v_regs to point after the
	 * VMX data.  Copy VSX low doubleword from userspace to local
	 * buffer for formatting, then into the taskstruct.
	 */
	v_regs += ELF_NVRREG;
	if ((msr & MSR_VSX) != 0)
		err |= copy_vsx_from_user(current, v_regs);
	else
		for (i = 0; i < 32 ; i++)
			current->thread.fpr[i][TS_VSRLOWOFFSET] = 0;
#endif
	return err;
}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
/*
 * Restore the two sigcontexts from the frame of a transactional processes.
 */

static long restore_tm_sigcontexts(struct pt_regs *regs,
				   struct sigcontext __user *sc,
				   struct sigcontext __user *tm_sc)
{
#ifdef CONFIG_ALTIVEC
	elf_vrreg_t __user *v_regs, *tm_v_regs;
#endif
	unsigned long err = 0;
	unsigned long msr;
#ifdef CONFIG_VSX
	int i;
#endif
	/* copy the GPRs */
	err |= __copy_from_user(regs->gpr, tm_sc->gp_regs, sizeof(regs->gpr));
	err |= __copy_from_user(&current->thread.ckpt_regs, sc->gp_regs,
				sizeof(regs->gpr));

	/*
	 * TFHAR is restored from the checkpointed 'wound-back' ucontext's NIP.
	 * TEXASR was set by the signal delivery reclaim, as was TFIAR.
	 * Users doing anything abhorrent like thread-switching w/ signals for
	 * TM-Suspended code will have to back TEXASR/TFIAR up themselves.
	 * For the case of getting a signal and simply returning from it,
	 * we don't need to re-copy them here.
	 */
	err |= __get_user(regs->nip, &tm_sc->gp_regs[PT_NIP]);
	err |= __get_user(current->thread.tm_tfhar, &sc->gp_regs[PT_NIP]);

	/* get MSR separately, transfer the LE bit if doing signal return */
	err |= __get_user(msr, &sc->gp_regs[PT_MSR]);
	/* pull in MSR TM from user context */
	regs->msr = (regs->msr & ~MSR_TS_MASK) | (msr & MSR_TS_MASK);

	/* pull in MSR LE from user context */
	regs->msr = (regs->msr & ~MSR_LE) | (msr & MSR_LE);

	/* The following non-GPR non-FPR non-VR state is also checkpointed: */
	err |= __get_user(regs->ctr, &tm_sc->gp_regs[PT_CTR]);
	err |= __get_user(regs->link, &tm_sc->gp_regs[PT_LNK]);
	err |= __get_user(regs->xer, &tm_sc->gp_regs[PT_XER]);
	err |= __get_user(regs->ccr, &tm_sc->gp_regs[PT_CCR]);
	err |= __get_user(current->thread.ckpt_regs.ctr,
			  &sc->gp_regs[PT_CTR]);
	err |= __get_user(current->thread.ckpt_regs.link,
			  &sc->gp_regs[PT_LNK]);
	err |= __get_user(current->thread.ckpt_regs.xer,
			  &sc->gp_regs[PT_XER]);
	err |= __get_user(current->thread.ckpt_regs.ccr,
			  &sc->gp_regs[PT_CCR]);

	/* These regs are not checkpointed; they can go in 'regs'. */
	err |= __get_user(regs->trap, &sc->gp_regs[PT_TRAP]);
	err |= __get_user(regs->dar, &sc->gp_regs[PT_DAR]);
	err |= __get_user(regs->dsisr, &sc->gp_regs[PT_DSISR]);
	err |= __get_user(regs->result, &sc->gp_regs[PT_RESULT]);

	/*
	 * Do this before updating the thread state in
	 * current->thread.fpr/vr.  That way, if we get preempted
	 * and another task grabs the FPU/Altivec, it won't be
	 * tempted to save the current CPU state into the thread_struct
	 * and corrupt what we are writing there.
	 */
	discard_lazy_cpu_state();

	/*
	 * Force reload of FP/VEC.
	 * This has to be done before copying stuff into current->thread.fpr/vr
	 * for the reasons explained in the previous comment.
	 */
	regs->msr &= ~(MSR_FP | MSR_FE0 | MSR_FE1 | MSR_VEC | MSR_VSX);

#ifdef CONFIG_ALTIVEC
	err |= __get_user(v_regs, &sc->v_regs);
	err |= __get_user(tm_v_regs, &tm_sc->v_regs);
	if (err)
		return err;
	if (v_regs && !access_ok(VERIFY_READ, v_regs, 34 * sizeof(vector128)))
		return -EFAULT;
	if (tm_v_regs && !access_ok(VERIFY_READ,
				    tm_v_regs, 34 * sizeof(vector128)))
		return -EFAULT;
	/* Copy 33 vec registers (vr0..31 and vscr) from the stack */
	if (v_regs != 0 && tm_v_regs != 0 && (msr & MSR_VEC) != 0) {
		err |= __copy_from_user(current->thread.vr, v_regs,
					33 * sizeof(vector128));
		err |= __copy_from_user(current->thread.transact_vr, tm_v_regs,
					33 * sizeof(vector128));
	}
	else if (current->thread.used_vr) {
		memset(current->thread.vr, 0, 33 * sizeof(vector128));
		memset(current->thread.transact_vr, 0, 33 * sizeof(vector128));
	}
	/* Always get VRSAVE back */
	if (v_regs != 0 && tm_v_regs != 0) {
		err |= __get_user(current->thread.vrsave,
				  (u32 __user *)&v_regs[33]);
		err |= __get_user(current->thread.transact_vrsave,
				  (u32 __user *)&tm_v_regs[33]);
	}
	else {
		current->thread.vrsave = 0;
		current->thread.transact_vrsave = 0;
	}
#endif /* CONFIG_ALTIVEC */
	/* restore floating point */
	err |= copy_fpr_from_user(current, &sc->fp_regs);
	err |= copy_transact_fpr_from_user(current, &tm_sc->fp_regs);
#ifdef CONFIG_VSX
	/*
	 * Get additional VSX data. Update v_regs to point after the
	 * VMX data.  Copy VSX low doubleword from userspace to local
	 * buffer for formatting, then into the taskstruct.
	 */
	if (v_regs && ((msr & MSR_VSX) != 0)) {
		v_regs += ELF_NVRREG;
		tm_v_regs += ELF_NVRREG;
		err |= copy_vsx_from_user(current, v_regs);
		err |= copy_transact_vsx_from_user(current, tm_v_regs);
	} else {
		for (i = 0; i < 32 ; i++) {
			current->thread.fpr[i][TS_VSRLOWOFFSET] = 0;
			current->thread.transact_fpr[i][TS_VSRLOWOFFSET] = 0;
		}
	}
#endif
	tm_enable();
	/* This loads the checkpointed FP/VEC state, if used */
	tm_recheckpoint(&current->thread, msr);

	/* This loads the speculative FP/VEC state, if used */
	if (msr & MSR_FP) {
		do_load_up_transact_fpu(&current->thread);
		regs->msr |= (MSR_FP | current->thread.fpexc_mode);
	}
#ifdef CONFIG_ALTIVEC
	if (msr & MSR_VEC) {
		do_load_up_transact_altivec(&current->thread);
		regs->msr |= MSR_VEC;
	}
#endif

	return err;
}
#endif

/*
 * Setup the trampoline code on the stack
 */
static long setup_trampoline(unsigned int syscall, unsigned int __user *tramp)
{
	int i;
	long err = 0;

	/* addi r1, r1, __SIGNAL_FRAMESIZE  # Pop the dummy stackframe */
	err |= __put_user(0x38210000UL | (__SIGNAL_FRAMESIZE & 0xffff), &tramp[0]);
	/* li r0, __NR_[rt_]sigreturn| */
	err |= __put_user(0x38000000UL | (syscall & 0xffff), &tramp[1]);
	/* sc */
	err |= __put_user(0x44000002UL, &tramp[2]);

	/* Minimal traceback info */
	for (i=TRAMP_TRACEBACK; i < TRAMP_SIZE ;i++)
		err |= __put_user(0, &tramp[i]);

	if (!err)
		flush_icache_range((unsigned long) &tramp[0],
			   (unsigned long) &tramp[TRAMP_SIZE]);

	return err;
}

/*
 * Userspace code may pass a ucontext which doesn't include VSX added
 * at the end.  We need to check for this case.
 */
#define UCONTEXTSIZEWITHOUTVSX \
		(sizeof(struct ucontext) - 32*sizeof(long))

/*
 * Handle {get,set,swap}_context operations
 */
int sys_swapcontext(struct ucontext __user *old_ctx,
		    struct ucontext __user *new_ctx,
		    long ctx_size, long r6, long r7, long r8, struct pt_regs *regs)
{
	unsigned char tmp;
	sigset_t set;
	unsigned long new_msr = 0;
	int ctx_has_vsx_region = 0;

	if (new_ctx &&
	    get_user(new_msr, &new_ctx->uc_mcontext.gp_regs[PT_MSR]))
		return -EFAULT;
	/*
	 * Check that the context is not smaller than the original
	 * size (with VMX but without VSX)
	 */
	if (ctx_size < UCONTEXTSIZEWITHOUTVSX)
		return -EINVAL;
	/*
	 * If the new context state sets the MSR VSX bits but
	 * it doesn't provide VSX state.
	 */
	if ((ctx_size < sizeof(struct ucontext)) &&
	    (new_msr & MSR_VSX))
		return -EINVAL;
	/* Does the context have enough room to store VSX data? */
	if (ctx_size >= sizeof(struct ucontext))
		ctx_has_vsx_region = 1;

	if (old_ctx != NULL) {
		if (!access_ok(VERIFY_WRITE, old_ctx, ctx_size)
		    || setup_sigcontext(&old_ctx->uc_mcontext, regs, 0, NULL, 0,
					ctx_has_vsx_region)
		    || __copy_to_user(&old_ctx->uc_sigmask,
				      &current->blocked, sizeof(sigset_t)))
			return -EFAULT;
	}
	if (new_ctx == NULL)
		return 0;
	if (!access_ok(VERIFY_READ, new_ctx, ctx_size)
	    || __get_user(tmp, (u8 __user *) new_ctx)
	    || __get_user(tmp, (u8 __user *) new_ctx + ctx_size - 1))
		return -EFAULT;

	/*
	 * If we get a fault copying the context into the kernel's
	 * image of the user's registers, we can't just return -EFAULT
	 * because the user's registers will be corrupted.  For instance
	 * the NIP value may have been updated but not some of the
	 * other registers.  Given that we have done the access_ok
	 * and successfully read the first and last bytes of the region
	 * above, this should only happen in an out-of-memory situation
	 * or if another thread unmaps the region containing the context.
	 * We kill the task with a SIGSEGV in this situation.
	 */

	if (__copy_from_user(&set, &new_ctx->uc_sigmask, sizeof(set)))
		do_exit(SIGSEGV);
	set_current_blocked(&set);
	if (restore_sigcontext(regs, NULL, 0, &new_ctx->uc_mcontext))
		do_exit(SIGSEGV);

	/* This returns like rt_sigreturn */
	set_thread_flag(TIF_RESTOREALL);
	return 0;
}


/*
 * Do a signal return; undo the signal stack.
 */

int sys_rt_sigreturn(unsigned long r3, unsigned long r4, unsigned long r5,
		     unsigned long r6, unsigned long r7, unsigned long r8,
		     struct pt_regs *regs)
{
	struct ucontext __user *uc = (struct ucontext __user *)regs->gpr[1];
	sigset_t set;
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	unsigned long msr;
#endif

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	if (!access_ok(VERIFY_READ, uc, sizeof(*uc)))
		goto badframe;

	if (__copy_from_user(&set, &uc->uc_sigmask, sizeof(set)))
		goto badframe;
	set_current_blocked(&set);
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	if (__get_user(msr, &uc->uc_mcontext.gp_regs[PT_MSR]))
		goto badframe;
	if (MSR_TM_ACTIVE(msr)) {
		/* We recheckpoint on return. */
		struct ucontext __user *uc_transact;
		if (__get_user(uc_transact, &uc->uc_link))
			goto badframe;
		if (restore_tm_sigcontexts(regs, &uc->uc_mcontext,
					   &uc_transact->uc_mcontext))
			goto badframe;
	}
	else
	/* Fall through, for non-TM restore */
#endif
	if (restore_sigcontext(regs, NULL, 1, &uc->uc_mcontext))
		goto badframe;

	if (restore_altstack(&uc->uc_stack))
		goto badframe;

	set_thread_flag(TIF_RESTOREALL);
	return 0;

badframe:
#if DEBUG_SIG
	printk("badframe in sys_rt_sigreturn, regs=%p uc=%p &uc->uc_mcontext=%p\n",
	       regs, uc, &uc->uc_mcontext);
#endif
	if (show_unhandled_signals)
		printk_ratelimited(regs->msr & MSR_64BIT ? fmt64 : fmt32,
				   current->comm, current->pid, "rt_sigreturn",
				   (long)uc, regs->nip, regs->link);

	force_sig(SIGSEGV, current);
	return 0;
}

int handle_rt_signal64(int signr, struct k_sigaction *ka, siginfo_t *info,
		sigset_t *set, struct pt_regs *regs)
{
	/* Handler is *really* a pointer to the function descriptor for
	 * the signal routine.  The first entry in the function
	 * descriptor is the entry address of signal and the second
	 * entry is the TOC value we need to use.
	 */
	func_descr_t __user *funct_desc_ptr;
	struct rt_sigframe __user *frame;
	unsigned long newsp = 0;
	long err = 0;

	frame = get_sigframe(ka, get_tm_stackpointer(regs), sizeof(*frame), 0);
	if (unlikely(frame == NULL))
		goto badframe;

	err |= __put_user(&frame->info, &frame->pinfo);
	err |= __put_user(&frame->uc, &frame->puc);
	err |= copy_siginfo_to_user(&frame->info, info);
	if (err)
		goto badframe;

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __save_altstack(&frame->uc.uc_stack, regs->gpr[1]);
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	if (MSR_TM_ACTIVE(regs->msr)) {
		/* The ucontext_t passed to userland points to the second
		 * ucontext_t (for transactional state) with its uc_link ptr.
		 */
		err |= __put_user(&frame->uc_transact, &frame->uc.uc_link);
		err |= setup_tm_sigcontexts(&frame->uc.uc_mcontext,
					    &frame->uc_transact.uc_mcontext,
					    regs, signr,
					    NULL,
					    (unsigned long)ka->sa.sa_handler);
	} else
#endif
	{
		err |= __put_user(0, &frame->uc.uc_link);
		err |= setup_sigcontext(&frame->uc.uc_mcontext, regs, signr,
					NULL, (unsigned long)ka->sa.sa_handler,
					1);
	}
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		goto badframe;

	/* Make sure signal handler doesn't get spurious FP exceptions */
	current->thread.fpscr.val = 0;
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	/* Remove TM bits from thread's MSR.  The MSR in the sigcontext
	 * just indicates to userland that we were doing a transaction, but we
	 * don't want to return in transactional state:
	 */
	regs->msr &= ~MSR_TS_MASK;
#endif

	/* Set up to return from userspace. */
	if (vdso64_rt_sigtramp && current->mm->context.vdso_base) {
		regs->link = current->mm->context.vdso_base + vdso64_rt_sigtramp;
	} else {
		err |= setup_trampoline(__NR_rt_sigreturn, &frame->tramp[0]);
		if (err)
			goto badframe;
		regs->link = (unsigned long) &frame->tramp[0];
	}
	funct_desc_ptr = (func_descr_t __user *) ka->sa.sa_handler;

	/* Allocate a dummy caller frame for the signal handler. */
	newsp = ((unsigned long)frame) - __SIGNAL_FRAMESIZE;
	err |= put_user(regs->gpr[1], (unsigned long __user *)newsp);

	/* Set up "regs" so we "return" to the signal handler. */
	err |= get_user(regs->nip, &funct_desc_ptr->entry);
	/* enter the signal handler in big-endian mode */
	regs->msr &= ~MSR_LE;
	regs->gpr[1] = newsp;
	err |= get_user(regs->gpr[2], &funct_desc_ptr->toc);
	regs->gpr[3] = signr;
	regs->result = 0;
	if (ka->sa.sa_flags & SA_SIGINFO) {
		err |= get_user(regs->gpr[4], (unsigned long __user *)&frame->pinfo);
		err |= get_user(regs->gpr[5], (unsigned long __user *)&frame->puc);
		regs->gpr[6] = (unsigned long) frame;
	} else {
		regs->gpr[4] = (unsigned long)&frame->uc.uc_mcontext;
	}
	if (err)
		goto badframe;

	return 1;

badframe:
#if DEBUG_SIG
	printk("badframe in setup_rt_frame, regs=%p frame=%p newsp=%lx\n",
	       regs, frame, newsp);
#endif
	if (show_unhandled_signals)
		printk_ratelimited(regs->msr & MSR_64BIT ? fmt64 : fmt32,
				   current->comm, current->pid, "setup_rt_frame",
				   (long)frame, regs->nip, regs->link);

	force_sigsegv(signr, current);
	return 0;
}
