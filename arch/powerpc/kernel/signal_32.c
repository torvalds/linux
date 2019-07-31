/*
 * Signal handling for 32bit PPC and 32bit tasks on 64bit PPC
 *
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 * Copyright (C) 2001 IBM
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
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
#include <linux/elf.h>
#include <linux/ptrace.h>
#include <linux/pagemap.h>
#include <linux/ratelimit.h>
#include <linux/syscalls.h>
#ifdef CONFIG_PPC64
#include <linux/compat.h>
#else
#include <linux/wait.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/tty.h>
#include <linux/binfmts.h>
#endif

#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/syscalls.h>
#include <asm/sigcontext.h>
#include <asm/vdso.h>
#include <asm/switch_to.h>
#include <asm/tm.h>
#include <asm/asm-prototypes.h>
#ifdef CONFIG_PPC64
#include "ppc32.h"
#include <asm/unistd.h>
#else
#include <asm/ucontext.h>
#include <asm/pgtable.h>
#endif

#include "signal.h"


#ifdef CONFIG_PPC64
#define old_sigaction	old_sigaction32
#define sigcontext	sigcontext32
#define mcontext	mcontext32
#define ucontext	ucontext32

#define __save_altstack __compat_save_altstack

/*
 * Userspace code may pass a ucontext which doesn't include VSX added
 * at the end.  We need to check for this case.
 */
#define UCONTEXTSIZEWITHOUTVSX \
		(sizeof(struct ucontext) - sizeof(elf_vsrreghalf_t32))

/*
 * Returning 0 means we return to userspace via
 * ret_from_except and thus restore all user
 * registers from *regs.  This is what we need
 * to do when a signal has been delivered.
 */

#define GP_REGS_SIZE	min(sizeof(elf_gregset_t32), sizeof(struct pt_regs32))
#undef __SIGNAL_FRAMESIZE
#define __SIGNAL_FRAMESIZE	__SIGNAL_FRAMESIZE32
#undef ELF_NVRREG
#define ELF_NVRREG	ELF_NVRREG32

/*
 * Functions for flipping sigsets (thanks to brain dead generic
 * implementation that makes things simple for little endian only)
 */
static inline int put_sigset_t(compat_sigset_t __user *uset, sigset_t *set)
{
	return put_compat_sigset(uset, set, sizeof(*uset));
}

static inline int get_sigset_t(sigset_t *set,
			       const compat_sigset_t __user *uset)
{
	return get_compat_sigset(set, uset);
}

#define to_user_ptr(p)		ptr_to_compat(p)
#define from_user_ptr(p)	compat_ptr(p)

static inline int save_general_regs(struct pt_regs *regs,
		struct mcontext __user *frame)
{
	elf_greg_t64 *gregs = (elf_greg_t64 *)regs;
	int i;
	/* Force usr to alway see softe as 1 (interrupts enabled) */
	elf_greg_t64 softe = 0x1;

	WARN_ON(!FULL_REGS(regs));

	for (i = 0; i <= PT_RESULT; i ++) {
		if (i == 14 && !FULL_REGS(regs))
			i = 32;
		if ( i == PT_SOFTE) {
			if(__put_user((unsigned int)softe, &frame->mc_gregs[i]))
				return -EFAULT;
			else
				continue;
		}
		if (__put_user((unsigned int)gregs[i], &frame->mc_gregs[i]))
			return -EFAULT;
	}
	return 0;
}

static inline int restore_general_regs(struct pt_regs *regs,
		struct mcontext __user *sr)
{
	elf_greg_t64 *gregs = (elf_greg_t64 *)regs;
	int i;

	for (i = 0; i <= PT_RESULT; i++) {
		if ((i == PT_MSR) || (i == PT_SOFTE))
			continue;
		if (__get_user(gregs[i], &sr->mc_gregs[i]))
			return -EFAULT;
	}
	return 0;
}

#else /* CONFIG_PPC64 */

#define GP_REGS_SIZE	min(sizeof(elf_gregset_t), sizeof(struct pt_regs))

static inline int put_sigset_t(sigset_t __user *uset, sigset_t *set)
{
	return copy_to_user(uset, set, sizeof(*uset));
}

static inline int get_sigset_t(sigset_t *set, const sigset_t __user *uset)
{
	return copy_from_user(set, uset, sizeof(*uset));
}

#define to_user_ptr(p)		((unsigned long)(p))
#define from_user_ptr(p)	((void __user *)(p))

static inline int save_general_regs(struct pt_regs *regs,
		struct mcontext __user *frame)
{
	WARN_ON(!FULL_REGS(regs));
	return __copy_to_user(&frame->mc_gregs, regs, GP_REGS_SIZE);
}

static inline int restore_general_regs(struct pt_regs *regs,
		struct mcontext __user *sr)
{
	/* copy up to but not including MSR */
	if (__copy_from_user(regs, &sr->mc_gregs,
				PT_MSR * sizeof(elf_greg_t)))
		return -EFAULT;
	/* copy from orig_r3 (the word after the MSR) up to the end */
	if (__copy_from_user(&regs->orig_gpr3, &sr->mc_gregs[PT_ORIG_R3],
				GP_REGS_SIZE - PT_ORIG_R3 * sizeof(elf_greg_t)))
		return -EFAULT;
	return 0;
}
#endif

/*
 * When we have signals to deliver, we set up on the
 * user stack, going down from the original stack pointer:
 *	an ABI gap of 56 words
 *	an mcontext struct
 *	a sigcontext struct
 *	a gap of __SIGNAL_FRAMESIZE bytes
 *
 * Each of these things must be a multiple of 16 bytes in size. The following
 * structure represent all of this except the __SIGNAL_FRAMESIZE gap
 *
 */
struct sigframe {
	struct sigcontext sctx;		/* the sigcontext */
	struct mcontext	mctx;		/* all the register values */
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	struct sigcontext sctx_transact;
	struct mcontext	mctx_transact;
#endif
	/*
	 * Programs using the rs6000/xcoff abi can save up to 19 gp
	 * regs and 18 fp regs below sp before decrementing it.
	 */
	int			abigap[56];
};

/* We use the mc_pad field for the signal return trampoline. */
#define tramp	mc_pad

