// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/i386/kernel/signal.c"
 *    Copyright (C) 1991, 1992 Linus Torvalds
 *    1997-11-28  Modified for POSIX.1b signals by Richard Henderson
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
#include <linux/syscalls.h>
#include <linux/pagemap.h>

#include <asm/sigcontext.h>
#include <asm/ucontext.h>
#include <linux/uaccess.h>
#include <asm/unistd.h>
#include <asm/cacheflush.h>
#include <asm/syscalls.h>
#include <asm/vdso.h>
#include <asm/switch_to.h>
#include <asm/tm.h>
#include <asm/asm-prototypes.h>

#include "signal.h"


#define GP_REGS_SIZE	min(sizeof(elf_gregset_t), sizeof(struct pt_regs))
#define FP_REGS_SIZE	sizeof(elf_fpregset_t)

#define TRAMP_TRACEBACK	4
#define TRAMP_SIZE	7

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
	/* New 64 bit little-endian ABI allows redzone of 512 bytes below sp */
	char abigap[USER_REDZONE_SIZE];
} __attribute__ ((aligned (16)));

/*
 * This computes a quad word aligned pointer inside the vmx_reserve array
 * element. For historical reasons sigcontext might not be quad word aligned,
 * but the location we write the VMX regs to must be. See the comment in
 * sigcontext for more detail.
 */
#ifdef CONFIG_ALTIVEC
static elf_vrreg_t __user *sigcontext_vmx_regs(struct sigcontext __user *sc)
{
	return (elf_vrreg_t __user *) (((unsigned long)sc->vmx_reserve + 15) & ~0xful);
}
#endif

static void prepare_setup_sigcontext(struct task_struct *tsk)
{
#ifdef CONFIG_ALTIVEC
	/* save altivec registers */
	if (tsk->thread.used_vr)
		flush_altivec_to_thread(tsk);
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		tsk->thread.vrsave = mfspr(SPRN_VRSAVE);
#endif /* CONFIG_ALTIVEC */

	flush_fp_to_thread(tsk);

#ifdef CONFIG_VSX
	if (tsk->thread.used_vsr)
		flush_vsx_to_thread(tsk);
#endif /* CONFIG_VSX */
}

/*
 * Set up the sigcontext for the signal frame.
 */

#define unsafe_setup_sigcontext(sc, tsk, signr, set, handler, ctx_has_vsx_region, label)\
do {											\
	if (__unsafe_setup_sigcontext(sc, tsk, signr, set, handler, ctx_has_vsx_region))\
		goto label;								\
} while (0)
static long notrace __unsafe_setup_sigcontext(struct sigcontext __user *sc,
					struct task_struct *tsk, int signr, sigset_t *set,
					unsigned long handler, int ctx_has_vsx_region)
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
	elf_vrreg_t __user *v_regs = sigcontext_vmx_regs(sc);
#endif
	struct pt_regs *regs = tsk->thread.regs;
	unsigned long msr = regs->msr;
	/* Force usr to alway see softe as 1 (interrupts enabled) */
	unsigned long softe = 0x1;

	BUG_ON(tsk != current);

#ifdef CONFIG_ALTIVEC
	unsafe_put_user(v_regs, &sc->v_regs, efault_out);

	/* save altivec registers */
	if (tsk->thread.used_vr) {
		/* Copy 33 vec registers (vr0..31 and vscr) to the stack */
		unsafe_copy_to_user(v_regs, &tsk->thread.vr_state,
				    33 * sizeof(vector128), efault_out);
		/* set MSR_VEC in the MSR value in the frame to indicate that sc->v_reg)
		 * contains valid data.
		 */
		msr |= MSR_VEC;
	}
	/* We always copy to/from vrsave, it's 0 if we don't have or don't
	 * use altivec.
	 */
	unsafe_put_user(tsk->thread.vrsave, (u32 __user *)&v_regs[33], efault_out);
#else /* CONFIG_ALTIVEC */
	unsafe_put_user(0, &sc->v_regs, efault_out);
#endif /* CONFIG_ALTIVEC */
	/* copy fpr regs and fpscr */
	unsafe_copy_fpr_to_user(&sc->fp_regs, tsk, efault_out);

	/*
	 * Clear the MSR VSX bit to indicate there is no valid state attached
	 * to this context, except in the specific case below where we set it.
	 */
	msr &= ~MSR_VSX;
