// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/alpha/kernel/signal.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  1997-11-02  Modified for POSIX.1b signals by Richard Henderson
 */

#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/tty.h>
#include <linux/binfmts.h>
#include <linux/bitops.h>
#include <linux/syscalls.h>
#include <linux/resume_user_mode.h>

#include <linux/uaccess.h>
#include <asm/sigcontext.h>
#include <asm/ucontext.h>
#include <linux/entry-common.h>
#include "proto.h"

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

asmlinkage void ret_from_sys_call(void);

/*
 * The OSF/1 sigprocmask calling sequence is different from the
 * C sigprocmask() sequence..
 */
SYSCALL_DEFINE2(osf_sigprocmask, int, how, unsigned long, newmask)
{
	sigset_t oldmask;
	sigset_t mask;
	unsigned long res;

	siginitset(&mask, newmask & _BLOCKABLE);
	res = sigprocmask(how, &mask, &oldmask);
	if (!res) {
		force_successful_syscall_return();
		res = oldmask.sig[0];
	}
	return res;
}

SYSCALL_DEFINE3(osf_sigaction, int, sig,
		const struct osf_sigaction __user *, act,
		struct osf_sigaction __user *, oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (!access_ok(act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags) ||
		    __get_user(mask, &act->sa_mask))
			return -EFAULT;
		siginitset(&new_ka.sa.sa_mask, mask);
		new_ka.ka_restorer = NULL;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_flags, &oact->sa_flags) ||
		    __put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask))
			return -EFAULT;
	}

	return ret;
}

SYSCALL_DEFINE5(rt_sigaction, int, sig, const struct sigaction __user *, act,
		struct sigaction __user *, oact,
		size_t, sigsetsize, void __user *, restorer)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (act) {
		new_ka.ka_restorer = restorer;
		if (copy_from_user(&new_ka.sa, act, sizeof(*act)))
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (copy_to_user(oact, &old_ka.sa, sizeof(*oact)))
			return -EFAULT;
	}

	return ret;
}

/*
 * Do a signal return; undo the signal stack.
 */

#if _NSIG_WORDS > 1
# error "Non SA_SIGINFO frame needs rearranging"
#endif

struct sigframe
{
	struct sigcontext sc;
	unsigned int retcode[3];
};

struct rt_sigframe
{
	struct siginfo info;
	struct ucontext uc;
	unsigned int retcode[3];
};

/* If this changes, userland unwinders that Know Things about our signal
   frame will break.  Do not undertake lightly.  It also implies an ABI
   change wrt the size of siginfo_t, which may cause some pain.  */
extern char compile_time_assert
        [offsetof(struct rt_sigframe, uc.uc_mcontext) == 176 ? 1 : -1];

#define INSN_MOV_R30_R16	0x47fe0410
#define INSN_LDI_R0		0x201f0000
#define INSN_CALLSYS		0x00000083

static long
restore_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs)
{
	unsigned long usp;
	struct switch_stack *sw = (struct switch_stack *)regs - 1;
	long err = __get_user(regs->pc, &sc->sc_pc);

	current->restart_block.fn = do_no_restart_syscall;
	current_thread_info()->status |= TS_SAVED_FP | TS_RESTORE_FP;

	sw->r26 = (unsigned long) ret_from_sys_call;

	err |= __get_user(regs->r0, sc->sc_regs+0);
	err |= __get_user(regs->r1, sc->sc_regs+1);
	err |= __get_user(regs->r2, sc->sc_regs+2);
	err |= __get_user(regs->r3, sc->sc_regs+3);
	err |= __get_user(regs->r4, sc->sc_regs+4);
	err |= __get_user(regs->r5, sc->sc_regs+5);
	err |= __get_user(regs->r6, sc->sc_regs+6);
	err |= __get_user(regs->r7, sc->sc_regs+7);
	err |= __get_user(regs->r8, sc->sc_regs+8);
	err |= __get_user(sw->r9, sc->sc_regs+9);
	err |= __get_user(sw->r10, sc->sc_regs+10);
	err |= __get_user(sw->r11, sc->sc_regs+11);
	err |= __get_user(sw->r12, sc->sc_regs+12);
	err |= __get_user(sw->r13, sc->sc_regs+13);
	err |= __get_user(sw->r14, sc->sc_regs+14);
	err |= __get_user(sw->r15, sc->sc_regs+15);
	err |= __get_user(regs->r16, sc->sc_regs+16);
	err |= __get_user(regs->r17, sc->sc_regs+17);
	err |= __get_user(regs->r18, sc->sc_regs+18);
	err |= __get_user(regs->r19, sc->sc_regs+19);
	err |= __get_user(regs->r20, sc->sc_regs+20);
	err |= __get_user(regs->r21, sc->sc_regs+21);
	err |= __get_user(regs->r22, sc->sc_regs+22);
	err |= __get_user(regs->r23, sc->sc_regs+23);
	err |= __get_user(regs->r24, sc->sc_regs+24);
	err |= __get_user(regs->r25, sc->sc_regs+25);
	err |= __get_user(regs->r26, sc->sc_regs+26);
	err |= __get_user(regs->r27, sc->sc_regs+27);
	err |= __get_user(regs->r28, sc->sc_regs+28);
	err |= __get_user(regs->gp, sc->sc_regs+29);
	err |= __get_user(usp, sc->sc_regs+30);
	wrusp(usp);

	err |= __copy_from_user(current_thread_info()->fp,
				sc->sc_fpregs, 31 * 8);
	err |= __get_user(current_thread_info()->fp[31], &sc->sc_fpcr);

	return err;
}