/*
 *  When we have rt signals to deliver, we set up on the
 *  user stack, going down from the original stack pointer:
 *	one rt_sigframe struct (siginfo + ucontext + ABI gap)
 *	a gap of __SIGNAL_FRAMESIZE+16 bytes
 *  (the +16 is to get the siginfo and ucontext in the same
 *  positions as in older kernels).
 *
 *  Each of these things must be a multiple of 16 bytes in size.
 *
 */
struct rt_sigframe {
#ifdef CONFIG_PPC64
	compat_siginfo_t info;
#else
	struct siginfo info;
#endif
	struct ucontext	uc;
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	struct ucontext	uc_transact;
#endif
	/*
	 * Programs using the rs6000/xcoff abi can save up to 19 gp
	 * regs and 18 fp regs below sp before decrementing it.
	 */
	int			abigap[56];
};

#ifdef CONFIG_VSX
unsigned long copy_fpr_to_user(void __user *to,
			       struct task_struct *task)
{
	u64 buf[ELF_NFPREG];
	int i;

	/* save FPR copy to local buffer then write to the thread_struct */
	for (i = 0; i < (ELF_NFPREG - 1) ; i++)
		buf[i] = task->thread.TS_FPR(i);
	buf[i] = task->thread.fp_state.fpscr;
	return __copy_to_user(to, buf, ELF_NFPREG * sizeof(double));
}

unsigned long copy_fpr_from_user(struct task_struct *task,
				 void __user *from)
{
	u64 buf[ELF_NFPREG];
	int i;

	if (__copy_from_user(buf, from, ELF_NFPREG * sizeof(double)))
		return 1;
	for (i = 0; i < (ELF_NFPREG - 1) ; i++)
		task->thread.TS_FPR(i) = buf[i];
	task->thread.fp_state.fpscr = buf[i];

	return 0;
}

unsigned long copy_vsx_to_user(void __user *to,
			       struct task_struct *task)
{
	u64 buf[ELF_NVSRHALFREG];
	int i;

	/* save FPR copy to local buffer then write to the thread_struct */
	for (i = 0; i < ELF_NVSRHALFREG; i++)
		buf[i] = task->thread.fp_state.fpr[i][TS_VSRLOWOFFSET];
	return __copy_to_user(to, buf, ELF_NVSRHALFREG * sizeof(double));
}

unsigned long copy_vsx_from_user(struct task_struct *task,
				 void __user *from)
{
	u64 buf[ELF_NVSRHALFREG];
	int i;

	if (__copy_from_user(buf, from, ELF_NVSRHALFREG * sizeof(double)))
		return 1;
	for (i = 0; i < ELF_NVSRHALFREG ; i++)
		task->thread.fp_state.fpr[i][TS_VSRLOWOFFSET] = buf[i];
	return 0;
}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
unsigned long copy_ckfpr_to_user(void __user *to,
				  struct task_struct *task)
{
	u64 buf[ELF_NFPREG];
	int i;

	/* save FPR copy to local buffer then write to the thread_struct */
	for (i = 0; i < (ELF_NFPREG - 1) ; i++)
		buf[i] = task->thread.TS_CKFPR(i);
	buf[i] = task->thread.ckfp_state.fpscr;
	return __copy_to_user(to, buf, ELF_NFPREG * sizeof(double));
}

unsigned long copy_ckfpr_from_user(struct task_struct *task,
					  void __user *from)
{
	u64 buf[ELF_NFPREG];
	int i;

	if (__copy_from_user(buf, from, ELF_NFPREG * sizeof(double)))
		return 1;
	for (i = 0; i < (ELF_NFPREG - 1) ; i++)
		task->thread.TS_CKFPR(i) = buf[i];
	task->thread.ckfp_state.fpscr = buf[i];

	return 0;
}

unsigned long copy_ckvsx_to_user(void __user *to,
				  struct task_struct *task)
{
	u64 buf[ELF_NVSRHALFREG];
	int i;

	/* save FPR copy to local buffer then write to the thread_struct */
	for (i = 0; i < ELF_NVSRHALFREG; i++)
		buf[i] = task->thread.ckfp_state.fpr[i][TS_VSRLOWOFFSET];
	return __copy_to_user(to, buf, ELF_NVSRHALFREG * sizeof(double));
}

unsigned long copy_ckvsx_from_user(struct task_struct *task,
					  void __user *from)
{
	u64 buf[ELF_NVSRHALFREG];
	int i;

	if (__copy_from_user(buf, from, ELF_NVSRHALFREG * sizeof(double)))
		return 1;
	for (i = 0; i < ELF_NVSRHALFREG ; i++)
		task->thread.ckfp_state.fpr[i][TS_VSRLOWOFFSET] = buf[i];
	return 0;
}
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */
#else
inline unsigned long copy_fpr_to_user(void __user *to,
				      struct task_struct *task)
{
	return __copy_to_user(to, task->thread.fp_state.fpr,
			      ELF_NFPREG * sizeof(double));
}

inline unsigned long copy_fpr_from_user(struct task_struct *task,
					void __user *from)
{
	return __copy_from_user(task->thread.fp_state.fpr, from,
			      ELF_NFPREG * sizeof(double));
}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
inline unsigned long copy_ckfpr_to_user(void __user *to,
					 struct task_struct *task)
{
	return __copy_to_user(to, task->thread.ckfp_state.fpr,
			      ELF_NFPREG * sizeof(double));
}

inline unsigned long copy_ckfpr_from_user(struct task_struct *task,
						 void __user *from)
{
	return __copy_from_user(task->thread.ckfp_state.fpr, from,
				ELF_NFPREG * sizeof(double));
}
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */
#endif

/*
 * Save the current user registers on the user stack.
 * We only save the altivec/spe registers if the process has used
 * altivec/spe instructions at some point.
 */