#ifdef CONFIG_VSX
	/*
	 * Copy VSX low doubleword to local buffer for formatting,
	 * then out to userspace.  Update v_regs to point after the
	 * VMX data.
	 */
	if (tsk->thread.used_vsr && ctx_has_vsx_region) {
		v_regs += ELF_NVRREG;
		unsafe_copy_vsx_to_user(v_regs, tsk, efault_out);
		/* set MSR_VSX in the MSR value in the frame to
		 * indicate that sc->vs_reg) contains valid data.
		 */
		msr |= MSR_VSX;
	}
#endif /* CONFIG_VSX */
	unsafe_put_user(&sc->gp_regs, &sc->regs, efault_out);
	unsafe_copy_to_user(&sc->gp_regs, regs, GP_REGS_SIZE, efault_out);
	unsafe_put_user(msr, &sc->gp_regs[PT_MSR], efault_out);
	unsafe_put_user(softe, &sc->gp_regs[PT_SOFTE], efault_out);
	unsafe_put_user(signr, &sc->signal, efault_out);
	unsafe_put_user(handler, &sc->handler, efault_out);
	if (set != NULL)
		unsafe_put_user(set->sig[0], &sc->oldmask, efault_out);

	return 0;

efault_out:
	return -EFAULT;
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
				 struct task_struct *tsk,
				 int signr, sigset_t *set, unsigned long handler,
				 unsigned long msr)
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
	elf_vrreg_t __user *v_regs = sigcontext_vmx_regs(sc);
	elf_vrreg_t __user *tm_v_regs = sigcontext_vmx_regs(tm_sc);
#endif
	struct pt_regs *regs = tsk->thread.regs;
	long err = 0;

	BUG_ON(tsk != current);

	BUG_ON(!MSR_TM_ACTIVE(msr));

	WARN_ON(tm_suspend_disabled);

	/* Restore checkpointed FP, VEC, and VSX bits from ckpt_regs as
	 * it contains the correct FP, VEC, VSX state after we treclaimed
	 * the transaction and giveup_all() was called on reclaiming.
	 */
	msr |= tsk->thread.ckpt_regs.msr & (MSR_FP | MSR_VEC | MSR_VSX);

#ifdef CONFIG_ALTIVEC
	err |= __put_user(v_regs, &sc->v_regs);
	err |= __put_user(tm_v_regs, &tm_sc->v_regs);

	/* save altivec registers */
	if (tsk->thread.used_vr) {
		/* Copy 33 vec registers (vr0..31 and vscr) to the stack */
		err |= __copy_to_user(v_regs, &tsk->thread.ckvr_state,
				      33 * sizeof(vector128));
		/* If VEC was enabled there are transactional VRs valid too,
		 * else they're a copy of the checkpointed VRs.
		 */
		if (msr & MSR_VEC)
			err |= __copy_to_user(tm_v_regs,
					      &tsk->thread.vr_state,
					      33 * sizeof(vector128));
		else
			err |= __copy_to_user(tm_v_regs,
					      &tsk->thread.ckvr_state,
					      33 * sizeof(vector128));

		/* set MSR_VEC in the MSR value in the frame to indicate
		 * that sc->v_reg contains valid data.
		 */
		msr |= MSR_VEC;
	}
	/* We always copy to/from vrsave, it's 0 if we don't have or don't
	 * use altivec.
	 */
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		tsk->thread.ckvrsave = mfspr(SPRN_VRSAVE);
	err |= __put_user(tsk->thread.ckvrsave, (u32 __user *)&v_regs[33]);
	if (msr & MSR_VEC)
		err |= __put_user(tsk->thread.vrsave,
				  (u32 __user *)&tm_v_regs[33]);
	else
		err |= __put_user(tsk->thread.ckvrsave,
				  (u32 __user *)&tm_v_regs[33]);

#else /* CONFIG_ALTIVEC */
	err |= __put_user(0, &sc->v_regs);
	err |= __put_user(0, &tm_sc->v_regs);
