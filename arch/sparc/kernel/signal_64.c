/*
 *  arch/sparc64/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1995, 2008 David S. Miller (davem@davemloft.net)
 *  Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1997 Eddie C. Dost   (ecd@skynet.be)
 *  Copyright (C) 1997,1998 Jakub Jelinek   (jj@sunsite.mff.cuni.cz)
 */

#ifdef CONFIG_COMPAT
#include <linux/compat.h>	/* for compat_old_sigset_t */
#endif
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/tracehook.h>
#include <linux/unistd.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/binfmts.h>
#include <linux/bitops.h>

#include <asm/uaccess.h>
#include <asm/ptrace.h>
#include <asm/pgtable.h>
#include <asm/fpumacro.h>
#include <asm/uctx.h>
#include <asm/siginfo.h>
#include <asm/visasm.h>
#include <asm/switch_to.h>
#include <asm/cacheflush.h>

#include "entry.h"
#include "systbls.h"
#include "sigutil.h"

/* {set, get}context() needed for 64-bit SparcLinux userland. */
asmlinkage void sparc64_set_context(struct pt_regs *regs)
{
	struct ucontext __user *ucp = (struct ucontext __user *)
		regs->u_regs[UREG_I0];
	mc_gregset_t __user *grp;
	unsigned long pc, npc, tstate;
	unsigned long fp, i7;
	unsigned char fenab;
	int err;

	flush_user_windows();
	if (get_thread_wsaved()					||
	    (((unsigned long)ucp) & (sizeof(unsigned long)-1))	||
	    (!__access_ok(ucp, sizeof(*ucp))))
		goto do_sigsegv;
	grp  = &ucp->uc_mcontext.mc_gregs;
	err  = __get_user(pc, &((*grp)[MC_PC]));
	err |= __get_user(npc, &((*grp)[MC_NPC]));
	if (err || ((pc | npc) & 3))
		goto do_sigsegv;
	if (regs->u_regs[UREG_I1]) {
		sigset_t set;

		if (_NSIG_WORDS == 1) {
			if (__get_user(set.sig[0], &ucp->uc_sigmask.sig[0]))
				goto do_sigsegv;
		} else {
			if (__copy_from_user(&set, &ucp->uc_sigmask, sizeof(sigset_t)))
				goto do_sigsegv;
		}
		set_current_blocked(&set);
	}
	if (test_thread_flag(TIF_32BIT)) {
		pc &= 0xffffffff;
		npc &= 0xffffffff;
	}
	regs->tpc = pc;
	regs->tnpc = npc;
	err |= __get_user(regs->y, &((*grp)[MC_Y]));
	err |= __get_user(tstate, &((*grp)[MC_TSTATE]));
	regs->tstate &= ~(TSTATE_ASI | TSTATE_ICC | TSTATE_XCC);
	regs->tstate |= (tstate & (TSTATE_ASI | TSTATE_ICC | TSTATE_XCC));
	err |= __get_user(regs->u_regs[UREG_G1], (&(*grp)[MC_G1]));
	err |= __get_user(regs->u_regs[UREG_G2], (&(*grp)[MC_G2]));
	err |= __get_user(regs->u_regs[UREG_G3], (&(*grp)[MC_G3]));
	err |= __get_user(regs->u_regs[UREG_G4], (&(*grp)[MC_G4]));
	err |= __get_user(regs->u_regs[UREG_G5], (&(*grp)[MC_G5]));
	err |= __get_user(regs->u_regs[UREG_G6], (&(*grp)[MC_G6]));

	/* Skip %g7 as that's the thread register in userspace.  */

	err |= __get_user(regs->u_regs[UREG_I0], (&(*grp)[MC_O0]));
	err |= __get_user(regs->u_regs[UREG_I1], (&(*grp)[MC_O1]));
	err |= __get_user(regs->u_regs[UREG_I2], (&(*grp)[MC_O2]));
	err |= __get_user(regs->u_regs[UREG_I3], (&(*grp)[MC_O3]));
	err |= __get_user(regs->u_regs[UREG_I4], (&(*grp)[MC_O4]));
	err |= __get_user(regs->u_regs[UREG_I5], (&(*grp)[MC_O5]));
	err |= __get_user(regs->u_regs[UREG_I6], (&(*grp)[MC_O6]));
	err |= __get_user(regs->u_regs[UREG_I7], (&(*grp)[MC_O7]));

	err |= __get_user(fp, &(ucp->uc_mcontext.mc_fp));
	err |= __get_user(i7, &(ucp->uc_mcontext.mc_i7));
	err |= __put_user(fp,
	      (&(((struct reg_window __user *)(STACK_BIAS+regs->u_regs[UREG_I6]))->ins[6])));
	err |= __put_user(i7,
	      (&(((struct reg_window __user *)(STACK_BIAS+regs->u_regs[UREG_I6]))->ins[7])));

	err |= __get_user(fenab, &(ucp->uc_mcontext.mc_fpregs.mcfpu_enab));
	if (fenab) {
		unsigned long *fpregs = current_thread_info()->fpregs;
		unsigned long fprs;
		
		fprs_write(0);
		err |= __get_user(fprs, &(ucp->uc_mcontext.mc_fpregs.mcfpu_fprs));
		if (fprs & FPRS_DL)
			err |= copy_from_user(fpregs,
					      &(ucp->uc_mcontext.mc_fpregs.mcfpu_fregs),
					      (sizeof(unsigned int) * 32));
		if (fprs & FPRS_DU)
			err |= copy_from_user(fpregs+16,
			 ((unsigned long __user *)&(ucp->uc_mcontext.mc_fpregs.mcfpu_fregs))+16,
			 (sizeof(unsigned int) * 32));
		err |= __get_user(current_thread_info()->xfsr[0],
				  &(ucp->uc_mcontext.mc_fpregs.mcfpu_fsr));
		err |= __get_user(current_thread_info()->gsr[0],
				  &(ucp->uc_mcontext.mc_fpregs.mcfpu_gsr));
		regs->tstate &= ~TSTATE_PEF;
	}
	if (err)
		goto do_sigsegv;

	return;
do_sigsegv:
	force_sig(SIGSEGV, current);
}