static int save_user_regs(struct pt_regs *regs, struct mcontext __user *frame,
			  struct mcontext __user *tm_frame, int sigret,
			  int ctx_has_vsx_region)
{
	unsigned long msr = regs->msr;

	/* Make sure floating point registers are stored in regs */
	flush_fp_to_thread(current);

	/* save general registers */
	if (save_general_regs(regs, frame))
		return 1;

#ifdef CONFIG_ALTIVEC
	/* save altivec registers */
	if (current->thread.used_vr) {
		flush_altivec_to_thread(current);
		if (__copy_to_user(&frame->mc_vregs, &current->thread.vr_state,
				   ELF_NVRREG * sizeof(vector128)))
			return 1;
		/* set MSR_VEC in the saved MSR value to indicate that
		   frame->mc_vregs contains valid data */
		msr |= MSR_VEC;
	}
	/* else assert((regs->msr & MSR_VEC) == 0) */

	/* We always copy to/from vrsave, it's 0 if we don't have or don't
	 * use altivec. Since VSCR only contains 32 bits saved in the least
	 * significant bits of a vector, we "cheat" and stuff VRSAVE in the
	 * most significant bits of that same vector. --BenH
	 * Note that the current VRSAVE value is in the SPR at this point.
	 */
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		current->thread.vrsave = mfspr(SPRN_VRSAVE);
	if (__put_user(current->thread.vrsave, (u32 __user *)&frame->mc_vregs[32]))
		return 1;
#endif /* CONFIG_ALTIVEC */
	if (copy_fpr_to_user(&frame->mc_fregs, current))
		return 1;

	/*
	 * Clear the MSR VSX bit to indicate there is no valid state attached
	 * to this context, except in the specific case below where we set it.
	 */
	msr &= ~MSR_VSX;
#ifdef CONFIG_VSX
	/*
	 * Copy VSR 0-31 upper half from thread_struct to local
	 * buffer, then write that to userspace.  Also set MSR_VSX in
	 * the saved MSR value to indicate that frame->mc_vregs
	 * contains valid data
	 */
	if (current->thread.used_vsr && ctx_has_vsx_region) {
		flush_vsx_to_thread(current);
		if (copy_vsx_to_user(&frame->mc_vsregs, current))
			return 1;
		msr |= MSR_VSX;
	}
#endif /* CONFIG_VSX */
#ifdef CONFIG_SPE
	/* save spe registers */
	if (current->thread.used_spe) {
		flush_spe_to_thread(current);
		if (__copy_to_user(&frame->mc_vregs, current->thread.evr,
				   ELF_NEVRREG * sizeof(u32)))
			return 1;
		/* set MSR_SPE in the saved MSR value to indicate that
		   frame->mc_vregs contains valid data */
		msr |= MSR_SPE;
	}
	/* else assert((regs->msr & MSR_SPE) == 0) */

	/* We always copy to/from spefscr */
	if (__put_user(current->thread.spefscr, (u32 __user *)&frame->mc_vregs + ELF_NEVRREG))
		return 1;
#endif /* CONFIG_SPE */

	if (__put_user(msr, &frame->mc_gregs[PT_MSR]))
		return 1;
	/* We need to write 0 the MSR top 32 bits in the tm frame so that we
	 * can check it on the restore to see if TM is active
	 */
	if (tm_frame && __put_user(0, &tm_frame->mc_gregs[PT_MSR]))
		return 1;

	if (sigret) {
		/* Set up the sigreturn trampoline: li r0,sigret; sc */
		if (__put_user(0x38000000UL + sigret, &frame->tramp[0])
		    || __put_user(0x44000002UL, &frame->tramp[1]))
			return 1;
		flush_icache_range((unsigned long) &frame->tramp[0],
				   (unsigned long) &frame->tramp[2]);
	}

	return 0;
}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
/*
 * Save the current user registers on the user stack.
 * We only save the altivec/spe registers if the process has used
 * altivec/spe instructions at some point.
 * We also save the transactional registers to a second ucontext in the
 * frame.
 *
 * See save_user_regs() and signal_64.c:setup_tm_sigcontexts().
 */
static int save_tm_user_regs(struct pt_regs *regs,
			     struct mcontext __user *frame,
			     struct mcontext __user *tm_frame, int sigret)
{
	unsigned long msr = regs->msr;

	WARN_ON(tm_suspend_disabled);

	/* Remove TM bits from thread's MSR.  The MSR in the sigcontext
	 * just indicates to userland that we were doing a transaction, but we
	 * don't want to return in transactional state.  This also ensures
	 * that flush_fp_to_thread won't set TIF_RESTORE_TM again.
	 */
	regs->msr &= ~MSR_TS_MASK;

	/* Save both sets of general registers */
	if (save_general_regs(&current->thread.ckpt_regs, frame)
	    || save_general_regs(regs, tm_frame))
		return 1;

	/* Stash the top half of the 64bit MSR into the 32bit MSR word
	 * of the transactional mcontext.  This way we have a backward-compatible
	 * MSR in the 'normal' (checkpointed) mcontext and additionally one can
	 * also look at what type of transaction (T or S) was active at the
	 * time of the signal.
	 */
	if (__put_user((msr >> 32), &tm_frame->mc_gregs[PT_MSR]))
		return 1;

#ifdef CONFIG_ALTIVEC
	/* save altivec registers */
	if (current->thread.used_vr) {
		if (__copy_to_user(&frame->mc_vregs, &current->thread.ckvr_state,
				   ELF_NVRREG * sizeof(vector128)))
			return 1;
		if (msr & MSR_VEC) {
			if (__copy_to_user(&tm_frame->mc_vregs,
					   &current->thread.vr_state,
					   ELF_NVRREG * sizeof(vector128)))
				return 1;
		} else {
			if (__copy_to_user(&tm_frame->mc_vregs,
					   &current->thread.ckvr_state,
					   ELF_NVRREG * sizeof(vector128)))
				return 1;
		}

		/* set MSR_VEC in the saved MSR value to indicate that
		 * frame->mc_vregs contains valid data
		 */
		msr |= MSR_VEC;
	}

	/* We always copy to/from vrsave, it's 0 if we don't have or don't
	 * use altivec. Since VSCR only contains 32 bits saved in the least
	 * significant bits of a vector, we "cheat" and stuff VRSAVE in the
	 * most significant bits of that same vector. --BenH
	 */
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		current->thread.ckvrsave = mfspr(SPRN_VRSAVE);
	if (__put_user(current->thread.ckvrsave,
		       (u32 __user *)&frame->mc_vregs[32]))
		return 1;
	if (msr & MSR_VEC) {
		if (__put_user(current->thread.vrsave,
			       (u32 __user *)&tm_frame->mc_vregs[32]))
			return 1;
	} else {
		if (__put_user(current->thread.ckvrsave,
			       (u32 __user *)&tm_frame->mc_vregs[32]))
			return 1;
	}
#endif /* CONFIG_ALTIVEC */