#endif /* CONFIG_ALTIVEC */

	/* copy fpr regs and fpscr */
	err |= copy_ckfpr_to_user(&sc->fp_regs, tsk);
	if (msr & MSR_FP)
		err |= copy_fpr_to_user(&tm_sc->fp_regs, tsk);
	else
		err |= copy_ckfpr_to_user(&tm_sc->fp_regs, tsk);

#ifdef CONFIG_VSX
	/*
	 * Copy VSX low doubleword to local buffer for formatting,
	 * then out to userspace.  Update v_regs to point after the
	 * VMX data.
	 */
	if (tsk->thread.used_vsr) {
		v_regs += ELF_NVRREG;
		tm_v_regs += ELF_NVRREG;

		err |= copy_ckvsx_to_user(v_regs, tsk);

		if (msr & MSR_VSX)
			err |= copy_vsx_to_user(tm_v_regs, tsk);
		else
			err |= copy_ckvsx_to_user(tm_v_regs, tsk);

		/* set MSR_VSX in the MSR value in the frame to
		 * indicate that sc->vs_reg) contains valid data.
		 */
		msr |= MSR_VSX;
	}
#endif /* CONFIG_VSX */

	err |= __put_user(&sc->gp_regs, &sc->regs);
	err |= __put_user(&tm_sc->gp_regs, &tm_sc->regs);
	err |= __copy_to_user(&tm_sc->gp_regs, regs, GP_REGS_SIZE);
	err |= __copy_to_user(&sc->gp_regs,
			      &tsk->thread.ckpt_regs, GP_REGS_SIZE);
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
#define unsafe_restore_sigcontext(tsk, set, sig, sc, label) do {	\
	if (__unsafe_restore_sigcontext(tsk, set, sig, sc))		\
		goto label;						\
} while (0)
static long notrace __unsafe_restore_sigcontext(struct task_struct *tsk, sigset_t *set,
						int sig, struct sigcontext __user *sc)
{
#ifdef CONFIG_ALTIVEC
	elf_vrreg_t __user *v_regs;
#endif
	unsigned long save_r13 = 0;
	unsigned long msr;
	struct pt_regs *regs = tsk->thread.regs;
#ifdef CONFIG_VSX
	int i;
#endif

	BUG_ON(tsk != current);

	/* If this is not a signal return, we preserve the TLS in r13 */
	if (!sig)
		save_r13 = regs->gpr[13];

	/* copy the GPRs */
	unsafe_copy_from_user(regs->gpr, sc->gp_regs, sizeof(regs->gpr), efault_out);
	unsafe_get_user(regs->nip, &sc->gp_regs[PT_NIP], efault_out);
	/* get MSR separately, transfer the LE bit if doing signal return */
	unsafe_get_user(msr, &sc->gp_regs[PT_MSR], efault_out);
	if (sig)
		regs_set_return_msr(regs, (regs->msr & ~MSR_LE) | (msr & MSR_LE));
	unsafe_get_user(regs->orig_gpr3, &sc->gp_regs[PT_ORIG_R3], efault_out);
	unsafe_get_user(regs->ctr, &sc->gp_regs[PT_CTR], efault_out);
	unsafe_get_user(regs->link, &sc->gp_regs[PT_LNK], efault_out);
	unsafe_get_user(regs->xer, &sc->gp_regs[PT_XER], efault_out);
	unsafe_get_user(regs->ccr, &sc->gp_regs[PT_CCR], efault_out);
	/* Don't allow userspace to set SOFTE */
	set_trap_norestart(regs);
	unsafe_get_user(regs->dar, &sc->gp_regs[PT_DAR], efault_out);
	unsafe_get_user(regs->dsisr, &sc->gp_regs[PT_DSISR], efault_out);
	unsafe_get_user(regs->result, &sc->gp_regs[PT_RESULT], efault_out);

	if (!sig)
		regs->gpr[13] = save_r13;
	if (set != NULL)
		unsafe_get_user(set->sig[0], &sc->oldmask, efault_out);

	/*
	 * Force reload of FP/VEC.
	 * This has to be done before copying stuff into tsk->thread.fpr/vr
	 * for the reasons explained in the previous comment.
	 */
	regs_set_return_msr(regs, regs->msr & ~(MSR_FP | MSR_FE0 | MSR_FE1 | MSR_VEC | MSR_VSX));

#ifdef CONFIG_ALTIVEC
	unsafe_get_user(v_regs, &sc->v_regs, efault_out);
	if (v_regs && !access_ok(v_regs, 34 * sizeof(vector128)))
		return -EFAULT;
	/* Copy 33 vec registers (vr0..31 and vscr) from the stack */
	if (v_regs != NULL && (msr & MSR_VEC) != 0) {
		unsafe_copy_from_user(&tsk->thread.vr_state, v_regs,
				      33 * sizeof(vector128), efault_out);
		tsk->thread.used_vr = true;
	} else if (tsk->thread.used_vr) {
		memset(&tsk->thread.vr_state, 0, 33 * sizeof(vector128));
	}
	/* Always get VRSAVE back */
	if (v_regs != NULL)
		unsafe_get_user(tsk->thread.vrsave, (u32 __user *)&v_regs[33], efault_out);
	else
		tsk->thread.vrsave = 0;
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		mtspr(SPRN_VRSAVE, tsk->thread.vrsave);
#endif /* CONFIG_ALTIVEC */
	/* restore floating point */
	unsafe_copy_fpr_from_user(tsk, &sc->fp_regs, efault_out);
#ifdef CONFIG_VSX
	/*
	 * Get additional VSX data. Update v_regs to point after the
	 * VMX data.  Copy VSX low doubleword from userspace to local
	 * buffer for formatting, then into the taskstruct.
	 */
	v_regs += ELF_NVRREG;
	if ((msr & MSR_VSX) != 0) {
		unsafe_copy_vsx_from_user(tsk, v_regs, efault_out);
		tsk->thread.used_vsr = true;
	} else {
		for (i = 0; i < 32 ; i++)
			tsk->thread.fp_state.fpr[i][TS_VSRLOWOFFSET] = 0;
	}
#endif
	return 0;

efault_out:
	return -EFAULT;
}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
/*
 * Restore the two sigcontexts from the frame of a transactional processes.
 */