/* Note that this syscall is also used by setcontext(3) to install
   a given sigcontext.  This because it's impossible to set *all*
   registers and transfer control from userland.  */

asmlinkage void
do_sigreturn(struct sigcontext __user *sc)
{
	struct pt_regs *regs = current_pt_regs();
	sigset_t set;

	/* Verify that it's a good sigcontext before using it */
	if (!access_ok(sc, sizeof(*sc)))
		goto give_sigsegv;
	if (__get_user(set.sig[0], &sc->sc_mask))
		goto give_sigsegv;

	set_current_blocked(&set);

	if (restore_sigcontext(sc, regs))
		goto give_sigsegv;

	/* Send SIGTRAP if we're single-stepping: */
	if (ptrace_cancel_bpt (current)) {
		send_sig_fault(SIGTRAP, TRAP_BRKPT, (void __user *) regs->pc,
			       current);
	}
	return;

give_sigsegv:
	force_sig(SIGSEGV);
}

asmlinkage void
do_rt_sigreturn(struct rt_sigframe __user *frame)
{
	struct pt_regs *regs = current_pt_regs();
	sigset_t set;

	/* Verify that it's a good ucontext_t before using it */
	if (!access_ok(&frame->uc, sizeof(frame->uc)))
		goto give_sigsegv;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto give_sigsegv;

	set_current_blocked(&set);

	if (restore_sigcontext(&frame->uc.uc_mcontext, regs))
		goto give_sigsegv;

	/* Send SIGTRAP if we're single-stepping: */
	if (ptrace_cancel_bpt (current)) {
		send_sig_fault(SIGTRAP, TRAP_BRKPT, (void __user *) regs->pc,
			       current);
	}
	return;

give_sigsegv:
	force_sig(SIGSEGV);
}


/*
 * Set up a signal frame.
 */

static inline void __user *
get_sigframe(struct ksignal *ksig, unsigned long sp, size_t frame_size)
{
	return (void __user *)((sigsp(sp, ksig) - frame_size) & -32ul);
}

