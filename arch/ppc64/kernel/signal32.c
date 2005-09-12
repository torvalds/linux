/*
 * signal32.c: Support 32bit signal syscalls.
 *
 * Copyright (C) 2001 IBM
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/mm.h> 
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/syscalls.h>
#include <linux/errno.h>
#include <linux/elf.h>
#include <linux/compat.h>
#include <linux/ptrace.h>
#include <asm/ppc32.h>
#include <asm/uaccess.h>
#include <asm/ppcdebug.h>
#include <asm/unistd.h>
#include <asm/cacheflush.h>
#include <asm/vdso.h>

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

#define GP_REGS_SIZE32	min(sizeof(elf_gregset_t32), sizeof(struct pt_regs32))

/*
 * When we have signals to deliver, we set up on the
 * user stack, going down from the original stack pointer:
 *	a sigregs32 struct
 *	a sigcontext32 struct
 *	a gap of __SIGNAL_FRAMESIZE32 bytes
 *
 * Each of these things must be a multiple of 16 bytes in size.
 *
 */
struct sigregs32 {
	struct mcontext32	mctx;		/* all the register values */
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
 *	one rt_sigframe32 struct (siginfo + ucontext + ABI gap)
 *	a gap of __SIGNAL_FRAMESIZE32+16 bytes
 *  (the +16 is to get the siginfo and ucontext32 in the same
 *  positions as in older kernels).
 *
 *  Each of these things must be a multiple of 16 bytes in size.
 *
 */
struct rt_sigframe32 {
	compat_siginfo_t	info;
	struct ucontext32	uc;
	/*
	 * Programs using the rs6000/xcoff abi can save up to 19 gp
	 * regs and 18 fp regs below sp before decrementing it.
	 */
	int			abigap[56];
};


/*
 * Common utility functions used by signal and context support
 *
 */

/*
 * Restore the user process's signal mask
 * (implemented in signal.c)
 */
extern void restore_sigmask(sigset_t *set);

/*
 * Functions for flipping sigsets (thanks to brain dead generic
 * implementation that makes things simple for little endian only
 */
static inline void compat_from_sigset(compat_sigset_t *compat, sigset_t *set)
{
	switch (_NSIG_WORDS) {
	case 4: compat->sig[5] = set->sig[3] & 0xffffffffull ;
		compat->sig[7] = set->sig[3] >> 32; 
	case 3: compat->sig[4] = set->sig[2] & 0xffffffffull ;
		compat->sig[5] = set->sig[2] >> 32; 
	case 2: compat->sig[2] = set->sig[1] & 0xffffffffull ;
		compat->sig[3] = set->sig[1] >> 32; 
	case 1: compat->sig[0] = set->sig[0] & 0xffffffffull ;
		compat->sig[1] = set->sig[0] >> 32; 
	}
}

static inline void sigset_from_compat(sigset_t *set, compat_sigset_t *compat)
{
	switch (_NSIG_WORDS) {
	case 4: set->sig[3] = compat->sig[6] | (((long)compat->sig[7]) << 32);
	case 3: set->sig[2] = compat->sig[4] | (((long)compat->sig[5]) << 32);
	case 2: set->sig[1] = compat->sig[2] | (((long)compat->sig[3]) << 32);
	case 1: set->sig[0] = compat->sig[0] | (((long)compat->sig[1]) << 32);
	}
}


/*
 * Save the current user registers on the user stack.
 * We only save the altivec registers if the process has used
 * altivec instructions at some point.
 */
static int save_user_regs(struct pt_regs *regs, struct mcontext32 __user *frame, int sigret)
{
	elf_greg_t64 *gregs = (elf_greg_t64 *)regs;
	int i, err = 0;

	/* Make sure floating point registers are stored in regs */
	flush_fp_to_thread(current);

	/* save general and floating-point registers */
	for (i = 0; i <= PT_RESULT; i ++)
		err |= __put_user((unsigned int)gregs[i], &frame->mc_gregs[i]);
	err |= __copy_to_user(&frame->mc_fregs, current->thread.fpr,
			      ELF_NFPREG * sizeof(double));
	if (err)
		return 1;

	current->thread.fpscr = 0;	/* turn off all fp exceptions */

#ifdef CONFIG_ALTIVEC
	/* save altivec registers */
	if (current->thread.used_vr) {
		flush_altivec_to_thread(current);
		if (__copy_to_user(&frame->mc_vregs, current->thread.vr,
				   ELF_NVRREG32 * sizeof(vector128)))
			return 1;
		/* set MSR_VEC in the saved MSR value to indicate that
		   frame->mc_vregs contains valid data */
		if (__put_user(regs->msr | MSR_VEC, &frame->mc_gregs[PT_MSR]))
			return 1;
	}
	/* else assert((regs->msr & MSR_VEC) == 0) */

	/* We always copy to/from vrsave, it's 0 if we don't have or don't
	 * use altivec. Since VSCR only contains 32 bits saved in the least
	 * significant bits of a vector, we "cheat" and stuff VRSAVE in the
	 * most significant bits of that same vector. --BenH
	 */
	if (__put_user(current->thread.vrsave, (u32 __user *)&frame->mc_vregs[32]))
		return 1;
#endif /* CONFIG_ALTIVEC */

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

/*
 * Restore the current user register values from the user stack,
 * (except for MSR).
 */
static long restore_user_regs(struct pt_regs *regs,
			      struct mcontext32 __user *sr, int sig)
{
	elf_greg_t64 *gregs = (elf_greg_t64 *)regs;
	int i;
	long err = 0;
	unsigned int save_r2 = 0;
#ifdef CONFIG_ALTIVEC
	unsigned long msr;
#endif

	/*
	 * restore general registers but not including MSR or SOFTE. Also
	 * take care of keeping r2 (TLS) intact if not a signal
	 */
	if (!sig)
		save_r2 = (unsigned int)regs->gpr[2];
	for (i = 0; i <= PT_RESULT; i++) {
		if ((i == PT_MSR) || (i == PT_SOFTE))
			continue;
		err |= __get_user(gregs[i], &sr->mc_gregs[i]);
	}
	if (!sig)
		regs->gpr[2] = (unsigned long) save_r2;
	if (err)
		return 1;

	/* force the process to reload the FP registers from
	   current->thread when it next does FP instructions */
	regs->msr &= ~(MSR_FP | MSR_FE0 | MSR_FE1);
	if (__copy_from_user(current->thread.fpr, &sr->mc_fregs,
			     sizeof(sr->mc_fregs)))
		return 1;

#ifdef CONFIG_ALTIVEC
	/* force the process to reload the altivec registers from
	   current->thread when it next does altivec instructions */
	regs->msr &= ~MSR_VEC;
	if (!__get_user(msr, &sr->mc_gregs[PT_MSR]) && (msr & MSR_VEC) != 0) {
		/* restore altivec registers from the stack */
		if (__copy_from_user(current->thread.vr, &sr->mc_vregs,
				     sizeof(sr->mc_vregs)))
			return 1;
	} else if (current->thread.used_vr)
		memset(current->thread.vr, 0, ELF_NVRREG32 * sizeof(vector128));

	/* Always get VRSAVE back */
	if (__get_user(current->thread.vrsave, (u32 __user *)&sr->mc_vregs[32]))
		return 1;
#endif /* CONFIG_ALTIVEC */

#ifndef CONFIG_SMP
	preempt_disable();
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	if (last_task_used_altivec == current)
		last_task_used_altivec = NULL;
	preempt_enable();
#endif
	return 0;
}


/*
 *  Start of nonRT signal support
 *
 *     sigset_t is 32 bits for non-rt signals
 *
 *  System Calls
 *       sigaction                sys32_sigaction
 *       sigreturn                sys32_sigreturn
 *
 *  Note sigsuspend has no special 32 bit routine - uses the 64 bit routine
 *
 *  Other routines
 *        setup_frame32
 */

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
long sys32_sigsuspend(old_sigset_t mask, int p2, int p3, int p4, int p6, int p7,
	       struct pt_regs *regs)
{
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->result = -EINTR;
	regs->gpr[3] = EINTR;
	regs->ccr |= 0x10000000;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal32(&saveset, regs))
			/*
			 * Returning 0 means we return to userspace via
			 * ret_from_except and thus restore all user
			 * registers from *regs.  This is what we need
			 * to do when a signal has been delivered.
			 */
			return 0;
	}
}