static long restore_tm_sigcontexts(struct task_struct *tsk,
				   struct sigcontext __user *sc,
				   struct sigcontext __user *tm_sc)
{
#ifdef CONFIG_ALTIVEC
	elf_vrreg_t __user *v_regs, *tm_v_regs;
#endif
	unsigned long err = 0;
	unsigned long msr;
	struct pt_regs *regs = tsk->thread.regs;
#ifdef CONFIG_VSX
	int i;
#endif

	BUG_ON(tsk != current);

	if (tm_suspend_disabled)
		return -EINVAL;

	/* copy the GPRs */
	err |= __copy_from_user(regs->gpr, tm_sc->gp_regs, sizeof(regs->gpr));
	err |= __copy_from_user(&tsk->thread.ckpt_regs, sc->gp_regs,
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
	err |= __get_user(tsk->thread.tm_tfhar, &sc->gp_regs[PT_NIP]);

	/* get MSR separately, transfer the LE bit if doing signal return */
	err |= __get_user(msr, &sc->gp_regs[PT_MSR]);
	/* Don't allow reserved mode. */
	if (MSR_TM_RESV(msr))
		return -EINVAL;

	/* pull in MSR LE from user context */
	regs_set_return_msr(regs, (regs->msr & ~MSR_LE) | (msr & MSR_LE));

	/* The following non-GPR non-FPR non-VR state is also checkpointed: */
	err |= __get_user(regs->ctr, &tm_sc->gp_regs[PT_CTR]);
	err |= __get_user(regs->link, &tm_sc->gp_regs[PT_LNK]);
	err |= __get_user(regs->xer, &tm_sc->gp_regs[PT_XER]);
	err |= __get_user(regs->ccr, &tm_sc->gp_regs[PT_CCR]);
	err |= __get_user(tsk->thread.ckpt_regs.ctr,
			  &sc->gp_regs[PT_CTR]);
	err |= __get_user(tsk->thread.ckpt_regs.link,
			  &sc->gp_regs[PT_LNK]);
	err |= __get_user(tsk->thread.ckpt_regs.xer,
			  &sc->gp_regs[PT_XER]);
	err |= __get_user(tsk->thread.ckpt_regs.ccr,
			  &sc->gp_regs[PT_CCR]);
	/* Don't allow userspace to set SOFTE */
	set_trap_norestart(regs);
	/* These regs are not checkpointed; they can go in 'regs'. */
	err |= __get_user(regs->dar, &sc->gp_regs[PT_DAR]);
	err |= __get_user(regs->dsisr, &sc->gp_regs[PT_DSISR]);
	err |= __get_user(regs->result, &sc->gp_regs[PT_RESULT]);

	/*
	 * Force reload of FP/VEC.
	 * This has to be done before copying stuff into tsk->thread.fpr/vr
	 * for the reasons explained in the previous comment.
	 */
	regs_set_return_msr(regs, regs->msr & ~(MSR_FP | MSR_FE0 | MSR_FE1 | MSR_VEC | MSR_VSX));

#ifdef CONFIG_ALTIVEC
	err |= __get_user(v_regs, &sc->v_regs);
	err |= __get_user(tm_v_regs, &tm_sc->v_regs);
	if (err)
		return err;
	if (v_regs && !access_ok(v_regs, 34 * sizeof(vector128)))
		return -EFAULT;
	if (tm_v_regs && !access_ok(tm_v_regs, 34 * sizeof(vector128)))
		return -EFAULT;
	/* Copy 33 vec registers (vr0..31 and vscr) from the stack */
	if (v_regs != NULL && tm_v_regs != NULL && (msr & MSR_VEC) != 0) {
		err |= __copy_from_user(&tsk->thread.ckvr_state, v_regs,
					33 * sizeof(vector128));
		err |= __copy_from_user(&tsk->thread.vr_state, tm_v_regs,
					33 * sizeof(vector128));
		current->thread.used_vr = true;
	}
	else if (tsk->thread.used_vr) {
		memset(&tsk->thread.vr_state, 0, 33 * sizeof(vector128));
		memset(&tsk->thread.ckvr_state, 0, 33 * sizeof(vector128));
	}
	/* Always get VRSAVE back */
	if (v_regs != NULL && tm_v_regs != NULL) {
		err |= __get_user(tsk->thread.ckvrsave,
				  (u32 __user *)&v_regs[33]);
		err |= __get_user(tsk->thread.vrsave,
				  (u32 __user *)&tm_v_regs[33]);
	}
	else {
		tsk->thread.vrsave = 0;
		tsk->thread.ckvrsave = 0;
	}
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		mtspr(SPRN_VRSAVE, tsk->thread.vrsave);
#endif /* CONFIG_ALTIVEC */
	/* restore floating point */
	err |= copy_fpr_from_user(tsk, &tm_sc->fp_regs);
	err |= copy_ckfpr_from_user(tsk, &sc->fp_regs);
#ifdef CONFIG_VSX
	/*
	 * Get additional VSX data. Update v_regs to point after the
	 * VMX data.  Copy VSX low doubleword from userspace to local
	 * buffer for formatting, then into the taskstruct.
	 */
	if (v_regs && ((msr & MSR_VSX) != 0)) {
		v_regs += ELF_NVRREG;
		tm_v_regs += ELF_NVRREG;
		err |= copy_vsx_from_user(tsk, tm_v_regs);
		err |= copy_ckvsx_from_user(tsk, v_regs);
		tsk->thread.used_vsr = true;
	} else {
		for (i = 0; i < 32 ; i++) {
			tsk->thread.fp_state.fpr[i][TS_VSRLOWOFFSET] = 0;
			tsk->thread.ckfp_state.fpr[i][TS_VSRLOWOFFSET] = 0;
		}
	}
#endif
	tm_enable();
	/* Make sure the transaction is marked as failed */
	tsk->thread.tm_texasr |= TEXASR_FS;

	/*
	 * Disabling preemption, since it is unsafe to be preempted
	 * with MSR[TS] set without recheckpointing.
	 */
	preempt_disable();

	/* pull in MSR TS bits from user context */
	regs_set_return_msr(regs, regs->msr | (msr & MSR_TS_MASK));

	/*
	 * Ensure that TM is enabled in regs->msr before we leave the signal
	 * handler. It could be the case that (a) user disabled the TM bit
	 * through the manipulation of the MSR bits in uc_mcontext or (b) the
	 * TM bit was disabled because a sufficient number of context switches
	 * happened whilst in the signal handler and load_tm overflowed,
	 * disabling the TM bit. In either case we can end up with an illegal
	 * TM state leading to a TM Bad Thing when we return to userspace.
	 *
	 * CAUTION:
	 * After regs->MSR[TS] being updated, make sure that get_user(),
	 * put_user() or similar functions are *not* called. These
	 * functions can generate page faults which will cause the process
	 * to be de-scheduled with MSR[TS] set but without calling
	 * tm_recheckpoint(). This can cause a bug.
	 */
	regs_set_return_msr(regs, regs->msr | MSR_TM);

	/* This loads the checkpointed FP/VEC state, if used */
	tm_recheckpoint(&tsk->thread);

	msr_check_and_set(msr & (MSR_FP | MSR_VEC));
	if (msr & MSR_FP) {
		load_fp_state(&tsk->thread.fp_state);
		regs_set_return_msr(regs, regs->msr | (MSR_FP | tsk->thread.fpexc_mode));
	}
	if (msr & MSR_VEC) {
		load_vr_state(&tsk->thread.vr_state);
		regs_set_return_msr(regs, regs->msr | MSR_VEC);
	}

	preempt_enable();

	return err;
}
#else /* !CONFIG_PPC_TRANSACTIONAL_MEM */
static long restore_tm_sigcontexts(struct task_struct *tsk, struct sigcontext __user *sc,
				   struct sigcontext __user *tm_sc)
{
	return -EINVAL;
}
#endif

/*
 * Setup the trampoline code on the stack
 */
static long setup_trampoline(unsigned int syscall, unsigned int __user *tramp)
{
	int i;
	long err = 0;

	/* Call the handler and pop the dummy stackframe*/
	err |= __put_user(PPC_RAW_BCTRL(), &tramp[0]);
	err |= __put_user(PPC_RAW_ADDI(_R1, _R1, __SIGNAL_FRAMESIZE), &tramp[1]);

	err |= __put_user(PPC_RAW_LI(_R0, syscall), &tramp[2]);
	err |= __put_user(PPC_RAW_SC(), &tramp[3]);

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
SYSCALL_DEFINE3(swapcontext, struct ucontext __user *, old_ctx,
		struct ucontext __user *, new_ctx, long, ctx_size)
{
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
		prepare_setup_sigcontext(current);
		if (!user_write_access_begin(old_ctx, ctx_size))
			return -EFAULT;

		unsafe_setup_sigcontext(&old_ctx->uc_mcontext, current, 0, NULL,
					0, ctx_has_vsx_region, efault_out);
		unsafe_copy_to_user(&old_ctx->uc_sigmask, &current->blocked,
				    sizeof(sigset_t), efault_out);

		user_write_access_end();
	}
	if (new_ctx == NULL)
		return 0;
	if (!access_ok(new_ctx, ctx_size) ||
	    fault_in_pages_readable((u8 __user *)new_ctx, ctx_size))
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

	if (__get_user_sigset(&set, &new_ctx->uc_sigmask))
		do_exit(SIGSEGV);
	set_current_blocked(&set);

	if (!user_read_access_begin(new_ctx, ctx_size))
		return -EFAULT;
	if (__unsafe_restore_sigcontext(current, NULL, 0, &new_ctx->uc_mcontext)) {
		user_read_access_end();
		do_exit(SIGSEGV);
	}
	user_read_access_end();

	/* This returns like rt_sigreturn */
	set_thread_flag(TIF_RESTOREALL);

	return 0;

efault_out:
	user_write_access_end();
	return -EFAULT;
}


/*
 * Do a signal return; undo the signal stack.
 */

SYSCALL_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = current_pt_regs();
	struct ucontext __user *uc = (struct ucontext __user *)regs->gpr[1];
	sigset_t set;
	unsigned long msr;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	if (!access_ok(uc, sizeof(*uc)))
		goto badframe;