asmlinkage void sparc64_get_context(struct pt_regs *regs)
{
	struct ucontext __user *ucp = (struct ucontext __user *)
		regs->u_regs[UREG_I0];
	mc_gregset_t __user *grp;
	mcontext_t __user *mcp;
	unsigned long fp, i7;
	unsigned char fenab;
	int err;

	synchronize_user_stack();
	if (get_thread_wsaved() || clear_user(ucp, sizeof(*ucp)))
		goto do_sigsegv;

#if 1
	fenab = 0; /* IMO get_context is like any other system call, thus modifies FPU state -jj */
#else
	fenab = (current_thread_info()->fpsaved[0] & FPRS_FEF);
#endif
		
	mcp = &ucp->uc_mcontext;
	grp = &mcp->mc_gregs;

	/* Skip over the trap instruction, first. */
	if (test_thread_flag(TIF_32BIT)) {
		regs->tpc   = (regs->tnpc & 0xffffffff);
		regs->tnpc  = (regs->tnpc + 4) & 0xffffffff;
	} else {
		regs->tpc   = regs->tnpc;
		regs->tnpc += 4;
	}
	err = 0;
	if (_NSIG_WORDS == 1)
		err |= __put_user(current->blocked.sig[0],
				  (unsigned long __user *)&ucp->uc_sigmask);
	else
		err |= __copy_to_user(&ucp->uc_sigmask, &current->blocked,
				      sizeof(sigset_t));

	err |= __put_user(regs->tstate, &((*grp)[MC_TSTATE]));
	err |= __put_user(regs->tpc, &((*grp)[MC_PC]));
	err |= __put_user(regs->tnpc, &((*grp)[MC_NPC]));
	err |= __put_user(regs->y, &((*grp)[MC_Y]));
	err |= __put_user(regs->u_regs[UREG_G1], &((*grp)[MC_G1]));
	err |= __put_user(regs->u_regs[UREG_G2], &((*grp)[MC_G2]));
	err |= __put_user(regs->u_regs[UREG_G3], &((*grp)[MC_G3]));
	err |= __put_user(regs->u_regs[UREG_G4], &((*grp)[MC_G4]));
	err |= __put_user(regs->u_regs[UREG_G5], &((*grp)[MC_G5]));
	err |= __put_user(regs->u_regs[UREG_G6], &((*grp)[MC_G6]));
	err |= __put_user(regs->u_regs[UREG_G7], &((*grp)[MC_G7]));
	err |= __put_user(regs->u_regs[UREG_I0], &((*grp)[MC_O0]));
	err |= __put_user(regs->u_regs[UREG_I1], &((*grp)[MC_O1]));
	err |= __put_user(regs->u_regs[UREG_I2], &((*grp)[MC_O2]));
	err |= __put_user(regs->u_regs[UREG_I3], &((*grp)[MC_O3]));
	err |= __put_user(regs->u_regs[UREG_I4], &((*grp)[MC_O4]));
	err |= __put_user(regs->u_regs[UREG_I5], &((*grp)[MC_O5]));
	err |= __put_user(regs->u_regs[UREG_I6], &((*grp)[MC_O6]));
	err |= __put_user(regs->u_regs[UREG_I7], &((*grp)[MC_O7]));

	err |= __get_user(fp,
		 (&(((struct reg_window __user *)(STACK_BIAS+regs->u_regs[UREG_I6]))->ins[6])));
	err |= __get_user(i7,
		 (&(((struct reg_window __user *)(STACK_BIAS+regs->u_regs[UREG_I6]))->ins[7])));
	err |= __put_user(fp, &(mcp->mc_fp));
	err |= __put_user(i7, &(mcp->mc_i7));

	err |= __put_user(fenab, &(mcp->mc_fpregs.mcfpu_enab));
	if (fenab) {
		unsigned long *fpregs = current_thread_info()->fpregs;
		unsigned long fprs;
		
		fprs = current_thread_info()->fpsaved[0];
		if (fprs & FPRS_DL)
			err |= copy_to_user(&(mcp->mc_fpregs.mcfpu_fregs), fpregs,
					    (sizeof(unsigned int) * 32));
		if (fprs & FPRS_DU)
			err |= copy_to_user(
                          ((unsigned long __user *)&(mcp->mc_fpregs.mcfpu_fregs))+16, fpregs+16,
			  (sizeof(unsigned int) * 32));
		err |= __put_user(current_thread_info()->xfsr[0], &(mcp->mc_fpregs.mcfpu_fsr));
		err |= __put_user(current_thread_info()->gsr[0], &(mcp->mc_fpregs.mcfpu_gsr));
		err |= __put_user(fprs, &(mcp->mc_fpregs.mcfpu_fprs));
	}
	if (err)
		goto do_sigsegv;

	return;
do_sigsegv:
	force_sig(SIGSEGV, current);
}