static long
setup_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs, 
		 unsigned long mask, unsigned long sp)
{
	struct switch_stack *sw = (struct switch_stack *)regs - 1;
	long err = 0;

	err |= __put_user(on_sig_stack((unsigned long)sc), &sc->sc_onstack);
	err |= __put_user(mask, &sc->sc_mask);
	err |= __put_user(regs->pc, &sc->sc_pc);
	err |= __put_user(8, &sc->sc_ps);

	err |= __put_user(regs->r0 , sc->sc_regs+0);
	err |= __put_user(regs->r1 , sc->sc_regs+1);
	err |= __put_user(regs->r2 , sc->sc_regs+2);
	err |= __put_user(regs->r3 , sc->sc_regs+3);
	err |= __put_user(regs->r4 , sc->sc_regs+4);
	err |= __put_user(regs->r5 , sc->sc_regs+5);
	err |= __put_user(regs->r6 , sc->sc_regs+6);
	err |= __put_user(regs->r7 , sc->sc_regs+7);
	err |= __put_user(regs->r8 , sc->sc_regs+8);
	err |= __put_user(sw->r9   , sc->sc_regs+9);
	err |= __put_user(sw->r10  , sc->sc_regs+10);
	err |= __put_user(sw->r11  , sc->sc_regs+11);
	err |= __put_user(sw->r12  , sc->sc_regs+12);
	err |= __put_user(sw->r13  , sc->sc_regs+13);
	err |= __put_user(sw->r14  , sc->sc_regs+14);
	err |= __put_user(sw->r15  , sc->sc_regs+15);
	err |= __put_user(regs->r16, sc->sc_regs+16);
	err |= __put_user(regs->r17, sc->sc_regs+17);
	err |= __put_user(regs->r18, sc->sc_regs+18);
	err |= __put_user(regs->r19, sc->sc_regs+19);
	err |= __put_user(regs->r20, sc->sc_regs+20);
	err |= __put_user(regs->r21, sc->sc_regs+21);
	err |= __put_user(regs->r22, sc->sc_regs+22);
	err |= __put_user(regs->r23, sc->sc_regs+23);
	err |= __put_user(regs->r24, sc->sc_regs+24);
	err |= __put_user(regs->r25, sc->sc_regs+25);
	err |= __put_user(regs->r26, sc->sc_regs+26);
	err |= __put_user(regs->r27, sc->sc_regs+27);
	err |= __put_user(regs->r28, sc->sc_regs+28);
	err |= __put_user(regs->gp , sc->sc_regs+29);
	err |= __put_user(sp, sc->sc_regs+30);
	err |= __put_user(0, sc->sc_regs+31);

	err |= __copy_to_user(sc->sc_fpregs,
			      current_thread_info()->fp, 31 * 8);
	err |= __put_user(0, sc->sc_fpregs+31);
	err |= __put_user(current_thread_info()->fp[31], &sc->sc_fpcr);

	err |= __put_user(regs->trap_a0, &sc->sc_traparg_a0);
	err |= __put_user(regs->trap_a1, &sc->sc_traparg_a1);
	err |= __put_user(regs->trap_a2, &sc->sc_traparg_a2);

	return err;
}

static int
setup_frame(struct ksignal *ksig, sigset_t *set, struct pt_regs *regs)
{
	unsigned long oldsp, r26, err = 0;
	struct sigframe __user *frame;

	oldsp = rdusp();
	frame = get_sigframe(ksig, oldsp, sizeof(*frame));
	if (!access_ok(frame, sizeof(*frame)))
		return -EFAULT;

	err |= setup_sigcontext(&frame->sc, regs, set->sig[0], oldsp);
	if (err)
		return -EFAULT;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	r26 = (unsigned long) ksig->ka.ka_restorer;
	if (!r26) {
		err |= __put_user(INSN_MOV_R30_R16, frame->retcode+0);
		err |= __put_user(INSN_LDI_R0+__NR_sigreturn, frame->retcode+1);
		err |= __put_user(INSN_CALLSYS, frame->retcode+2);
		imb();
		r26 = (unsigned long) frame->retcode;
	}

	/* Check that everything was written properly.  */
	if (err)
		return err;

	/* "Return" to the handler */
	regs->r26 = r26;
	regs->r27 = regs->pc = (unsigned long) ksig->ka.sa.sa_handler;
	regs->r16 = ksig->sig;			/* a0: signal number */
	regs->r17 = 0;				/* a1: exception code */
	regs->r18 = (unsigned long) &frame->sc;	/* a2: sigcontext pointer */
	wrusp((unsigned long) frame);
	
#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%p ra=%p\n",
		current->comm, current->pid, frame, regs->pc, regs->r26);
#endif
	return 0;
}

