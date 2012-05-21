/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
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
#include <linux/personality.h>
#include <linux/suspend.h>
#include <linux/ptrace.h>
#include <linux/elf.h>
#include <linux/compat.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <asm/processor.h>
#include <asm/ucontext.h>
#include <asm/sigframe.h>
#include <asm/syscalls.h>
#include <arch/interrupts.h>

struct compat_sigaction {
	compat_uptr_t sa_handler;
	compat_ulong_t sa_flags;
	compat_uptr_t sa_restorer;
	sigset_t sa_mask __packed;
};

struct compat_sigaltstack {
	compat_uptr_t ss_sp;
	int ss_flags;
	compat_size_t ss_size;
};

struct compat_ucontext {
	compat_ulong_t	  uc_flags;
	compat_uptr_t     uc_link;
	struct compat_sigaltstack	  uc_stack;
	struct sigcontext uc_mcontext;
	sigset_t	  uc_sigmask;	/* mask last for extensibility */
};

#define COMPAT_SI_PAD_SIZE	((SI_MAX_SIZE - 3 * sizeof(int)) / sizeof(int))

struct compat_siginfo {
	int si_signo;
	int si_errno;
	int si_code;

	union {
		int _pad[COMPAT_SI_PAD_SIZE];

		/* kill() */
		struct {
			unsigned int _pid;	/* sender's pid */
			unsigned int _uid;	/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			compat_timer_t _tid;	/* timer id */
			int _overrun;		/* overrun count */
			compat_sigval_t _sigval;	/* same as below */
			int _sys_private;	/* not to be passed to user */
			int _overrun_incr;	/* amount to add to overrun */
		} _timer;

		/* POSIX.1b signals */
		struct {
			unsigned int _pid;	/* sender's pid */
			unsigned int _uid;	/* sender's uid */
			compat_sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			unsigned int _pid;	/* which child */
			unsigned int _uid;	/* sender's uid */
			int _status;		/* exit code */
			compat_clock_t _utime;
			compat_clock_t _stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			unsigned int _addr;	/* faulting insn/memory ref. */
#ifdef __ARCH_SI_TRAPNO
			int _trapno;	/* TRAP # which caused the signal */
#endif
		} _sigfault;

		/* SIGPOLL */
		struct {
			int _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;
	} _sifields;
};

struct compat_rt_sigframe {
	unsigned char save_area[C_ABI_SAVE_AREA_SIZE]; /* caller save area */
	struct compat_siginfo info;
	struct compat_ucontext uc;
};

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

long compat_sys_rt_sigaction(int sig, struct compat_sigaction __user *act,
			     struct compat_sigaction __user *oact,
			     size_t sigsetsize)
{
	struct k_sigaction new_sa, old_sa;
	int ret = -EINVAL;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		goto out;

	if (act) {
		compat_uptr_t handler, restorer;

		if (!access_ok(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(handler, &act->sa_handler) ||
		    __get_user(new_sa.sa.sa_flags, &act->sa_flags) ||
		    __get_user(restorer, &act->sa_restorer) ||
		    __copy_from_user(&new_sa.sa.sa_mask, &act->sa_mask,
				     sizeof(sigset_t)))
			return -EFAULT;
		new_sa.sa.sa_handler = compat_ptr(handler);
		new_sa.sa.sa_restorer = compat_ptr(restorer);
	}

	ret = do_sigaction(sig, act ? &new_sa : NULL, oact ? &old_sa : NULL);

	if (!ret && oact) {
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(ptr_to_compat(old_sa.sa.sa_handler),
			       &oact->sa_handler) ||
		    __put_user(ptr_to_compat(old_sa.sa.sa_restorer),
			       &oact->sa_restorer) ||
		    __put_user(old_sa.sa.sa_flags, &oact->sa_flags) ||
		    __copy_to_user(&oact->sa_mask, &old_sa.sa.sa_mask,
				   sizeof(sigset_t)))
			return -EFAULT;
	}
out:
	return ret;
}

long compat_sys_rt_sigqueueinfo(int pid, int sig,
				struct compat_siginfo __user *uinfo)
{
	siginfo_t info;
	int ret;
	mm_segment_t old_fs = get_fs();