struct rt_signal_frame {
	struct sparc_stackf	ss;
	siginfo_t		info;
	struct pt_regs		regs;
	__siginfo_fpu_t __user	*fpu_save;
	stack_t			stack;
	sigset_t		mask;
	__siginfo_rwin_t	*rwin_save;
};

static long _sigpause_common(old_sigset_t set)
{
	sigset_t blocked;
	siginitset(&blocked, set);
	return sigsuspend(&blocked);
}

asmlinkage long sys_sigpause(unsigned int set)
{
	return _sigpause_common(set);
}

asmlinkage long sys_sigsuspend(old_sigset_t set)
{
	return _sigpause_common(set);
}

void do_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_signal_frame __user *sf;
	unsigned long tpc, tnpc, tstate;
	__siginfo_fpu_t __user *fpu_save;
	__siginfo_rwin_t __user *rwin_save;
	sigset_t set;
	int err;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	synchronize_user_stack ();
	sf = (struct rt_signal_frame __user *)
		(regs->u_regs [UREG_FP] + STACK_BIAS);

	/* 1. Make sure we are not getting garbage from the user */
	if (((unsigned long) sf) & 3)
		goto segv;

	err = get_user(tpc, &sf->regs.tpc);
	err |= __get_user(tnpc, &sf->regs.tnpc);
	if (test_thread_flag(TIF_32BIT)) {
		tpc &= 0xffffffff;
		tnpc &= 0xffffffff;
	}
	err |= ((tpc | tnpc) & 3);

	/* 2. Restore the state */
	err |= __get_user(regs->y, &sf->regs.y);
	err |= __get_user(tstate, &sf->regs.tstate);
	err |= copy_from_user(regs->u_regs, sf->regs.u_regs, sizeof(regs->u_regs));

	/* User can only change condition codes and %asi in %tstate. */
	regs->tstate &= ~(TSTATE_ASI | TSTATE_ICC | TSTATE_XCC);
	regs->tstate |= (tstate & (TSTATE_ASI | TSTATE_ICC | TSTATE_XCC));

	err |= __get_user(fpu_save, &sf->fpu_save);
	if (!err && fpu_save)
		err |= restore_fpu_state(regs, fpu_save);

	err |= __copy_from_user(&set, &sf->mask, sizeof(sigset_t));
	err |= do_sigaltstack(&sf->stack, NULL, (unsigned long)sf);

	if (err)
		goto segv;

	err |= __get_user(rwin_save, &sf->rwin_save);
	if (!err && rwin_save) {
		if (restore_rwin_state(rwin_save))
			goto segv;
	}

	regs->tpc = tpc;
	regs->tnpc = tnpc;

	/* Prevent syscall restart.  */
	pt_regs_clear_syscall(regs);

	set_current_blocked(&set);
	return;