static int
setup_rt_frame(struct ksignal *ksig, sigset_t *set, struct pt_regs *regs)
{
	unsigned long oldsp, r26, err = 0;
	struct rt_sigframe __user *frame;

	oldsp = rdusp();
	frame = get_sigframe(ksig, oldsp, sizeof(*frame));
	if (!access_ok(frame, sizeof(*frame)))
		return -EFAULT;

	err |= copy_siginfo_to_user(&frame->info, &ksig->info);

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(set->sig[0], &frame->uc.uc_osf_sigmask);
	err |= __save_altstack(&frame->uc.uc_stack, oldsp);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, regs, 
				set->sig[0], oldsp);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		return -EFAULT;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	r26 = (unsigned long) ksig->ka.ka_restorer;
	if (!r26) {
		err |= __put_user(INSN_MOV_R30_R16, frame->retcode+0);
		err |= __put_user(INSN_LDI_R0+__NR_rt_sigreturn,
				  frame->retcode+1);
		err |= __put_user(INSN_CALLSYS, frame->retcode+2);
		imb();
		r26 = (unsigned long) frame->retcode;
	}

	if (err)
		return -EFAULT;

	/* "Return" to the handler */
	regs->r26 = r26;
	regs->r27 = regs->pc = (unsigned long) ksig->ka.sa.sa_handler;
	regs->r16 = ksig->sig;			  /* a0: signal number */
	regs->r17 = (unsigned long) &frame->info; /* a1: siginfo pointer */
	regs->r18 = (unsigned long) &frame->uc;	  /* a2: ucontext pointer */
	wrusp((unsigned long) frame);

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%p ra=%p\n",
		current->comm, current->pid, frame, regs->pc, regs->r26);
#endif

	return 0;
}


/*
 * OK, we're invoking a handler.
 */
static inline void
handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
	int ret;

	if (ksig->ka.sa.sa_flags & SA_SIGINFO)
		ret = setup_rt_frame(ksig, oldset, regs);
	else
		ret = setup_frame(ksig, oldset, regs);

	signal_setup_done(ret, ksig, 0);
}

static inline void
syscall_restart(unsigned long r0, unsigned long r19,
		struct pt_regs *regs, struct k_sigaction *ka)
{
	switch (regs->r0) {
	case ERESTARTSYS:
		if (!(ka->sa.sa_flags & SA_RESTART)) {
		case ERESTARTNOHAND:
			regs->r0 = EINTR;
			break;
		}
		fallthrough;
	case ERESTARTNOINTR:
		regs->r0 = r0;	/* reset v0 and a3 and replay syscall */
		regs->r1 = r0;
		regs->r19 = r19;
		regs->pc -= 4;
		break;
	case ERESTART_RESTARTBLOCK:
		regs->r0 = EINTR;
		break;
	}
}


/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals that
 * the kernel can handle, and then we build all the user-level signal handling
 * stack-frames in one go after that.
 *
 * "r0" and "r19" are the registers we need to restore for system call
 * restart. "r0" is also used as an indicator whether we can restart at
 * all (if we get here from anything but a syscall return, it will be 0)
 */
void
do_signal(struct pt_regs *regs, unsigned long r0, unsigned long r19)
{
	unsigned long single_stepping = ptrace_cancel_bpt(current);
	struct ksignal ksig;

	/* This lets the debugger run, ... */
	if (get_signal(&ksig)) {
		/* ... so re-check the single stepping. */
		single_stepping |= ptrace_cancel_bpt(current);
		/* Whee!  Actually deliver the signal.  */
		if (r0)
			syscall_restart(r0, r19, regs, &ksig.ka);
		handle_signal(&ksig, regs);
	} else {
		single_stepping |= ptrace_cancel_bpt(current);
		if (r0) {
			switch (regs->r0) {
			case ERESTARTNOHAND:
			case ERESTARTSYS:
			case ERESTARTNOINTR:
				/* Reset v0 and a3 and replay syscall.  */
				regs->r0 = r0;
				regs->r1 = r0;
				regs->r19 = r19;
				regs->pc -= 4;
				break;
			case ERESTART_RESTARTBLOCK:
				/* Set v0 to the restart_syscall and replay */
				regs->r0 = __NR_restart_syscall;
				regs->r1 = __NR_restart_syscall;
				regs->pc -= 4;
				break;
			}
		}
		restore_saved_sigmask();
	}
	if (single_stepping)
		ptrace_set_bpt(current);	/* re-set breakpoint */
}

asmlinkage void alpha_exit_to_user_mode(struct pt_regs *regs)
{
	local_irq_disable();
	irqentry_exit_to_user_mode_prepare(regs);
	exit_to_user_mode();
}