	if (copy_siginfo_from_user32(&info, uinfo))
		return -EFAULT;
	set_fs(KERNEL_DS);
	ret = sys_rt_sigqueueinfo(pid, sig, (siginfo_t __force __user *)&info);
	set_fs(old_fs);
	return ret;
}

int copy_siginfo_to_user32(struct compat_siginfo __user *to, siginfo_t *from)
{
	int err;

	if (!access_ok(VERIFY_WRITE, to, sizeof(struct compat_siginfo)))
		return -EFAULT;

	/* If you change siginfo_t structure, please make sure that
	   this code is fixed accordingly.
	   It should never copy any pad contained in the structure
	   to avoid security leaks, but must copy the generic
	   3 ints plus the relevant union member.  */
	err = __put_user(from->si_signo, &to->si_signo);
	err |= __put_user(from->si_errno, &to->si_errno);
	err |= __put_user((short)from->si_code, &to->si_code);

	if (from->si_code < 0) {
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		err |= __put_user(ptr_to_compat(from->si_ptr), &to->si_ptr);
	} else {
		/*
		 * First 32bits of unions are always present:
		 * si_pid === si_band === si_tid === si_addr(LS half)
		 */
		err |= __put_user(from->_sifields._pad[0],
				  &to->_sifields._pad[0]);
		switch (from->si_code >> 16) {
		case __SI_FAULT >> 16:
			break;
		case __SI_CHLD >> 16:
			err |= __put_user(from->si_utime, &to->si_utime);
			err |= __put_user(from->si_stime, &to->si_stime);
			err |= __put_user(from->si_status, &to->si_status);
			/* FALL THROUGH */
		default:
		case __SI_KILL >> 16:
			err |= __put_user(from->si_uid, &to->si_uid);
			break;
		case __SI_POLL >> 16:
			err |= __put_user(from->si_fd, &to->si_fd);
			break;
		case __SI_TIMER >> 16:
			err |= __put_user(from->si_overrun, &to->si_overrun);
			err |= __put_user(ptr_to_compat(from->si_ptr),
					  &to->si_ptr);
			break;
			 /* This is not generated by the kernel as of now.  */
		case __SI_RT >> 16:
		case __SI_MESGQ >> 16:
			err |= __put_user(from->si_uid, &to->si_uid);
			err |= __put_user(from->si_int, &to->si_int);
			break;
		}
	}
	return err;
}

int copy_siginfo_from_user32(siginfo_t *to, struct compat_siginfo __user *from)
{
	int err;
	u32 ptr32;

	if (!access_ok(VERIFY_READ, from, sizeof(struct compat_siginfo)))
		return -EFAULT;

	err = __get_user(to->si_signo, &from->si_signo);
	err |= __get_user(to->si_errno, &from->si_errno);
	err |= __get_user(to->si_code, &from->si_code);

	err |= __get_user(to->si_pid, &from->si_pid);
	err |= __get_user(to->si_uid, &from->si_uid);
	err |= __get_user(ptr32, &from->si_ptr);
	to->si_ptr = compat_ptr(ptr32);

	return err;
}

long compat_sys_sigaltstack(const struct compat_sigaltstack __user *uss_ptr,
			    struct compat_sigaltstack __user *uoss_ptr,
			    struct pt_regs *regs)
{
	stack_t uss, uoss;
	int ret;
	mm_segment_t seg;