	if (__get_user_sigset(&set, &uc->uc_sigmask))
		goto badframe;
	set_current_blocked(&set);

	if (IS_ENABLED(CONFIG_PPC_TRANSACTIONAL_MEM)) {
		/*
		 * If there is a transactional state then throw it away.
		 * The purpose of a sigreturn is to destroy all traces of the
		 * signal frame, this includes any transactional state created
		 * within in. We only check for suspended as we can never be
		 * active in the kernel, we are active, there is nothing better to
		 * do than go ahead and Bad Thing later.
		 * The cause is not important as there will never be a
		 * recheckpoint so it's not user visible.
		 */
		if (MSR_TM_SUSPENDED(mfmsr()))
			tm_reclaim_current(0);

		/*
		 * Disable MSR[TS] bit also, so, if there is an exception in the
		 * code below (as a page fault in copy_ckvsx_to_user()), it does
		 * not recheckpoint this task if there was a context switch inside
		 * the exception.
		 *
		 * A major page fault can indirectly call schedule(). A reschedule
		 * process in the middle of an exception can have a side effect
		 * (Changing the CPU MSR[TS] state), since schedule() is called
		 * with the CPU MSR[TS] disable and returns with MSR[TS]=Suspended
		 * (switch_to() calls tm_recheckpoint() for the 'new' process). In
		 * this case, the process continues to be the same in the CPU, but
		 * the CPU state just changed.
		 *
		 * This can cause a TM Bad Thing, since the MSR in the stack will
		 * have the MSR[TS]=0, and this is what will be used to RFID.
		 *
		 * Clearing MSR[TS] state here will avoid a recheckpoint if there
		 * is any process reschedule in kernel space. The MSR[TS] state
		 * does not need to be saved also, since it will be replaced with
		 * the MSR[TS] that came from user context later, at
		 * restore_tm_sigcontexts.
		 */
		regs_set_return_msr(regs, regs->msr & ~MSR_TS_MASK);

		if (__get_user(msr, &uc->uc_mcontext.gp_regs[PT_MSR]))
			goto badframe;
	}