long sys32_sigaction(int sig, struct old_sigaction32 __user *act,
		struct old_sigaction32 __user *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;
	
	if (sig < 0)
		sig = -sig;

	if (act) {
		compat_old_sigset_t mask;
		compat_uptr_t handler, restorer;

		if (get_user(handler, &act->sa_handler) ||
		    __get_user(restorer, &act->sa_restorer) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags) ||
		    __get_user(mask, &act->sa_mask))
			return -EFAULT;
		new_ka.sa.sa_handler = compat_ptr(handler);
		new_ka.sa.sa_restorer = compat_ptr(restorer);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);
	if (!ret && oact) {
		if (put_user((long)old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user((long)old_ka.sa.sa_restorer, &oact->sa_restorer) ||
		    __put_user(old_ka.sa.sa_flags, &oact->sa_flags) ||
		    __put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask))
			return -EFAULT;
	}

	return ret;
}



/*
 *  Start of RT signal support
 *
 *     sigset_t is 64 bits for rt signals
 *
 *  System Calls
 *       sigaction                sys32_rt_sigaction
 *       sigpending               sys32_rt_sigpending
 *       sigprocmask              sys32_rt_sigprocmask
 *       sigreturn                sys32_rt_sigreturn
 *       sigqueueinfo             sys32_rt_sigqueueinfo
 *       sigsuspend               sys32_rt_sigsuspend
 *
 *  Other routines
 *        setup_rt_frame32
 *        copy_siginfo_to_user32
 *        siginfo32to64
 */