	if (uss_ptr) {
		u32 ptr;

		memset(&uss, 0, sizeof(stack_t));
		if (!access_ok(VERIFY_READ, uss_ptr, sizeof(*uss_ptr)) ||
			    __get_user(ptr, &uss_ptr->ss_sp) ||
			    __get_user(uss.ss_flags, &uss_ptr->ss_flags) ||
			    __get_user(uss.ss_size, &uss_ptr->ss_size))
			return -EFAULT;
		uss.ss_sp = compat_ptr(ptr);
	}
	seg = get_fs();
	set_fs(KERNEL_DS);
	ret = do_sigaltstack(uss_ptr ? (stack_t __user __force *)&uss : NULL,
			     (stack_t __user __force *)&uoss,
			     (unsigned long)compat_ptr(regs->sp));
	set_fs(seg);
	if (ret >= 0 && uoss_ptr)  {
		if (!access_ok(VERIFY_WRITE, uoss_ptr, sizeof(*uoss_ptr)) ||
		    __put_user(ptr_to_compat(uoss.ss_sp), &uoss_ptr->ss_sp) ||
		    __put_user(uoss.ss_flags, &uoss_ptr->ss_flags) ||
		    __put_user(uoss.ss_size, &uoss_ptr->ss_size))
			ret = -EFAULT;
	}
	return ret;
}

/* The assembly shim for this function arranges to ignore the return value. */
long compat_sys_rt_sigreturn(struct pt_regs *regs)
{
	struct compat_rt_sigframe __user *frame =
		(struct compat_rt_sigframe __user *) compat_ptr(regs->sp);
	sigset_t set;

	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	set_current_blocked(&set);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	if (compat_sys_sigaltstack(&frame->uc.uc_stack, NULL, regs) != 0)
		goto badframe;

	return 0;

badframe:
	signal_fault("bad sigreturn frame", regs, frame, 0);
	return 0;
}

/*
 * Determine which stack to use..
 */
static inline void __user *compat_get_sigframe(struct k_sigaction *ka,
					       struct pt_regs *regs,
					       size_t frame_size)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = (unsigned long)compat_ptr(regs->sp);

	/*
	 * If we are on the alternate signal stack and would overflow
	 * it, don't.  Return an always-bogus address instead so we
	 * will die with SIGSEGV.
	 */
	if (on_sig_stack(sp) && !likely(on_sig_stack(sp - frame_size)))
		return (void __user __force *)-1UL;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (sas_ss_flags(sp) == 0)
			sp = current->sas_ss_sp + current->sas_ss_size;
	}

	sp -= frame_size;
	/*
	 * Align the stack pointer according to the TILE ABI,
	 * i.e. so that on function entry (sp & 15) == 0.
	 */
	sp &= -16UL;
	return (void __user *) sp;
}

int compat_setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			  sigset_t *set, struct pt_regs *regs)
{
	unsigned long restorer;
	struct compat_rt_sigframe __user *frame;
	int err = 0;
	int usig;

	frame = compat_get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	usig = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	/* Always write at least the signal number for the stack backtracer. */
	if (ka->sa.sa_flags & SA_SIGINFO) {
		/* At sigreturn time, restore the callee-save registers too. */
		err |= copy_siginfo_to_user32(&frame->info, info);
		regs->flags |= PT_FLAGS_RESTORE_REGS;
	} else {
		err |= __put_user(info->si_signo, &frame->info.si_signo);
	}

	/* Create the ucontext.  */
	err |= __clear_user(&frame->save_area, sizeof(frame->save_area));
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(ptr_to_compat((void *)(current->sas_ss_sp)),
			  &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->sp),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, regs);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		goto give_sigsegv;

	restorer = VDSO_BASE;
	if (ka->sa.sa_flags & SA_RESTORER)
		restorer = ptr_to_compat_reg(ka->sa.sa_restorer);

	/*
	 * Set up registers for signal handler.
	 * Registers that we don't modify keep the value they had from
	 * user-space at the time we took the signal.
	 * We always pass siginfo and mcontext, regardless of SA_SIGINFO,
	 * since some things rely on this (e.g. glibc's debug/segfault.c).
	 */
	regs->pc = ptr_to_compat_reg(ka->sa.sa_handler);
	regs->ex1 = PL_ICS_EX1(USER_PL, 1); /* set crit sec in handler */
	regs->sp = ptr_to_compat_reg(frame);
	regs->lr = restorer;
	regs->regs[0] = (unsigned long) usig;
	regs->regs[1] = ptr_to_compat_reg(&frame->info);
	regs->regs[2] = ptr_to_compat_reg(&frame->uc);
	regs->flags |= PT_FLAGS_CALLER_SAVES;

	/*
	 * Notify any tracer that was single-stepping it.
	 * The tracer may want to single-step inside the
	 * handler too.
	 */
	if (test_thread_flag(TIF_SINGLESTEP))
		ptrace_notify(SIGTRAP);

	return 0;

give_sigsegv:
	signal_fault("bad setup frame", regs, frame, sig);
	return -EFAULT;
}