	if (IS_ENABLED(CONFIG_PPC_TRANSACTIONAL_MEM) && MSR_TM_ACTIVE(msr)) {
		/* We recheckpoint on return. */
		struct ucontext __user *uc_transact;

		/* Trying to start TM on non TM system */
		if (!cpu_has_feature(CPU_FTR_TM))
			goto badframe;

		if (__get_user(uc_transact, &uc->uc_link))
			goto badframe;
		if (restore_tm_sigcontexts(current, &uc->uc_mcontext,
					   &uc_transact->uc_mcontext))
			goto badframe;
	} else {
		/*
		 * Fall through, for non-TM restore
		 *
		 * Unset MSR[TS] on the thread regs since MSR from user
		 * context does not have MSR active, and recheckpoint was
		 * not called since restore_tm_sigcontexts() was not called
		 * also.
		 *
		 * If not unsetting it, the code can RFID to userspace with
		 * MSR[TS] set, but without CPU in the proper state,
		 * causing a TM bad thing.
		 */
		regs_set_return_msr(current->thread.regs,
				current->thread.regs->msr & ~MSR_TS_MASK);
		if (!user_read_access_begin(&uc->uc_mcontext, sizeof(uc->uc_mcontext)))
			goto badframe;

		unsafe_restore_sigcontext(current, NULL, 1, &uc->uc_mcontext,
					  badframe_block);

		user_read_access_end();
	}