long sys32_rt_sigaction(int sig, const struct sigaction32 __user *act,
		struct sigaction32 __user *oact, size_t sigsetsize)
{
	struct k_sigaction new_ka, old_ka;
	int ret;
	compat_sigset_t set32;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(compat_sigset_t))
		return -EINVAL;

	if (act) {
		compat_uptr_t handler;

		ret = get_user(handler, &act->sa_handler);
		new_ka.sa.sa_handler = compat_ptr(handler);
		ret |= __copy_from_user(&set32, &act->sa_mask,
					sizeof(compat_sigset_t));
		sigset_from_compat(&new_ka.sa.sa_mask, &set32);
		ret |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		if (ret)
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);
	if (!ret && oact) {
		compat_from_sigset(&set32, &old_ka.sa.sa_mask);
		ret = put_user((long)old_ka.sa.sa_handler, &oact->sa_handler);
		ret |= __copy_to_user(&oact->sa_mask, &set32,
				      sizeof(compat_sigset_t));
		ret |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
	}
	return ret;
}

/*
 * Note: it is necessary to treat how as an unsigned int, with the
 * corresponding cast to a signed int to insure that the proper
 * conversion (sign extension) between the register representation
 * of a signed int (msr in 32-bit mode) and the register representation
 * of a signed int (msr in 64-bit mode) is performed.
 */
long sys32_rt_sigprocmask(u32 how, compat_sigset_t __user *set,
		compat_sigset_t __user *oset, size_t sigsetsize)
{
	sigset_t s;
	sigset_t __user *up;
	compat_sigset_t s32;
	int ret;
	mm_segment_t old_fs = get_fs();

	if (set) {
		if (copy_from_user (&s32, set, sizeof(compat_sigset_t)))
			return -EFAULT;    
		sigset_from_compat(&s, &s32);
	}
	
	set_fs(KERNEL_DS);
	/* This is valid because of the set_fs() */
	up = (sigset_t __user *) &s;
	ret = sys_rt_sigprocmask((int)how, set ? up : NULL, oset ? up : NULL,
				 sigsetsize); 
	set_fs(old_fs);
	if (ret)
		return ret;
	if (oset) {
		compat_from_sigset(&s32, &s);
		if (copy_to_user (oset, &s32, sizeof(compat_sigset_t)))
			return -EFAULT;
	}
	return 0;
}