	if (copy_ckfpr_to_user(&frame->mc_fregs, current))
		return 1;
	if (msr & MSR_FP) {
		if (copy_fpr_to_user(&tm_frame->mc_fregs, current))
			return 1;
	} else {
		if (copy_ckfpr_to_user(&tm_frame->mc_fregs, current))
			return 1;
	}

#ifdef CONFIG_VSX
	/*
	 * Copy VSR 0-31 upper half from thread_struct to local
	 * buffer, then write that to userspace.  Also set MSR_VSX in
	 * the saved MSR value to indicate that frame->mc_vregs
	 * contains valid data
	 */
	if (current->thread.used_vsr) {
		if (copy_ckvsx_to_user(&frame->mc_vsregs, current))
			return 1;
		if (msr & MSR_VSX) {
			if (copy_vsx_to_user(&tm_frame->mc_vsregs,
						      current))
				return 1;
		} else {
			if (copy_ckvsx_to_user(&tm_frame->mc_vsregs, current))
				return 1;
		}

		msr |= MSR_VSX;
	}
#endif /* CONFIG_VSX */
#ifdef CONFIG_SPE
	/* SPE regs are not checkpointed with TM, so this section is
	 * simply the same as in save_user_regs().
	 */
	if (current->thread.used_spe) {
		flush_spe_to_thread(current);
		if (__copy_to_user(&frame->mc_vregs, current->thread.evr,
				   ELF_NEVRREG * sizeof(u32)))
			return 1;
		/* set MSR_SPE in the saved MSR value to indicate that
		 * frame->mc_vregs contains valid data */
		msr |= MSR_SPE;
	}

	/* We always copy to/from spefscr */
	if (__put_user(current->thread.spefscr, (u32 __user *)&frame->mc_vregs + ELF_NEVRREG))
		return 1;
#endif /* CONFIG_SPE */

	if (__put_user(msr, &frame->mc_gregs[PT_MSR]))
		return 1;
	if (sigret) {
		/* Set up the sigreturn trampoline: li r0,sigret; sc */
		if (__put_user(0x38000000UL + sigret, &frame->tramp[0])
		    || __put_user(0x44000002UL, &frame->tramp[1]))
			return 1;
		flush_icache_range((unsigned long) &frame->tramp[0],
				   (unsigned long) &frame->tramp[2]);
	}

	return 0;
}
#endif

/*
 * Restore the current user register values from the user stack,
 * (except for MSR).
 */
static long restore_user_regs(struct pt_regs *regs,
			      struct mcontext __user *sr, int sig)
{
	long err;
	unsigned int save_r2 = 0;
	unsigned long msr;
#ifdef CONFIG_VSX
	int i;
#endif

	/*
	 * restore general registers but not including MSR or SOFTE. Also
	 * take care of keeping r2 (TLS) intact if not a signal
	 */
	if (!sig)
		save_r2 = (unsigned int)regs->gpr[2];
	err = restore_general_regs(regs, sr);
	regs->trap = 0;
	err |= __get_user(msr, &sr->mc_gregs[PT_MSR]);
	if (!sig)
		regs->gpr[2] = (unsigned long) save_r2;
	if (err)
		return 1;

	/* if doing signal return, restore the previous little-endian mode */
	if (sig)
		regs->msr = (regs->msr & ~MSR_LE) | (msr & MSR_LE);

#ifdef CONFIG_ALTIVEC
	/*
	 * Force the process to reload the altivec registers from
	 * current->thread when it next does altivec instructions
	 */
	regs->msr &= ~MSR_VEC;
	if (msr & MSR_VEC) {
		/* restore altivec registers from the stack */
		if (__copy_from_user(&current->thread.vr_state, &sr->mc_vregs,
				     sizeof(sr->mc_vregs)))
			return 1;
		current->thread.used_vr = true;
	} else if (current->thread.used_vr)
		memset(&current->thread.vr_state, 0,
		       ELF_NVRREG * sizeof(vector128));

	/* Always get VRSAVE back */
	if (__get_user(current->thread.vrsave, (u32 __user *)&sr->mc_vregs[32]))
		return 1;
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		mtspr(SPRN_VRSAVE, current->thread.vrsave);
#endif /* CONFIG_ALTIVEC */
	if (copy_fpr_from_user(current, &sr->mc_fregs))
		return 1;

#ifdef CONFIG_VSX
	/*
	 * Force the process to reload the VSX registers from
	 * current->thread when it next does VSX instruction.
	 */
	regs->msr &= ~MSR_VSX;
	if (msr & MSR_VSX) {
		/*
		 * Restore altivec registers from the stack to a local
		 * buffer, then write this out to the thread_struct
		 */
		if (copy_vsx_from_user(current, &sr->mc_vsregs))
			return 1;
		current->thread.used_vsr = true;
	} else if (current->thread.used_vsr)
		for (i = 0; i < 32 ; i++)
			current->thread.fp_state.fpr[i][TS_VSRLOWOFFSET] = 0;
#endif /* CONFIG_VSX */
	/*
	 * force the process to reload the FP registers from
	 * current->thread when it next does FP instructions
	 */
	regs->msr &= ~(MSR_FP | MSR_FE0 | MSR_FE1);

#ifdef CONFIG_SPE
	/* force the process to reload the spe registers from
	   current->thread when it next does spe instructions */
	regs->msr &= ~MSR_SPE;
	if (msr & MSR_SPE) {
		/* restore spe registers from the stack */
		if (__copy_from_user(current->thread.evr, &sr->mc_vregs,
				     ELF_NEVRREG * sizeof(u32)))
			return 1;
		current->thread.used_spe = true;
	} else if (current->thread.used_spe)
		memset(current->thread.evr, 0, ELF_NEVRREG * sizeof(u32));

	/* Always get SPEFSCR back */
	if (__get_user(current->thread.spefscr, (u32 __user *)&sr->mc_vregs + ELF_NEVRREG))
		return 1;
#endif /* CONFIG_SPE */

	return 0;
}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
/*
 * Restore the current user register values from the user stack, except for
 * MSR, and recheckpoint the original checkpointed register state for processes
 * in transactions.
 */
static long restore_tm_user_regs(struct pt_regs *regs,
				 struct mcontext __user *sr,
				 struct mcontext __user *tm_sr)
{
	long err;
	unsigned long msr, msr_hi;
#ifdef CONFIG_VSX
	int i;
#endif