/*
 * Syscall return reaches here after Alpha-specific r0/a3 result encoding.
 * Delegate syscall-exit work and final exit-to-user handling to generic
 * entry code; low-level PAL restore remains in assembly.
 */
asmlinkage void alpha_syscall_exit_to_user_mode(struct pt_regs *regs)
{
	syscall_exit_to_user_mode(regs);
}

void arch_do_signal_or_restart(struct pt_regs *regs)
{
	struct thread_info *ti = current_thread_info();

	preempt_disable();
	save_fpu();
	preempt_enable();
	do_signal(regs, ti->syscall_saved_nr, ti->syscall_saved_r19);
}

asmlinkage unsigned long
alpha_syscall_enter_select(struct pt_regs *regs, long syscall)
{
	struct thread_info *ti = current_thread_info();
	unsigned long saved_r0 = regs->r0;
	unsigned long saved_r19 = regs->r19;
	unsigned long fn = (unsigned long)sys_ni_syscall;
	long nr = syscall;
	bool permit;

	ti->syscall_meta = 0;
	ti->syscall_saved_nr = syscall;

	if (!(ti->status & TS_SAVED_FP)) {
		ti->status |= TS_SAVED_FP;
		__save_fpu();
	}

	permit = syscall_enter_from_user_mode_randomize_stack(regs, &nr);

	/*
	 * Alpha cannot preset the full -ENOSYS return state before generic
	 * entry handling, because r19 is still syscall argument 4 on the
	 * normal dispatch path. So provide the default -ENOSYS result only
	 * here, after we know no real syscall will be invoked, and only if
	 * entry work did not already change the return state.
	 */
	if (unlikely(!permit || (unsigned long)nr >= NR_syscalls)) {
		if (regs->r0 == saved_r0 && regs->r19 == saved_r19)
			syscall_set_return_value(current, regs, -ENOSYS, 0);

		ti->syscall_meta = ALPHA_SYSCALL_META_SKIP;
		return fn;	/* ignored by asm when SKIP is set */
	}

	instrumentation_begin();
	{
		unsigned long index;

		index = array_index_nospec((unsigned long)nr, NR_syscalls);
		fn = (unsigned long)sys_call_table[index];
	}
	instrumentation_end();

	return fn;
}

asmlinkage noinstr void
alpha_finish_syscall_to_user_mode(struct pt_regs *regs, long ret)
{
	struct thread_info *ti = current_thread_info();
	unsigned long meta = ti->syscall_meta;

	ti->syscall_meta = 0;
	ti->syscall_saved_r19 = regs->r19;

	instrumentation_begin();

	if (meta & ALPHA_SYSCALL_META_SKIP) {
	/*
	 * Skip-dispatch path: ptrace/seccomp may already have installed
	 * the return state in r0/r19. Preserve it unless the syscall was
	 * skipped with no explicit return value.
	 *
	 * Generic PTRACE_SET_SYSCALL_INFO changes only the syscall-number
	 * shadow, so r1 == -1 while r0 still contains the original Alpha
	 * syscall number. Legacy PTRACE_POKEUSR based skipping can leave
	 * r0 == -1 with a3/r19 still indicating success. Both represent
	 * an unhandled skipped syscall and should become ENOSYS/a3=1.
	 */
		if (regs->r1 == (unsigned long)-1 &&
		   (regs->r0 == ti->syscall_saved_nr ||
		    regs->r0 == (unsigned long)-1)) {
			regs->r0 = ENOSYS;
			regs->r19 = 1;
		}

		instrumentation_end();

		syscall_exit_to_user_mode(regs);
		return;
	}

	/*
	 * Some successful syscalls, notably legacy ptrace PEEK requests,
	 * return arbitrary data in r0.  That data may have the bit pattern
	 * of a negative errno, so do not infer failure from ret < 0 when
	 * arch code explicitly requested a successful Alpha return.
	 */
	if (meta & ALPHA_SYSCALL_META_FORCE_SUCCESS) {
		regs->r0 = ret;
		regs->r19 = 0;
	} else if (ret < 0) {
		regs->r0 = -ret;
		regs->r19 = 1;
	} else {
		regs->r0 = ret;
		regs->r19 = 0;
	}

	instrumentation_end();
	syscall_exit_to_user_mode(regs);
}