	if (restore_altstack(&uc->uc_stack))
		goto badframe;

	set_thread_flag(TIF_RESTOREALL);

	return 0;

badframe_block:
	user_read_access_end();
badframe:
	signal_fault(current, regs, "rt_sigreturn", uc);

	force_sig(SIGSEGV);
	return 0;
}

int handle_rt_signal64(struct ksignal *ksig, sigset_t *set,
		struct task_struct *tsk)
{
	struct rt_sigframe __user *frame;
	unsigned long newsp = 0;
	long err = 0;
	struct pt_regs *regs = tsk->thread.regs;
	/* Save the thread's msr before get_tm_stackpointer() changes it */
	unsigned long msr = regs->msr;

	frame = get_sigframe(ksig, tsk, sizeof(*frame), 0);

	/*
	 * This only applies when calling unsafe_setup_sigcontext() and must be
	 * called before opening the uaccess window.
	 */
	if (!MSR_TM_ACTIVE(msr))
		prepare_setup_sigcontext(tsk);

	if (!user_write_access_begin(frame, sizeof(*frame)))
		goto badframe;

	unsafe_put_user(&frame->info, &frame->pinfo, badframe_block);
	unsafe_put_user(&frame->uc, &frame->puc, badframe_block);

	/* Create the ucontext.  */
	unsafe_put_user(0, &frame->uc.uc_flags, badframe_block);
	unsafe_save_altstack(&frame->uc.uc_stack, regs->gpr[1], badframe_block);