long sys32_rt_sigpending(compat_sigset_t __user *set, compat_size_t sigsetsize)
{
	sigset_t s;
	compat_sigset_t s32;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	/* The __user pointer cast is valid because of the set_fs() */
	ret = sys_rt_sigpending((sigset_t __user *) &s, sigsetsize);
	set_fs(old_fs);
	if (!ret) {
		compat_from_sigset(&s32, &s);
		if (copy_to_user (set, &s32, sizeof(compat_sigset_t)))
			return -EFAULT;
	}
	return ret;
}


int copy_siginfo_to_user32(struct compat_siginfo __user *d, siginfo_t *s)
{
	int err;

	if (!access_ok (VERIFY_WRITE, d, sizeof(*d)))
		return -EFAULT;

	/* If you change siginfo_t structure, please be sure
	 * this code is fixed accordingly.
	 * It should never copy any pad contained in the structure
	 * to avoid security leaks, but must copy the generic
	 * 3 ints plus the relevant union member.
	 * This routine must convert siginfo from 64bit to 32bit as well
	 * at the same time.
	 */
	err = __put_user(s->si_signo, &d->si_signo);
	err |= __put_user(s->si_errno, &d->si_errno);
	err |= __put_user((short)s->si_code, &d->si_code);
	if (s->si_code < 0)
		err |= __copy_to_user(&d->_sifields._pad, &s->_sifields._pad,
				      SI_PAD_SIZE32);
	else switch(s->si_code >> 16) {
	case __SI_CHLD >> 16:
		err |= __put_user(s->si_pid, &d->si_pid);
		err |= __put_user(s->si_uid, &d->si_uid);
		err |= __put_user(s->si_utime, &d->si_utime);
		err |= __put_user(s->si_stime, &d->si_stime);
		err |= __put_user(s->si_status, &d->si_status);
		break;
	case __SI_FAULT >> 16:
		err |= __put_user((unsigned int)(unsigned long)s->si_addr,
				  &d->si_addr);
		break;
	case __SI_POLL >> 16:
		err |= __put_user(s->si_band, &d->si_band);
		err |= __put_user(s->si_fd, &d->si_fd);
		break;
	case __SI_TIMER >> 16:
		err |= __put_user(s->si_tid, &d->si_tid);
		err |= __put_user(s->si_overrun, &d->si_overrun);
		err |= __put_user(s->si_int, &d->si_int);
		break;
	case __SI_RT >> 16: /* This is not generated by the kernel as of now.  */
	case __SI_MESGQ >> 16:
		err |= __put_user(s->si_int, &d->si_int);
		/* fallthrough */
	case __SI_KILL >> 16:
	default:
		err |= __put_user(s->si_pid, &d->si_pid);
		err |= __put_user(s->si_uid, &d->si_uid);
		break;
	}
	return err;
}

/*
 * Note: it is necessary to treat pid and sig as unsigned ints, with the
 * corresponding cast to a signed int to insure that the proper conversion
 * (sign extension) between the register representation of a signed int
 * (msr in 32-bit mode) and the register representation of a signed int
 * (msr in 64-bit mode) is performed.
 */
long sys32_rt_sigqueueinfo(u32 pid, u32 sig, compat_siginfo_t __user *uinfo)
{
	siginfo_t info;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	if (copy_from_user (&info, uinfo, 3*sizeof(int)) ||
	    copy_from_user (info._sifields._pad, uinfo->_sifields._pad, SI_PAD_SIZE32))
		return -EFAULT;
	set_fs (KERNEL_DS);
	/* The __user pointer cast is valid becasuse of the set_fs() */
	ret = sys_rt_sigqueueinfo((int)pid, (int)sig, (siginfo_t __user *) &info);
	set_fs (old_fs);
	return ret;
}