segv:
	force_sig(SIGSEGV, current);
}

/* Checks if the fp is valid */
static int invalid_frame_pointer(void __user *fp)
{
	if (((unsigned long) fp) & 15)
		return 1;
	return 0;
}

static inline void __user *get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, unsigned long framesize)
{
	unsigned long sp = regs->u_regs[UREG_FP] + STACK_BIAS;

	/*
	 * If we are on the alternate signal stack and would overflow it, don't.
	 * Return an always-bogus address instead so we will die with SIGSEGV.
	 */
	if (on_sig_stack(sp) && !likely(on_sig_stack(sp - framesize)))
		return (void __user *) -1L;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (sas_ss_flags(sp) == 0)
			sp = current->sas_ss_sp + current->sas_ss_size;
	}

	sp -= framesize;

	/* Always align the stack frame.  This handles two cases.  First,
	 * sigaltstack need not be mindful of platform specific stack
	 * alignment.  Second, if we took this signal because the stack
	 * is not aligned properly, we'd like to take the signal cleanly
	 * and report that.
	 */
	sp &= ~15UL;

	return (void __user *) sp;
}

static inline int
setup_rt_frame(struct k_sigaction *ka, struct pt_regs *regs,
	       int signo, sigset_t *oldset, siginfo_t *info)
{
	struct rt_signal_frame __user *sf;
	int wsaved, err, sf_size;
	void __user *tail;

	/* 1. Make sure everything is clean */
	synchronize_user_stack();
	save_and_clear_fpu();
	
	wsaved = get_thread_wsaved();

	sf_size = sizeof(struct rt_signal_frame);
	if (current_thread_info()->fpsaved[0] & FPRS_FEF)
		sf_size += sizeof(__siginfo_fpu_t);
	if (wsaved)
		sf_size += sizeof(__siginfo_rwin_t);
	sf = (struct rt_signal_frame __user *)
		get_sigframe(ka, regs, sf_size);

	if (invalid_frame_pointer (sf))
		goto sigill;

	tail = (sf + 1);

	/* 2. Save the current process state */
	err = copy_to_user(&sf->regs, regs, sizeof (*regs));

	if (current_thread_info()->fpsaved[0] & FPRS_FEF) {
		__siginfo_fpu_t __user *fpu_save = tail;
		tail += sizeof(__siginfo_fpu_t);
		err |= save_fpu_state(regs, fpu_save);
		err |= __put_user((u64)fpu_save, &sf->fpu_save);
	} else {
		err |= __put_user(0, &sf->fpu_save);
	}
	if (wsaved) {
		__siginfo_rwin_t __user *rwin_save = tail;
		tail += sizeof(__siginfo_rwin_t);
		err |= save_rwin_state(wsaved, rwin_save);
		err |= __put_user((u64)rwin_save, &sf->rwin_save);
		set_thread_wsaved(0);
	} else {
		err |= __put_user(0, &sf->rwin_save);
	}
	
	/* Setup sigaltstack */
	err |= __put_user(current->sas_ss_sp, &sf->stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->u_regs[UREG_FP]), &sf->stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &sf->stack.ss_size);

	err |= copy_to_user(&sf->mask, oldset, sizeof(sigset_t));

	if (!wsaved) {
		err |= copy_in_user((u64 __user *)sf,
				    (u64 __user *)(regs->u_regs[UREG_FP] +
						   STACK_BIAS),
				    sizeof(struct reg_window));
	} else {
		struct reg_window *rp;

		rp = &current_thread_info()->reg_window[wsaved - 1];
		err |= copy_to_user(sf, rp, sizeof(struct reg_window));
	}
	if (info)
		err |= copy_siginfo_to_user(&sf->info, info);
	else {
		err |= __put_user(signo, &sf->info.si_signo);
		err |= __put_user(SI_NOINFO, &sf->info.si_code);
	}
	if (err)
		goto sigsegv;
	
	/* 3. signal handler back-trampoline and parameters */
	regs->u_regs[UREG_FP] = ((unsigned long) sf) - STACK_BIAS;
	regs->u_regs[UREG_I0] = signo;
	regs->u_regs[UREG_I1] = (unsigned long) &sf->info;

	/* The sigcontext is passed in this way because of how it
	 * is defined in GLIBC's /usr/include/bits/sigcontext.h
	 * for sparc64.  It includes the 128 bytes of siginfo_t.
	 */
	regs->u_regs[UREG_I2] = (unsigned long) &sf->info;

	/* 5. signal handler */
	regs->tpc = (unsigned long) ka->sa.sa_handler;
	regs->tnpc = (regs->tpc + 4);
	if (test_thread_flag(TIF_32BIT)) {
		regs->tpc &= 0xffffffff;
		regs->tnpc &= 0xffffffff;
	}
	/* 4. return to kernel instructions */
	regs->u_regs[UREG_I7] = (unsigned long)ka->ka_restorer;
	return 0;