	if (MSR_TM_ACTIVE(msr)) {
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
		/* The ucontext_t passed to userland points to the second
		 * ucontext_t (for transactional state) with its uc_link ptr.
		 */
		unsafe_put_user(&frame->uc_transact, &frame->uc.uc_link, badframe_block);

		user_write_access_end();

		err |= setup_tm_sigcontexts(&frame->uc.uc_mcontext,
					    &frame->uc_transact.uc_mcontext,
					    tsk, ksig->sig, NULL,
					    (unsigned long)ksig->ka.sa.sa_handler,
					    msr);

		if (!user_write_access_begin(&frame->uc.uc_sigmask,
					     sizeof(frame->uc.uc_sigmask)))
			goto badframe;

#endif
	} else {
		unsafe_put_user(0, &frame->uc.uc_link, badframe_block);
		unsafe_setup_sigcontext(&frame->uc.uc_mcontext, tsk, ksig->sig,
					NULL, (unsigned long)ksig->ka.sa.sa_handler,
					1, badframe_block);
	}

	unsafe_copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set), badframe_block);
	user_write_access_end();

	/* Save the siginfo outside of the unsafe block. */
	if (copy_siginfo_to_user(&frame->info, &ksig->info))
		goto badframe;

	/* Make sure signal handler doesn't get spurious FP exceptions */
	tsk->thread.fp_state.fpscr = 0;

	/* Set up to return from userspace. */
	if (tsk->mm->context.vdso) {
		regs_set_return_ip(regs, VDSO64_SYMBOL(tsk->mm->context.vdso, sigtramp_rt64));
	} else {
		err |= setup_trampoline(__NR_rt_sigreturn, &frame->tramp[0]);
		if (err)
			goto badframe;
		regs_set_return_ip(regs, (unsigned long) &frame->tramp[0]);
	}

	/* Allocate a dummy caller frame for the signal handler. */
	newsp = ((unsigned long)frame) - __SIGNAL_FRAMESIZE;
	err |= put_user(regs->gpr[1], (unsigned long __user *)newsp);

	/* Set up "regs" so we "return" to the signal handler. */
	if (is_elf2_task()) {
		regs->ctr = (unsigned long) ksig->ka.sa.sa_handler;
		regs->gpr[12] = regs->ctr;
	} else {
		/* Handler is *really* a pointer to the function descriptor for
		 * the signal routine.  The first entry in the function
		 * descriptor is the entry address of signal and the second
		 * entry is the TOC value we need to use.
		 */
		func_descr_t __user *funct_desc_ptr =
			(func_descr_t __user *) ksig->ka.sa.sa_handler;

		err |= get_user(regs->ctr, &funct_desc_ptr->entry);
		err |= get_user(regs->gpr[2], &funct_desc_ptr->toc);
	}

	/* enter the signal handler in native-endian mode */
	regs_set_return_msr(regs, (regs->msr & ~MSR_LE) | (MSR_KERNEL & MSR_LE));
	regs->gpr[1] = newsp;
	regs->gpr[3] = ksig->sig;
	regs->result = 0;
	if (ksig->ka.sa.sa_flags & SA_SIGINFO) {
		regs->gpr[4] = (unsigned long)&frame->info;
		regs->gpr[5] = (unsigned long)&frame->uc;
		regs->gpr[6] = (unsigned long) frame;
	} else {
		regs->gpr[4] = (unsigned long)&frame->uc.uc_mcontext;
	}
	if (err)
		goto badframe;

	return 0;

badframe_block:
	user_write_access_end();
badframe:
	signal_fault(current, regs, "handle_rt_signal64", frame);

	return 1;
}