int sys32_rt_sigsuspend(compat_sigset_t __user * unewset, size_t sigsetsize, int p3,
		int p4, int p6, int p7, struct pt_regs *regs)
{
	sigset_t saveset, newset;
	compat_sigset_t s32;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&s32, unewset, sizeof(s32)))
		return -EFAULT;

	/*
	 * Swap the 2 words of the 64-bit sigset_t (they are stored
	 * in the "wrong" endian in 32-bit user storage).
	 */
	sigset_from_compat(&newset, &s32);

	sigdelsetmask(&newset, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs->result = -EINTR;
	regs->gpr[3] = EINTR;
	regs->ccr |= 0x10000000;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal32(&saveset, regs))
			/*
			 * Returning 0 means we return to userspace via
			 * ret_from_except and thus restore all user
			 * registers from *regs.  This is what we need
			 * to do when a signal has been delivered.
			 */
			return 0;
	}
}

/*
 *  Start Alternate signal stack support
 *
 *  System Calls
 *       sigaltatck               sys32_sigaltstack
 */

int sys32_sigaltstack(u32 __new, u32 __old, int r5,
		      int r6, int r7, int r8, struct pt_regs *regs)
{
	stack_32_t __user * newstack = (stack_32_t __user *)(long) __new;
	stack_32_t __user * oldstack = (stack_32_t __user *)(long) __old;
	stack_t uss, uoss;
	int ret;
	mm_segment_t old_fs;
	unsigned long sp;
	compat_uptr_t ss_sp;

	/*
	 * set sp to the user stack on entry to the system call
	 * the system call router sets R9 to the saved registers
	 */
	sp = regs->gpr[1];

	/* Put new stack info in local 64 bit stack struct */
	if (newstack) {
		if (get_user(ss_sp, &newstack->ss_sp) ||
		    __get_user(uss.ss_flags, &newstack->ss_flags) ||
		    __get_user(uss.ss_size, &newstack->ss_size))
			return -EFAULT;
		uss.ss_sp = compat_ptr(ss_sp);
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	/* The __user pointer casts are valid because of the set_fs() */
	ret = do_sigaltstack(
		newstack ? (stack_t __user *) &uss : NULL,
		oldstack ? (stack_t __user *) &uoss : NULL,
		sp);
	set_fs(old_fs);
	/* Copy the stack information to the user output buffer */
	if (!ret && oldstack  &&
		(put_user((long)uoss.ss_sp, &oldstack->ss_sp) ||
		 __put_user(uoss.ss_flags, &oldstack->ss_flags) ||
		 __put_user(uoss.ss_size, &oldstack->ss_size)))
		return -EFAULT;
	return ret;
}


/*
 * Set up a signal frame for a "real-time" signal handler
 * (one which gets siginfo).
 */
static int handle_rt_signal32(unsigned long sig, struct k_sigaction *ka,
			      siginfo_t *info, sigset_t *oldset,
			      struct pt_regs * regs, unsigned long newsp)
{
	struct rt_sigframe32 __user *rt_sf;
	struct mcontext32 __user *frame;
	unsigned long origsp = newsp;
	compat_sigset_t c_oldset;

	/* Set up Signal Frame */
	/* Put a Real Time Context onto stack */
	newsp -= sizeof(*rt_sf);
	rt_sf = (struct rt_sigframe32 __user *)newsp;

	/* create a stack frame for the caller of the handler */
	newsp -= __SIGNAL_FRAMESIZE32 + 16;

	if (!access_ok(VERIFY_WRITE, (void __user *)newsp, origsp - newsp))
		goto badframe;

	compat_from_sigset(&c_oldset, oldset);

	/* Put the siginfo & fill in most of the ucontext */
	if (copy_siginfo_to_user32(&rt_sf->info, info)
	    || __put_user(0, &rt_sf->uc.uc_flags)
	    || __put_user(0, &rt_sf->uc.uc_link)
	    || __put_user(current->sas_ss_sp, &rt_sf->uc.uc_stack.ss_sp)
	    || __put_user(sas_ss_flags(regs->gpr[1]),
			  &rt_sf->uc.uc_stack.ss_flags)
	    || __put_user(current->sas_ss_size, &rt_sf->uc.uc_stack.ss_size)
	    || __put_user((u32)(u64)&rt_sf->uc.uc_mcontext, &rt_sf->uc.uc_regs)
	    || __copy_to_user(&rt_sf->uc.uc_sigmask, &c_oldset, sizeof(c_oldset)))
		goto badframe;

	/* Save user registers on the stack */
	frame = &rt_sf->uc.uc_mcontext;
	if (put_user(regs->gpr[1], (u32 __user *)newsp))
		goto badframe;

	if (vdso32_rt_sigtramp && current->thread.vdso_base) {
		if (save_user_regs(regs, frame, 0))
			goto badframe;
		regs->link = current->thread.vdso_base + vdso32_rt_sigtramp;
	} else {
		if (save_user_regs(regs, frame, __NR_rt_sigreturn))
			goto badframe;
		regs->link = (unsigned long) frame->tramp;
	}
	regs->gpr[1] = (unsigned long) newsp;
	regs->gpr[3] = sig;
	regs->gpr[4] = (unsigned long) &rt_sf->info;
	regs->gpr[5] = (unsigned long) &rt_sf->uc;
	regs->gpr[6] = (unsigned long) rt_sf;
	regs->nip = (unsigned long) ka->sa.sa_handler;
	regs->trap = 0;
	regs->result = 0;

	if (test_thread_flag(TIF_SINGLESTEP))
		ptrace_notify(SIGTRAP);

	return 1;

badframe:
#if DEBUG_SIG
	printk("badframe in handle_rt_signal, regs=%p frame=%p newsp=%lx\n",
	       regs, frame, newsp);
#endif
	force_sigsegv(sig, current);
	return 0;
}

static long do_setcontext32(struct ucontext32 __user *ucp, struct pt_regs *regs, int sig)
{
	compat_sigset_t c_set;
	sigset_t set;
	u32 mcp;

	if (__copy_from_user(&c_set, &ucp->uc_sigmask, sizeof(c_set))
	    || __get_user(mcp, &ucp->uc_regs))
		return -EFAULT;
	sigset_from_compat(&set, &c_set);
	restore_sigmask(&set);
	if (restore_user_regs(regs, (struct mcontext32 __user *)(u64)mcp, sig))
		return -EFAULT;

	return 0;
}

/*
 * Handle {get,set,swap}_context operations for 32 bits processes
 */

long sys32_swapcontext(struct ucontext32 __user *old_ctx,
		       struct ucontext32 __user *new_ctx,
		       int ctx_size, int r6, int r7, int r8, struct pt_regs *regs)
{
	unsigned char tmp;
	compat_sigset_t c_set;

	/* Context size is for future use. Right now, we only make sure
	 * we are passed something we understand
	 */
	if (ctx_size < sizeof(struct ucontext32))
		return -EINVAL;

	if (old_ctx != NULL) {
		compat_from_sigset(&c_set, &current->blocked);
		if (!access_ok(VERIFY_WRITE, old_ctx, sizeof(*old_ctx))
		    || save_user_regs(regs, &old_ctx->uc_mcontext, 0)
		    || __copy_to_user(&old_ctx->uc_sigmask, &c_set, sizeof(c_set))
		    || __put_user((u32)(u64)&old_ctx->uc_mcontext, &old_ctx->uc_regs))
			return -EFAULT;
	}
	if (new_ctx == NULL)
		return 0;
	if (!access_ok(VERIFY_READ, new_ctx, sizeof(*new_ctx))
	    || __get_user(tmp, (u8 __user *) new_ctx)
	    || __get_user(tmp, (u8 __user *) (new_ctx + 1) - 1))
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
	if (do_setcontext32(new_ctx, regs, 0))
		do_exit(SIGSEGV);

	return 0;
}

long sys32_rt_sigreturn(int r3, int r4, int r5, int r6, int r7, int r8,
		     struct pt_regs *regs)
{
	struct rt_sigframe32 __user *rt_sf;
	int ret;


	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	rt_sf = (struct rt_sigframe32 __user *)
		(regs->gpr[1] + __SIGNAL_FRAMESIZE32 + 16);
	if (!access_ok(VERIFY_READ, rt_sf, sizeof(*rt_sf)))
		goto bad;
	if (do_setcontext32(&rt_sf->uc, regs, 1))
		goto bad;

	/*
	 * It's not clear whether or why it is desirable to save the
	 * sigaltstack setting on signal delivery and restore it on
	 * signal return.  But other architectures do this and we have
	 * always done it up until now so it is probably better not to
	 * change it.  -- paulus
	 * We use the sys32_ version that does the 32/64 bits conversion
	 * and takes userland pointer directly. What about error checking ?
	 * nobody does any...
	 */
       	sys32_sigaltstack((u32)(u64)&rt_sf->uc.uc_stack, 0, 0, 0, 0, 0, regs);

	ret = regs->result;

	return ret;

 bad:
	force_sig(SIGSEGV, current);
	return 0;
}


/*
 * OK, we're invoking a handler
 */
static int handle_signal32(unsigned long sig, struct k_sigaction *ka,
			    siginfo_t *info, sigset_t *oldset,
			    struct pt_regs * regs, unsigned long newsp)
{
	struct sigcontext32 __user *sc;
	struct sigregs32 __user *frame;
	unsigned long origsp = newsp;

	/* Set up Signal Frame */
	newsp -= sizeof(struct sigregs32);
	frame = (struct sigregs32 __user *) newsp;

	/* Put a sigcontext on the stack */
	newsp -= sizeof(*sc);
	sc = (struct sigcontext32 __user *) newsp;

	/* create a stack frame for the caller of the handler */
	newsp -= __SIGNAL_FRAMESIZE32;

	if (!access_ok(VERIFY_WRITE, (void __user *) newsp, origsp - newsp))
		goto badframe;

#if _NSIG != 64
#error "Please adjust handle_signal32()"
#endif
	if (__put_user((u32)(u64)ka->sa.sa_handler, &sc->handler)
	    || __put_user(oldset->sig[0], &sc->oldmask)
	    || __put_user((oldset->sig[0] >> 32), &sc->_unused[3])
	    || __put_user((u32)(u64)frame, &sc->regs)
	    || __put_user(sig, &sc->signal))
		goto badframe;

	if (vdso32_sigtramp && current->thread.vdso_base) {
		if (save_user_regs(regs, &frame->mctx, 0))
			goto badframe;
		regs->link = current->thread.vdso_base + vdso32_sigtramp;
	} else {
		if (save_user_regs(regs, &frame->mctx, __NR_sigreturn))
			goto badframe;
		regs->link = (unsigned long) frame->mctx.tramp;
	}

	if (put_user(regs->gpr[1], (u32 __user *)newsp))
		goto badframe;
	regs->gpr[1] = (unsigned long) newsp;
	regs->gpr[3] = sig;
	regs->gpr[4] = (unsigned long) sc;
	regs->nip = (unsigned long) ka->sa.sa_handler;
	regs->trap = 0;
	regs->result = 0;

	if (test_thread_flag(TIF_SINGLESTEP))
		ptrace_notify(SIGTRAP);

	return 1;

badframe:
#if DEBUG_SIG
	printk("badframe in handle_signal, regs=%p frame=%x newsp=%x\n",
	       regs, frame, *newspp);
#endif
	force_sigsegv(sig, current);
	return 0;
}

/*
 * Do a signal return; undo the signal stack.
 */
long sys32_sigreturn(int r3, int r4, int r5, int r6, int r7, int r8,
		       struct pt_regs *regs)
{
	struct sigcontext32 __user *sc;
	struct sigcontext32 sigctx;
	struct mcontext32 __user *sr;
	sigset_t set;
	int ret;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	sc = (struct sigcontext32 __user *)(regs->gpr[1] + __SIGNAL_FRAMESIZE32);
	if (copy_from_user(&sigctx, sc, sizeof(sigctx)))
		goto badframe;

	/*
	 * Note that PPC32 puts the upper 32 bits of the sigmask in the
	 * unused part of the signal stackframe
	 */
	set.sig[0] = sigctx.oldmask + ((long)(sigctx._unused[3]) << 32);
	restore_sigmask(&set);

	sr = (struct mcontext32 __user *)(u64)sigctx.regs;
	if (!access_ok(VERIFY_READ, sr, sizeof(*sr))
	    || restore_user_regs(regs, sr, 1))
		goto badframe;

	ret = regs->result;
	return ret;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}



/*
 *  Start of do_signal32 routine
 *
 *   This routine gets control when a pending signal needs to be processed
 *     in the 32 bit target thread -
 *
 *   It handles both rt and non-rt signals
 */

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */

int do_signal32(sigset_t *oldset, struct pt_regs *regs)
{
	siginfo_t info;
	unsigned int frame, newsp;
	int signr, ret;
	struct k_sigaction ka;

	if (!oldset)
		oldset = &current->blocked;

	newsp = frame = 0;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);

	if (TRAP(regs) == 0x0C00		/* System Call! */
	    && regs->ccr & 0x10000000		/* error signalled */
	    && ((ret = regs->gpr[3]) == ERESTARTSYS
		|| ret == ERESTARTNOHAND || ret == ERESTARTNOINTR
		|| ret == ERESTART_RESTARTBLOCK)) {

		if (signr > 0
		    && (ret == ERESTARTNOHAND || ret == ERESTART_RESTARTBLOCK
			|| (ret == ERESTARTSYS
			    && !(ka.sa.sa_flags & SA_RESTART)))) {
			/* make the system call return an EINTR error */
			regs->result = -EINTR;
			regs->gpr[3] = EINTR;
			/* note that the cr0.SO bit is already set */
		} else {
			regs->nip -= 4;	/* Back up & retry system call */
			regs->result = 0;
			regs->trap = 0;
			if (ret == ERESTART_RESTARTBLOCK)
				regs->gpr[0] = __NR_restart_syscall;
			else
				regs->gpr[3] = regs->orig_gpr3;
		}
	}

	if (signr == 0)
		return 0;		/* no signals delivered */

	if ((ka.sa.sa_flags & SA_ONSTACK) && current->sas_ss_size
	    && (!on_sig_stack(regs->gpr[1])))
		newsp = (current->sas_ss_sp + current->sas_ss_size);
	else
		newsp = regs->gpr[1];
	newsp &= ~0xfUL;

	/*
	 * Reenable the DABR before delivering the signal to
	 * user space. The DABR will have been cleared if it
	 * triggered inside the kernel.
	 */
	if (current->thread.dabr)
		set_dabr(current->thread.dabr);

	/* Whee!  Actually deliver the signal.  */
	if (ka.sa.sa_flags & SA_SIGINFO)
		ret = handle_rt_signal32(signr, &ka, &info, oldset, regs, newsp);
	else
		ret = handle_signal32(signr, &ka, &info, oldset, regs, newsp);

	if (ret) {
		spin_lock_irq(&current->sighand->siglock);
		sigorsets(&current->blocked, &current->blocked,
			  &ka.sa.sa_mask);
		if (!(ka.sa.sa_flags & SA_NODEFER))
			sigaddset(&current->blocked, signr);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}

	return ret;
}