	if (tm_suspend_disabled)
		return 1;
	/*
	 * restore general registers but not including MSR or SOFTE. Also
	 * take care of keeping r2 (TLS) intact if not a signal.
	 * See comment in signal_64.c:restore_tm_sigcontexts();
	 * TFHAR is restored from the checkpointed NIP; TEXASR and TFIAR
	 * were set by the signal delivery.
	 */
	err = restore_general_regs(regs, tm_sr);
	err |= restore_general_regs(&current->thread.ckpt_regs, sr);

	err |= __get_user(current->thread.tm_tfhar, &sr->mc_gregs[PT_NIP]);

	err |= __get_user(msr, &sr->mc_gregs[PT_MSR]);
	if (err)
		return 1;

	/* Restore the previous little-endian mode */
	regs->msr = (regs->msr & ~MSR_LE) | (msr & MSR_LE);

#ifdef CONFIG_ALTIVEC
	regs->msr &= ~MSR_VEC;
	if (msr & MSR_VEC) {
		/* restore altivec registers from the stack */
		if (__copy_from_user(&current->thread.ckvr_state, &sr->mc_vregs,
				     sizeof(sr->mc_vregs)) ||
		    __copy_from_user(&current->thread.vr_state,
				     &tm_sr->mc_vregs,
				     sizeof(sr->mc_vregs)))
			return 1;
		current->thread.used_vr = true;
	} else if (current->thread.used_vr) {
		memset(&current->thread.vr_state, 0,
		       ELF_NVRREG * sizeof(vector128));
		memset(&current->thread.ckvr_state, 0,
		       ELF_NVRREG * sizeof(vector128));
	}

	/* Always get VRSAVE back */
	if (__get_user(current->thread.ckvrsave,
		       (u32 __user *)&sr->mc_vregs[32]) ||
	    __get_user(current->thread.vrsave,
		       (u32 __user *)&tm_sr->mc_vregs[32]))
		return 1;
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		mtspr(SPRN_VRSAVE, current->thread.ckvrsave);
#endif /* CONFIG_ALTIVEC */

	regs->msr &= ~(MSR_FP | MSR_FE0 | MSR_FE1);

	if (copy_fpr_from_user(current, &sr->mc_fregs) ||
	    copy_ckfpr_from_user(current, &tm_sr->mc_fregs))
		return 1;

#ifdef CONFIG_VSX
	regs->msr &= ~MSR_VSX;
	if (msr & MSR_VSX) {
		/*
		 * Restore altivec registers from the stack to a local
		 * buffer, then write this out to the thread_struct
		 */
		if (copy_vsx_from_user(current, &tm_sr->mc_vsregs) ||
		    copy_ckvsx_from_user(current, &sr->mc_vsregs))
			return 1;
		current->thread.used_vsr = true;
	} else if (current->thread.used_vsr)
		for (i = 0; i < 32 ; i++) {
			current->thread.fp_state.fpr[i][TS_VSRLOWOFFSET] = 0;
			current->thread.ckfp_state.fpr[i][TS_VSRLOWOFFSET] = 0;
		}
#endif /* CONFIG_VSX */

#ifdef CONFIG_SPE
	/* SPE regs are not checkpointed with TM, so this section is
	 * simply the same as in restore_user_regs().
	 */
	regs->msr &= ~MSR_SPE;
	if (msr & MSR_SPE) {
		if (__copy_from_user(current->thread.evr, &sr->mc_vregs,
				     ELF_NEVRREG * sizeof(u32)))
			return 1;
		current->thread.used_spe = true;
	} else if (current->thread.used_spe)
		memset(current->thread.evr, 0, ELF_NEVRREG * sizeof(u32));

	/* Always get SPEFSCR back */
	if (__get_user(current->thread.spefscr, (u32 __user *)&sr->mc_vregs
		       + ELF_NEVRREG))
		return 1;
#endif /* CONFIG_SPE */

	/* Get the top half of the MSR from the user context */
	if (__get_user(msr_hi, &tm_sr->mc_gregs[PT_MSR]))
		return 1;
	msr_hi <<= 32;
	/* If TM bits are set to the reserved value, it's an invalid context */
	if (MSR_TM_RESV(msr_hi))
		return 1;

	/*
	 * Disabling preemption, since it is unsafe to be preempted
	 * with MSR[TS] set without recheckpointing.
	 */
	preempt_disable();

	/*
	 * CAUTION:
	 * After regs->MSR[TS] being updated, make sure that get_user(),
	 * put_user() or similar functions are *not* called. These
	 * functions can generate page faults which will cause the process
	 * to be de-scheduled with MSR[TS] set but without calling
	 * tm_recheckpoint(). This can cause a bug.
	 *
	 * Pull in the MSR TM bits from the user context
	 */
	regs->msr = (regs->msr & ~MSR_TS_MASK) | (msr_hi & MSR_TS_MASK);
	/* Now, recheckpoint.  This loads up all of the checkpointed (older)
	 * registers, including FP and V[S]Rs.  After recheckpointing, the
	 * transactional versions should be loaded.
	 */
	tm_enable();
	/* Make sure the transaction is marked as failed */
	current->thread.tm_texasr |= TEXASR_FS;
	/* This loads the checkpointed FP/VEC state, if used */
	tm_recheckpoint(&current->thread);

	/* This loads the speculative FP/VEC state, if used */
	msr_check_and_set(msr & (MSR_FP | MSR_VEC));
	if (msr & MSR_FP) {
		load_fp_state(&current->thread.fp_state);
		regs->msr |= (MSR_FP | current->thread.fpexc_mode);
	}
#ifdef CONFIG_ALTIVEC
	if (msr & MSR_VEC) {
		load_vr_state(&current->thread.vr_state);
		regs->msr |= MSR_VEC;
	}
#endif

	preempt_enable();

	return 0;
}
#endif

#ifdef CONFIG_PPC64

#define copy_siginfo_to_user	copy_siginfo_to_user32

#endif /* CONFIG_PPC64 */

/*
 * Set up a signal frame for a "real-time" signal handler
 * (one which gets siginfo).
 */
int handle_rt_signal32(struct ksignal *ksig, sigset_t *oldset,
		       struct task_struct *tsk)
{
	struct rt_sigframe __user *rt_sf;
	struct mcontext __user *frame;
	struct mcontext __user *tm_frame = NULL;
	void __user *addr;
	unsigned long newsp = 0;
	int sigret;
	unsigned long tramp;
	struct pt_regs *regs = tsk->thread.regs;