sigill:
	do_exit(SIGILL);
	return -EINVAL;

sigsegv:
	force_sigsegv(signo, current);
	return -EFAULT;
}

static inline void handle_signal(unsigned long signr, struct k_sigaction *ka,
				siginfo_t *info,
				sigset_t *oldset, struct pt_regs *regs)
{
	int err;

	err = setup_rt_frame(ka, regs, signr, oldset,
			     (ka->sa.sa_flags & SA_SIGINFO) ? info : NULL);
	if (err)
		return;

	signal_delivered(signr, info, ka, regs, 0);
}

static inline void syscall_restart(unsigned long orig_i0, struct pt_regs *regs,
				   struct sigaction *sa)
{
	switch (regs->u_regs[UREG_I0]) {
	case ERESTART_RESTARTBLOCK:
	case ERESTARTNOHAND:
	no_system_call_restart:
		regs->u_regs[UREG_I0] = EINTR;
		regs->tstate |= (TSTATE_ICARRY|TSTATE_XCARRY);
		break;
	case ERESTARTSYS:
		if (!(sa->sa_flags & SA_RESTART))
			goto no_system_call_restart;
		/* fallthrough */
	case ERESTARTNOINTR:
		regs->u_regs[UREG_I0] = orig_i0;
		regs->tpc -= 4;
		regs->tnpc -= 4;
	}
}

/* Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
static void do_signal(struct pt_regs *regs, unsigned long orig_i0)
{
	struct k_sigaction ka;
	int restart_syscall;
	sigset_t *oldset = sigmask_to_save();
	siginfo_t info;
	int signr;
	
	/* It's a lot of work and synchronization to add a new ptrace
	 * register for GDB to save and restore in order to get
	 * orig_i0 correct for syscall restarts when debugging.
	 *
	 * Although it should be the case that most of the global
	 * registers are volatile across a system call, glibc already
	 * depends upon that fact that we preserve them.  So we can't
	 * just use any global register to save away the orig_i0 value.
	 *
	 * In particular %g2, %g3, %g4, and %g5 are all assumed to be
	 * preserved across a system call trap by various pieces of
	 * code in glibc.
	 *
	 * %g7 is used as the "thread register".   %g6 is not used in
	 * any fixed manner.  %g6 is used as a scratch register and
	 * a compiler temporary, but it's value is never used across
	 * a system call.  Therefore %g6 is usable for orig_i0 storage.
	 */
	if (pt_regs_is_syscall(regs) &&
	    (regs->tstate & (TSTATE_XCARRY | TSTATE_ICARRY)))
		regs->u_regs[UREG_G6] = orig_i0;

#ifdef CONFIG_COMPAT
	if (test_thread_flag(TIF_32BIT)) {
		extern void do_signal32(sigset_t *, struct pt_regs *);
		do_signal32(oldset, regs);
		return;
	}
#endif	

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);

	restart_syscall = 0;
	if (pt_regs_is_syscall(regs) &&
	    (regs->tstate & (TSTATE_XCARRY | TSTATE_ICARRY))) {
		restart_syscall = 1;
		orig_i0 = regs->u_regs[UREG_G6];
	}

	if (signr > 0) {
		if (restart_syscall)
			syscall_restart(orig_i0, regs, &ka.sa);
		handle_signal(signr, &ka, &info, oldset, regs);
		return;
	}
	if (restart_syscall &&
	    (regs->u_regs[UREG_I0] == ERESTARTNOHAND ||
	     regs->u_regs[UREG_I0] == ERESTARTSYS ||
	     regs->u_regs[UREG_I0] == ERESTARTNOINTR)) {
		/* replay the system call when we are done */
		regs->u_regs[UREG_I0] = orig_i0;
		regs->tpc -= 4;
		regs->tnpc -= 4;
		pt_regs_clear_syscall(regs);
	}
	if (restart_syscall &&
	    regs->u_regs[UREG_I0] == ERESTART_RESTARTBLOCK) {
		regs->u_regs[UREG_G1] = __NR_restart_syscall;
		regs->tpc -= 4;
		regs->tnpc -= 4;
		pt_regs_clear_syscall(regs);
	}

	/* If there's no signal to deliver, we just put the saved sigmask
	 * back
	 */
	restore_saved_sigmask();
}

void do_notify_resume(struct pt_regs *regs, unsigned long orig_i0, unsigned long thread_info_flags)
{
	if (thread_info_flags & _TIF_SIGPENDING)
		do_signal(regs, orig_i0);
	if (thread_info_flags & _TIF_NOTIFY_RESUME) {
		clear_thread_flag(TIF_NOTIFY_RESUME);
		tracehook_notify_resume(regs);
	}
}