	BUG_ON(tsk != current);

	/* Set up Signal Frame */
	/* Put a Real Time Context onto stack */
	rt_sf = get_sigframe(ksig, get_tm_stackpointer(tsk), sizeof(*rt_sf), 1);
	addr = rt_sf;
	if (unlikely(rt_sf == NULL))
		goto badframe;

	/* Put the siginfo & fill in most of the ucontext */
	if (copy_siginfo_to_user(&rt_sf->info, &ksig->info)
	    || __put_user(0, &rt_sf->uc.uc_flags)
	    || __save_altstack(&rt_sf->uc.uc_stack, regs->gpr[1])
	    || __put_user(to_user_ptr(&rt_sf->uc.uc_mcontext),
		    &rt_sf->uc.uc_regs)
	    || put_sigset_t(&rt_sf->uc.uc_sigmask, oldset))
		goto badframe;

	/* Save user registers on the stack */
	frame = &rt_sf->uc.uc_mcontext;
	addr = frame;
	if (vdso32_rt_sigtramp && tsk->mm->context.vdso_base) {
		sigret = 0;
		tramp = tsk->mm->context.vdso_base + vdso32_rt_sigtramp;
	} else {
		sigret = __NR_rt_sigreturn;
		tramp = (unsigned long) frame->tramp;
	}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	tm_frame = &rt_sf->uc_transact.uc_mcontext;
	if (MSR_TM_ACTIVE(regs->msr)) {
		if (__put_user((unsigned long)&rt_sf->uc_transact,
			       &rt_sf->uc.uc_link) ||
		    __put_user((unsigned long)tm_frame,
			       &rt_sf->uc_transact.uc_regs))
			goto badframe;
		if (save_tm_user_regs(regs, frame, tm_frame, sigret))
			goto badframe;
	}
	else
#endif
	{
		if (__put_user(0, &rt_sf->uc.uc_link))
			goto badframe;
		if (save_user_regs(regs, frame, tm_frame, sigret, 1))
			goto badframe;
	}
	regs->link = tramp;

	tsk->thread.fp_state.fpscr = 0;	/* turn off all fp exceptions */

	/* create a stack frame for the caller of the handler */
	newsp = ((unsigned long)rt_sf) - (__SIGNAL_FRAMESIZE + 16);
	addr = (void __user *)regs->gpr[1];
	if (put_user(regs->gpr[1], (u32 __user *)newsp))
		goto badframe;

	/* Fill registers for signal handler */
	regs->gpr[1] = newsp;
	regs->gpr[3] = ksig->sig;
	regs->gpr[4] = (unsigned long) &rt_sf->info;
	regs->gpr[5] = (unsigned long) &rt_sf->uc;
	regs->gpr[6] = (unsigned long) rt_sf;
	regs->nip = (unsigned long) ksig->ka.sa.sa_handler;
	/* enter the signal handler in native-endian mode */
	regs->msr &= ~MSR_LE;
	regs->msr |= (MSR_KERNEL & MSR_LE);
	return 0;

badframe:
	if (show_unhandled_signals)
		printk_ratelimited(KERN_INFO
				   "%s[%d]: bad frame in handle_rt_signal32: "
				   "%p nip %08lx lr %08lx\n",
				   tsk->comm, tsk->pid,
				   addr, regs->nip, regs->link);

	return 1;
}

static int do_setcontext(struct ucontext __user *ucp, struct pt_regs *regs, int sig)
{
	sigset_t set;
	struct mcontext __user *mcp;

	if (get_sigset_t(&set, &ucp->uc_sigmask))
		return -EFAULT;
#ifdef CONFIG_PPC64
	{
		u32 cmcp;

		if (__get_user(cmcp, &ucp->uc_regs))
			return -EFAULT;
		mcp = (struct mcontext __user *)(u64)cmcp;
		/* no need to check access_ok(mcp), since mcp < 4GB */
	}
#else
	if (__get_user(mcp, &ucp->uc_regs))
		return -EFAULT;
	if (!access_ok(VERIFY_READ, mcp, sizeof(*mcp)))
		return -EFAULT;
#endif
	set_current_blocked(&set);
	if (restore_user_regs(regs, mcp, sig))
		return -EFAULT;

	return 0;
}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
static int do_setcontext_tm(struct ucontext __user *ucp,
			    struct ucontext __user *tm_ucp,
			    struct pt_regs *regs)
{
	sigset_t set;
	struct mcontext __user *mcp;
	struct mcontext __user *tm_mcp;
	u32 cmcp;
	u32 tm_cmcp;

	if (get_sigset_t(&set, &ucp->uc_sigmask))
		return -EFAULT;

	if (__get_user(cmcp, &ucp->uc_regs) ||
	    __get_user(tm_cmcp, &tm_ucp->uc_regs))
		return -EFAULT;
	mcp = (struct mcontext __user *)(u64)cmcp;
	tm_mcp = (struct mcontext __user *)(u64)tm_cmcp;
	/* no need to check access_ok(mcp), since mcp < 4GB */

	set_current_blocked(&set);
	if (restore_tm_user_regs(regs, mcp, tm_mcp))
		return -EFAULT;

	return 0;
}
#endif

#ifdef CONFIG_PPC64
COMPAT_SYSCALL_DEFINE3(swapcontext, struct ucontext __user *, old_ctx,
		       struct ucontext __user *, new_ctx, int, ctx_size)
#else
SYSCALL_DEFINE3(swapcontext, struct ucontext __user *, old_ctx,
		       struct ucontext __user *, new_ctx, long, ctx_size)
#endif
{
	struct pt_regs *regs = current_pt_regs();
	int ctx_has_vsx_region = 0;

#ifdef CONFIG_PPC64
	unsigned long new_msr = 0;

	if (new_ctx) {
		struct mcontext __user *mcp;
		u32 cmcp;

		/*
		 * Get pointer to the real mcontext.  No need for
		 * access_ok since we are dealing with compat
		 * pointers.
		 */
		if (__get_user(cmcp, &new_ctx->uc_regs))
			return -EFAULT;
		mcp = (struct mcontext __user *)(u64)cmcp;
		if (__get_user(new_msr, &mcp->mc_gregs[PT_MSR]))
			return -EFAULT;
	}
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
#else
	/* Context size is for future use. Right now, we only make sure
	 * we are passed something we understand
	 */
	if (ctx_size < sizeof(struct ucontext))
		return -EINVAL;
#endif
	if (old_ctx != NULL) {
		struct mcontext __user *mctx;

		/*
		 * old_ctx might not be 16-byte aligned, in which
		 * case old_ctx->uc_mcontext won't be either.
		 * Because we have the old_ctx->uc_pad2 field
		 * before old_ctx->uc_mcontext, we need to round down
		 * from &old_ctx->uc_mcontext to a 16-byte boundary.
		 */
		mctx = (struct mcontext __user *)
			((unsigned long) &old_ctx->uc_mcontext & ~0xfUL);
		if (!access_ok(VERIFY_WRITE, old_ctx, ctx_size)
		    || save_user_regs(regs, mctx, NULL, 0, ctx_has_vsx_region)
		    || put_sigset_t(&old_ctx->uc_sigmask, &current->blocked)
		    || __put_user(to_user_ptr(mctx), &old_ctx->uc_regs))
			return -EFAULT;
	}
	if (new_ctx == NULL)
		return 0;
	if (!access_ok(VERIFY_READ, new_ctx, ctx_size) ||
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
	if (do_setcontext(new_ctx, regs, 0))
		do_exit(SIGSEGV);

	set_thread_flag(TIF_RESTOREALL);
	return 0;
}

#ifdef CONFIG_PPC64
COMPAT_SYSCALL_DEFINE0(rt_sigreturn)
#else
SYSCALL_DEFINE0(rt_sigreturn)
#endif
{
	struct rt_sigframe __user *rt_sf;
	struct pt_regs *regs = current_pt_regs();
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	struct ucontext __user *uc_transact;
	unsigned long msr_hi;
	unsigned long tmp;
	int tm_restore = 0;
#endif
	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	rt_sf = (struct rt_sigframe __user *)
		(regs->gpr[1] + __SIGNAL_FRAMESIZE + 16);
	if (!access_ok(VERIFY_READ, rt_sf, sizeof(*rt_sf)))
		goto bad;

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
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

	if (__get_user(tmp, &rt_sf->uc.uc_link))
		goto bad;
	uc_transact = (struct ucontext __user *)(uintptr_t)tmp;
	if (uc_transact) {
		u32 cmcp;
		struct mcontext __user *mcp;

		if (__get_user(cmcp, &uc_transact->uc_regs))
			return -EFAULT;
		mcp = (struct mcontext __user *)(u64)cmcp;
		/* The top 32 bits of the MSR are stashed in the transactional
		 * ucontext. */
		if (__get_user(msr_hi, &mcp->mc_gregs[PT_MSR]))
			goto bad;

		if (MSR_TM_ACTIVE(msr_hi<<32)) {
			/* Trying to start TM on non TM system */
			if (!cpu_has_feature(CPU_FTR_TM))
				goto bad;
			/* We only recheckpoint on return if we're
			 * transaction.
			 */
			tm_restore = 1;
			if (do_setcontext_tm(&rt_sf->uc, uc_transact, regs))
				goto bad;
		}
	}
	if (!tm_restore)
		/* Fall through, for non-TM restore */
#endif
	if (do_setcontext(&rt_sf->uc, regs, 1))
		goto bad;

	/*
	 * It's not clear whether or why it is desirable to save the
	 * sigaltstack setting on signal delivery and restore it on
	 * signal return.  But other architectures do this and we have
	 * always done it up until now so it is probably better not to
	 * change it.  -- paulus
	 */
#ifdef CONFIG_PPC64
	if (compat_restore_altstack(&rt_sf->uc.uc_stack))
		goto bad;
#else
	if (restore_altstack(&rt_sf->uc.uc_stack))
		goto bad;
#endif
	set_thread_flag(TIF_RESTOREALL);
	return 0;

 bad:
	if (show_unhandled_signals)
		printk_ratelimited(KERN_INFO
				   "%s[%d]: bad frame in sys_rt_sigreturn: "
				   "%p nip %08lx lr %08lx\n",
				   current->comm, current->pid,
				   rt_sf, regs->nip, regs->link);

	force_sig(SIGSEGV, current);
	return 0;
}

#ifdef CONFIG_PPC32
SYSCALL_DEFINE3(debug_setcontext, struct ucontext __user *, ctx,
			 int, ndbg, struct sig_dbg_op __user *, dbg)
{
	struct pt_regs *regs = current_pt_regs();
	struct sig_dbg_op op;
	int i;
	unsigned long new_msr = regs->msr;
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
	unsigned long new_dbcr0 = current->thread.debug.dbcr0;
#endif

	for (i=0; i<ndbg; i++) {
		if (copy_from_user(&op, dbg + i, sizeof(op)))
			return -EFAULT;
		switch (op.dbg_type) {
		case SIG_DBG_SINGLE_STEPPING:
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
			if (op.dbg_value) {
				new_msr |= MSR_DE;
				new_dbcr0 |= (DBCR0_IDM | DBCR0_IC);
			} else {
				new_dbcr0 &= ~DBCR0_IC;
				if (!DBCR_ACTIVE_EVENTS(new_dbcr0,
						current->thread.debug.dbcr1)) {
					new_msr &= ~MSR_DE;
					new_dbcr0 &= ~DBCR0_IDM;
				}
			}
#else
			if (op.dbg_value)
				new_msr |= MSR_SE;
			else
				new_msr &= ~MSR_SE;
#endif
			break;
		case SIG_DBG_BRANCH_TRACING:
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
			return -EINVAL;
#else
			if (op.dbg_value)
				new_msr |= MSR_BE;
			else
				new_msr &= ~MSR_BE;
#endif
			break;

		default:
			return -EINVAL;
		}
	}

	/* We wait until here to actually install the values in the
	   registers so if we fail in the above loop, it will not
	   affect the contents of these registers.  After this point,
	   failure is a problem, anyway, and it's very unlikely unless
	   the user is really doing something wrong. */
	regs->msr = new_msr;
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
	current->thread.debug.dbcr0 = new_dbcr0;
#endif

	if (!access_ok(VERIFY_READ, ctx, sizeof(*ctx)) ||
	    fault_in_pages_readable((u8 __user *)ctx, sizeof(*ctx)))
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
	if (do_setcontext(ctx, regs, 1)) {
		if (show_unhandled_signals)
			printk_ratelimited(KERN_INFO "%s[%d]: bad frame in "
					   "sys_debug_setcontext: %p nip %08lx "
					   "lr %08lx\n",
					   current->comm, current->pid,
					   ctx, regs->nip, regs->link);

		force_sig(SIGSEGV, current);
		goto out;
	}

	/*
	 * It's not clear whether or why it is desirable to save the
	 * sigaltstack setting on signal delivery and restore it on
	 * signal return.  But other architectures do this and we have
	 * always done it up until now so it is probably better not to
	 * change it.  -- paulus
	 */
	restore_altstack(&ctx->uc_stack);

	set_thread_flag(TIF_RESTOREALL);
 out:
	return 0;
}
#endif

/*
 * OK, we're invoking a handler
 */
int handle_signal32(struct ksignal *ksig, sigset_t *oldset,
		struct task_struct *tsk)
{
	struct sigcontext __user *sc;
	struct sigframe __user *frame;
	struct mcontext __user *tm_mctx = NULL;
	unsigned long newsp = 0;
	int sigret;
	unsigned long tramp;
	struct pt_regs *regs = tsk->thread.regs;

	BUG_ON(tsk != current);

	/* Set up Signal Frame */
	frame = get_sigframe(ksig, get_tm_stackpointer(tsk), sizeof(*frame), 1);
	if (unlikely(frame == NULL))
		goto badframe;
	sc = (struct sigcontext __user *) &frame->sctx;

#if _NSIG != 64
#error "Please adjust handle_signal()"
#endif
	if (__put_user(to_user_ptr(ksig->ka.sa.sa_handler), &sc->handler)
	    || __put_user(oldset->sig[0], &sc->oldmask)
#ifdef CONFIG_PPC64
	    || __put_user((oldset->sig[0] >> 32), &sc->_unused[3])
#else
	    || __put_user(oldset->sig[1], &sc->_unused[3])
#endif
	    || __put_user(to_user_ptr(&frame->mctx), &sc->regs)
	    || __put_user(ksig->sig, &sc->signal))
		goto badframe;

	if (vdso32_sigtramp && tsk->mm->context.vdso_base) {
		sigret = 0;
		tramp = tsk->mm->context.vdso_base + vdso32_sigtramp;
	} else {
		sigret = __NR_sigreturn;
		tramp = (unsigned long) frame->mctx.tramp;
	}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	tm_mctx = &frame->mctx_transact;
	if (MSR_TM_ACTIVE(regs->msr)) {
		if (save_tm_user_regs(regs, &frame->mctx, &frame->mctx_transact,
				      sigret))
			goto badframe;
	}
	else
#endif
	{
		if (save_user_regs(regs, &frame->mctx, tm_mctx, sigret, 1))
			goto badframe;
	}

	regs->link = tramp;

	tsk->thread.fp_state.fpscr = 0;	/* turn off all fp exceptions */

	/* create a stack frame for the caller of the handler */
	newsp = ((unsigned long)frame) - __SIGNAL_FRAMESIZE;
	if (put_user(regs->gpr[1], (u32 __user *)newsp))
		goto badframe;

	regs->gpr[1] = newsp;
	regs->gpr[3] = ksig->sig;
	regs->gpr[4] = (unsigned long) sc;
	regs->nip = (unsigned long) (unsigned long)ksig->ka.sa.sa_handler;
	/* enter the signal handler in big-endian mode */
	regs->msr &= ~MSR_LE;
	return 0;

badframe:
	if (show_unhandled_signals)
		printk_ratelimited(KERN_INFO
				   "%s[%d]: bad frame in handle_signal32: "
				   "%p nip %08lx lr %08lx\n",
				   tsk->comm, tsk->pid,
				   frame, regs->nip, regs->link);

	return 1;
}

/*
 * Do a signal return; undo the signal stack.
 */
#ifdef CONFIG_PPC64
COMPAT_SYSCALL_DEFINE0(sigreturn)
#else
SYSCALL_DEFINE0(sigreturn)
#endif
{
	struct pt_regs *regs = current_pt_regs();
	struct sigframe __user *sf;
	struct sigcontext __user *sc;
	struct sigcontext sigctx;
	struct mcontext __user *sr;
	void __user *addr;
	sigset_t set;
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	struct mcontext __user *mcp, *tm_mcp;
	unsigned long msr_hi;
#endif

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	sf = (struct sigframe __user *)(regs->gpr[1] + __SIGNAL_FRAMESIZE);
	sc = &sf->sctx;
	addr = sc;
	if (copy_from_user(&sigctx, sc, sizeof(sigctx)))
		goto badframe;

#ifdef CONFIG_PPC64
	/*
	 * Note that PPC32 puts the upper 32 bits of the sigmask in the
	 * unused part of the signal stackframe
	 */
	set.sig[0] = sigctx.oldmask + ((long)(sigctx._unused[3]) << 32);
#else
	set.sig[0] = sigctx.oldmask;
	set.sig[1] = sigctx._unused[3];
#endif
	set_current_blocked(&set);

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	mcp = (struct mcontext __user *)&sf->mctx;
	tm_mcp = (struct mcontext __user *)&sf->mctx_transact;
	if (__get_user(msr_hi, &tm_mcp->mc_gregs[PT_MSR]))
		goto badframe;
	if (MSR_TM_ACTIVE(msr_hi<<32)) {
		if (!cpu_has_feature(CPU_FTR_TM))
			goto badframe;
		if (restore_tm_user_regs(regs, mcp, tm_mcp))
			goto badframe;
	} else
#endif
	{
		sr = (struct mcontext __user *)from_user_ptr(sigctx.regs);
		addr = sr;
		if (!access_ok(VERIFY_READ, sr, sizeof(*sr))
		    || restore_user_regs(regs, sr, 1))
			goto badframe;
	}

	set_thread_flag(TIF_RESTOREALL);
	return 0;

badframe:
	if (show_unhandled_signals)
		printk_ratelimited(KERN_INFO
				   "%s[%d]: bad frame in sys_sigreturn: "
				   "%p nip %08lx lr %08lx\n",
				   current->comm, current->pid,
				   addr, regs->nip, regs->link);

	force_sig(SIGSEGV, current);
	return 0;
}
