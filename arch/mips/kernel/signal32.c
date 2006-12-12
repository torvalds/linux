/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 1994 - 2000, 2006  Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/cache.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/syscalls.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/compat.h>
#include <linux/suspend.h>
#include <linux/compiler.h>

#include <asm/abi.h>
#include <asm/asm.h>
#include <linux/bitops.h>
#include <asm/cacheflush.h>
#include <asm/sim.h>
#include <asm/uaccess.h>
#include <asm/ucontext.h>
#include <asm/system.h>
#include <asm/fpu.h>
#include <asm/war.h>

#define SI_PAD_SIZE32   ((SI_MAX_SIZE/sizeof(int)) - 3)

typedef struct compat_siginfo {
	int si_signo;
	int si_code;
	int si_errno;

	union {
		int _pad[SI_PAD_SIZE32];

		/* kill() */
		struct {
			compat_pid_t _pid;	/* sender's pid */
			compat_uid_t _uid;	/* sender's uid */
		} _kill;

		/* SIGCHLD */
		struct {
			compat_pid_t _pid;	/* which child */
			compat_uid_t _uid;	/* sender's uid */
			int _status;		/* exit code */
			compat_clock_t _utime;
			compat_clock_t _stime;
		} _sigchld;

		/* IRIX SIGCHLD */
		struct {
			compat_pid_t _pid;	/* which child */
			compat_clock_t _utime;
			int _status;		/* exit code */
			compat_clock_t _stime;
		} _irix_sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			s32 _addr; /* faulting insn/memory ref. */
		} _sigfault;

		/* SIGPOLL, SIGXFSZ (To do ...)  */
		struct {
			int _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;

		/* POSIX.1b timers */
		struct {
			timer_t _tid;		/* timer id */
			int _overrun;		/* overrun count */
			compat_sigval_t _sigval;/* same as below */
			int _sys_private;       /* not to be passed to user */
		} _timer;

		/* POSIX.1b signals */
		struct {
			compat_pid_t _pid;	/* sender's pid */
			compat_uid_t _uid;	/* sender's uid */
			compat_sigval_t _sigval;
		} _rt;

	} _sifields;
} compat_siginfo_t;

/*
 * Including <asm/unistd.h> would give use the 64-bit syscall numbers ...
 */
#define __NR_O32_sigreturn		4119
#define __NR_O32_rt_sigreturn		4193
#define __NR_O32_restart_syscall	4253

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

/* 32-bit compatibility types */

#define _NSIG_BPW32	32
#define _NSIG_WORDS32	(_NSIG / _NSIG_BPW32)

typedef struct {
	unsigned int sig[_NSIG_WORDS32];
} sigset_t32;

typedef unsigned int __sighandler32_t;
typedef void (*vfptr_t)(void);

struct sigaction32 {
	unsigned int		sa_flags;
	__sighandler32_t	sa_handler;
	compat_sigset_t		sa_mask;
};

/* IRIX compatible stack_t  */
typedef struct sigaltstack32 {
	s32 ss_sp;
	compat_size_t ss_size;
	int ss_flags;
} stack32_t;

struct ucontext32 {
	u32                 uc_flags;
	s32                 uc_link;
	stack32_t           uc_stack;
	struct sigcontext32 uc_mcontext;
	sigset_t32          uc_sigmask;   /* mask last for extensibility */
};

extern void __put_sigset_unknown_nsig(void);
extern void __get_sigset_unknown_nsig(void);

static inline int put_sigset(const sigset_t *kbuf, compat_sigset_t __user *ubuf)
{
	int err = 0;

	if (!access_ok(VERIFY_WRITE, ubuf, sizeof(*ubuf)))
		return -EFAULT;

	switch (_NSIG_WORDS) {
	default:
		__put_sigset_unknown_nsig();
	case 2:
		err |= __put_user (kbuf->sig[1] >> 32, &ubuf->sig[3]);
		err |= __put_user (kbuf->sig[1] & 0xffffffff, &ubuf->sig[2]);
	case 1:
		err |= __put_user (kbuf->sig[0] >> 32, &ubuf->sig[1]);
		err |= __put_user (kbuf->sig[0] & 0xffffffff, &ubuf->sig[0]);
	}

	return err;
}

static inline int get_sigset(sigset_t *kbuf, const compat_sigset_t __user *ubuf)
{
	int err = 0;
	unsigned long sig[4];

	if (!access_ok(VERIFY_READ, ubuf, sizeof(*ubuf)))
		return -EFAULT;

	switch (_NSIG_WORDS) {
	default:
		__get_sigset_unknown_nsig();
	case 2:
		err |= __get_user (sig[3], &ubuf->sig[3]);
		err |= __get_user (sig[2], &ubuf->sig[2]);
		kbuf->sig[1] = sig[2] | (sig[3] << 32);
	case 1:
		err |= __get_user (sig[1], &ubuf->sig[1]);
		err |= __get_user (sig[0], &ubuf->sig[0]);
		kbuf->sig[0] = sig[0] | (sig[1] << 32);
	}

	return err;
}

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */

save_static_function(sys32_sigsuspend);
__attribute_used__ noinline static int
_sys32_sigsuspend(nabi_no_regargs struct pt_regs regs)
{
	compat_sigset_t __user *uset;
	sigset_t newset;

	uset = (compat_sigset_t __user *) regs.regs[4];
	if (get_sigset(&newset, uset))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	current->saved_sigmask = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	current->state = TASK_INTERRUPTIBLE;
	schedule();
	set_thread_flag(TIF_RESTORE_SIGMASK);
	return -ERESTARTNOHAND;
}

save_static_function(sys32_rt_sigsuspend);
__attribute_used__ noinline static int
_sys32_rt_sigsuspend(nabi_no_regargs struct pt_regs regs)
{
	compat_sigset_t __user *uset;
	sigset_t newset;
	size_t sigsetsize;

	/* XXX Don't preclude handling different sized sigset_t's.  */
	sigsetsize = regs.regs[5];
	if (sigsetsize != sizeof(compat_sigset_t))
		return -EINVAL;

	uset = (compat_sigset_t __user *) regs.regs[4];
	if (get_sigset(&newset, uset))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	current->saved_sigmask = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	current->state = TASK_INTERRUPTIBLE;
	schedule();
	set_thread_flag(TIF_RESTORE_SIGMASK);
	return -ERESTARTNOHAND;
}

asmlinkage int sys32_sigaction(int sig, const struct sigaction32 __user *act,
                               struct sigaction32 __user *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;
	int err = 0;

	if (act) {
		old_sigset_t mask;
		s32 handler;

		if (!access_ok(VERIFY_READ, act, sizeof(*act)))
			return -EFAULT;
		err |= __get_user(handler, &act->sa_handler);
		new_ka.sa.sa_handler = (void __user *)(s64)handler;
		err |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		err |= __get_user(mask, &act->sa_mask.sig[0]);
		if (err)
			return -EFAULT;

		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)))
			return -EFAULT;
		err |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		err |= __put_user((u32)(u64)old_ka.sa.sa_handler,
		                  &oact->sa_handler);
		err |= __put_user(old_ka.sa.sa_mask.sig[0], oact->sa_mask.sig);
		err |= __put_user(0, &oact->sa_mask.sig[1]);
		err |= __put_user(0, &oact->sa_mask.sig[2]);
		err |= __put_user(0, &oact->sa_mask.sig[3]);
		if (err)
			return -EFAULT;
	}

	return ret;
}

asmlinkage int sys32_sigaltstack(nabi_no_regargs struct pt_regs regs)
{
	const stack32_t __user *uss = (const stack32_t __user *) regs.regs[4];
	stack32_t __user *uoss = (stack32_t __user *) regs.regs[5];
	unsigned long usp = regs.regs[29];
	stack_t kss, koss;
	int ret, err = 0;
	mm_segment_t old_fs = get_fs();
	s32 sp;

	if (uss) {
		if (!access_ok(VERIFY_READ, uss, sizeof(*uss)))
			return -EFAULT;
		err |= __get_user(sp, &uss->ss_sp);
		kss.ss_sp = (void __user *) (long) sp;
		err |= __get_user(kss.ss_size, &uss->ss_size);
		err |= __get_user(kss.ss_flags, &uss->ss_flags);
		if (err)
			return -EFAULT;
	}

	set_fs (KERNEL_DS);
	ret = do_sigaltstack(uss ? (stack_t __user *)&kss : NULL,
			     uoss ? (stack_t __user *)&koss : NULL, usp);
	set_fs (old_fs);

	if (!ret && uoss) {
		if (!access_ok(VERIFY_WRITE, uoss, sizeof(*uoss)))
			return -EFAULT;
		sp = (int) (unsigned long) koss.ss_sp;
		err |= __put_user(sp, &uoss->ss_sp);
		err |= __put_user(koss.ss_size, &uoss->ss_size);
		err |= __put_user(koss.ss_flags, &uoss->ss_flags);
		if (err)
			return -EFAULT;
	}
	return ret;
}

static int restore_sigcontext32(struct pt_regs *regs, struct sigcontext32 __user *sc)
{
	u32 used_math;
	int err = 0;
	s32 treg;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	err |= __get_user(regs->cp0_epc, &sc->sc_pc);
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

#define restore_gp_reg(i) do {						\
	err |= __get_user(regs->regs[i], &sc->sc_regs[i]);		\
} while(0)
	restore_gp_reg( 1); restore_gp_reg( 2); restore_gp_reg( 3);
	restore_gp_reg( 4); restore_gp_reg( 5); restore_gp_reg( 6);
	restore_gp_reg( 7); restore_gp_reg( 8); restore_gp_reg( 9);
	restore_gp_reg(10); restore_gp_reg(11); restore_gp_reg(12);
	restore_gp_reg(13); restore_gp_reg(14); restore_gp_reg(15);
	restore_gp_reg(16); restore_gp_reg(17); restore_gp_reg(18);
	restore_gp_reg(19); restore_gp_reg(20); restore_gp_reg(21);
	restore_gp_reg(22); restore_gp_reg(23); restore_gp_reg(24);
	restore_gp_reg(25); restore_gp_reg(26); restore_gp_reg(27);
	restore_gp_reg(28); restore_gp_reg(29); restore_gp_reg(30);
	restore_gp_reg(31);
#undef restore_gp_reg

	err |= __get_user(used_math, &sc->sc_used_math);
	conditional_used_math(used_math);

	preempt_disable();

	if (used_math()) {
		/* restore fpu context if we have used it before */
		own_fpu();
		err |= restore_fp_context32(sc);
	} else {
		/* signal handler may have used FPU.  Give it up. */
		lose_fpu();
	}

	preempt_enable();

	return err;
}

struct sigframe {
	u32 sf_ass[4];			/* argument save space for o32 */
#if ICACHE_REFILLS_WORKAROUND_WAR
	u32 sf_pad[2];
#else
	u32 sf_code[2];			/* signal trampoline */
#endif
	struct sigcontext32 sf_sc;
	sigset_t sf_mask;
#if ICACHE_REFILLS_WORKAROUND_WAR
	u32 sf_code[8] ____cacheline_aligned;	/* signal trampoline */
#endif
};

struct rt_sigframe32 {
	u32 rs_ass[4];			/* argument save space for o32 */
#if ICACHE_REFILLS_WORKAROUND_WAR
	u32 rs_pad[2];
#else
	u32 rs_code[2];			/* signal trampoline */
#endif
	compat_siginfo_t rs_info;
	struct ucontext32 rs_uc;
#if ICACHE_REFILLS_WORKAROUND_WAR
	u32 rs_code[8] __attribute__((aligned(32)));	/* signal trampoline */
#endif
};

int copy_siginfo_to_user32(compat_siginfo_t __user *to, siginfo_t *from)
{
	int err;

	if (!access_ok (VERIFY_WRITE, to, sizeof(compat_siginfo_t)))
		return -EFAULT;

	/* If you change siginfo_t structure, please be sure
	   this code is fixed accordingly.
	   It should never copy any pad contained in the structure
	   to avoid security leaks, but must copy the generic
	   3 ints plus the relevant union member.
	   This routine must convert siginfo from 64bit to 32bit as well
	   at the same time.  */
	err = __put_user(from->si_signo, &to->si_signo);
	err |= __put_user(from->si_errno, &to->si_errno);
	err |= __put_user((short)from->si_code, &to->si_code);
	if (from->si_code < 0)
		err |= __copy_to_user(&to->_sifields._pad, &from->_sifields._pad, SI_PAD_SIZE);
	else {
		switch (from->si_code >> 16) {
		case __SI_TIMER >> 16:
			err |= __put_user(from->si_tid, &to->si_tid);
			err |= __put_user(from->si_overrun, &to->si_overrun);
			err |= __put_user(from->si_int, &to->si_int);
			break;
		case __SI_CHLD >> 16:
			err |= __put_user(from->si_utime, &to->si_utime);
			err |= __put_user(from->si_stime, &to->si_stime);
			err |= __put_user(from->si_status, &to->si_status);
		default:
			err |= __put_user(from->si_pid, &to->si_pid);
			err |= __put_user(from->si_uid, &to->si_uid);
			break;
		case __SI_FAULT >> 16:
			err |= __put_user((unsigned long)from->si_addr, &to->si_addr);
			break;
		case __SI_POLL >> 16:
			err |= __put_user(from->si_band, &to->si_band);
			err |= __put_user(from->si_fd, &to->si_fd);
			break;
		case __SI_RT >> 16: /* This is not generated by the kernel as of now.  */
		case __SI_MESGQ >> 16:
			err |= __put_user(from->si_pid, &to->si_pid);
			err |= __put_user(from->si_uid, &to->si_uid);
			err |= __put_user(from->si_int, &to->si_int);
			break;
		}
	}
	return err;
}

save_static_function(sys32_sigreturn);
__attribute_used__ noinline static void
_sys32_sigreturn(nabi_no_regargs struct pt_regs regs)
{
	struct sigframe __user *frame;
	sigset_t blocked;

	frame = (struct sigframe __user *) regs.regs[29];
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&blocked, &frame->sf_mask, sizeof(blocked)))
		goto badframe;

	sigdelsetmask(&blocked, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = blocked;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext32(&regs, &frame->sf_sc))
		goto badframe;

	/*
	 * Don't let your children do this ...
	 */
	__asm__ __volatile__(
		"move\t$29, %0\n\t"
		"j\tsyscall_exit"
		:/* no outputs */
		:"r" (&regs));
	/* Unreached */

badframe:
	force_sig(SIGSEGV, current);
}

save_static_function(sys32_rt_sigreturn);
__attribute_used__ noinline static void
_sys32_rt_sigreturn(nabi_no_regargs struct pt_regs regs)
{
	struct rt_sigframe32 __user *frame;
	mm_segment_t old_fs;
	sigset_t set;
	stack_t st;
	s32 sp;

	frame = (struct rt_sigframe32 __user *) regs.regs[29];
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->rs_uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext32(&regs, &frame->rs_uc.uc_mcontext))
		goto badframe;

	/* The ucontext contains a stack32_t, so we must convert!  */
	if (__get_user(sp, &frame->rs_uc.uc_stack.ss_sp))
		goto badframe;
	st.ss_sp = (void __user *)(long) sp;
	if (__get_user(st.ss_size, &frame->rs_uc.uc_stack.ss_size))
		goto badframe;
	if (__get_user(st.ss_flags, &frame->rs_uc.uc_stack.ss_flags))
		goto badframe;

	/* It is more difficult to avoid calling this function than to
	   call it and ignore errors.  */
	old_fs = get_fs();
	set_fs (KERNEL_DS);
	do_sigaltstack((stack_t __user *)&st, NULL, regs.regs[29]);
	set_fs (old_fs);

	/*
	 * Don't let your children do this ...
	 */
	__asm__ __volatile__(
		"move\t$29, %0\n\t"
		"j\tsyscall_exit"
		:/* no outputs */
		:"r" (&regs));
	/* Unreached */

badframe:
	force_sig(SIGSEGV, current);
}

static inline int setup_sigcontext32(struct pt_regs *regs,
				     struct sigcontext32 __user *sc)
{
	int err = 0;

	err |= __put_user(regs->cp0_epc, &sc->sc_pc);
	err |= __put_user(regs->cp0_status, &sc->sc_status);

#define save_gp_reg(i) {						\
	err |= __put_user(regs->regs[i], &sc->sc_regs[i]);		\
} while(0)
	__put_user(0, &sc->sc_regs[0]); save_gp_reg(1); save_gp_reg(2);
	save_gp_reg(3); save_gp_reg(4); save_gp_reg(5); save_gp_reg(6);
	save_gp_reg(7); save_gp_reg(8); save_gp_reg(9); save_gp_reg(10);
	save_gp_reg(11); save_gp_reg(12); save_gp_reg(13); save_gp_reg(14);
	save_gp_reg(15); save_gp_reg(16); save_gp_reg(17); save_gp_reg(18);
	save_gp_reg(19); save_gp_reg(20); save_gp_reg(21); save_gp_reg(22);
	save_gp_reg(23); save_gp_reg(24); save_gp_reg(25); save_gp_reg(26);
	save_gp_reg(27); save_gp_reg(28); save_gp_reg(29); save_gp_reg(30);
	save_gp_reg(31);
#undef save_gp_reg

	err |= __put_user(regs->hi, &sc->sc_mdhi);
	err |= __put_user(regs->lo, &sc->sc_mdlo);
	if (cpu_has_dsp) {
		err |= __put_user(rddsp(DSP_MASK), &sc->sc_dsp);
		err |= __put_user(mfhi1(), &sc->sc_hi1);
		err |= __put_user(mflo1(), &sc->sc_lo1);
		err |= __put_user(mfhi2(), &sc->sc_hi2);
		err |= __put_user(mflo2(), &sc->sc_lo2);
		err |= __put_user(mfhi3(), &sc->sc_hi3);
		err |= __put_user(mflo3(), &sc->sc_lo3);
	}

	err |= __put_user(!!used_math(), &sc->sc_used_math);

	if (!used_math())
		goto out;

	/*
	 * Save FPU state to signal context.  Signal handler will "inherit"
	 * current FPU state.
	 */
	preempt_disable();

	if (!is_fpu_owner()) {
		own_fpu();
		restore_fp(current);
	}
	err |= save_fp_context32(sc);

	preempt_enable();

out:
	return err;
}

/*
 * Determine which stack to use..
 */
static inline void __user *get_sigframe(struct k_sigaction *ka,
					struct pt_regs *regs,
					size_t frame_size)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = regs->regs[29];

	/*
	 * FPU emulator may have it's own trampoline active just
	 * above the user stack, 16-bytes before the next lowest
	 * 16 byte boundary.  Try to avoid trashing it.
	 */
	sp -= 32;

	/* This is the X/Open sanctioned signal stack switching.  */
	if ((ka->sa.sa_flags & SA_ONSTACK) && (sas_ss_flags (sp) == 0))
		sp = current->sas_ss_sp + current->sas_ss_size;

	return (void __user *)((sp - frame_size) & ALMASK);
}

int setup_frame_32(struct k_sigaction * ka, struct pt_regs *regs,
	int signr, sigset_t *set)
{
	struct sigframe __user *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		goto give_sigsegv;

	/*
	 * Set up the return code ...
	 *
	 *         li      v0, __NR_O32_sigreturn
	 *         syscall
	 */
	err |= __put_user(0x24020000 + __NR_O32_sigreturn, frame->sf_code + 0);
	err |= __put_user(0x0000000c                     , frame->sf_code + 1);
	flush_cache_sigtramp((unsigned long) frame->sf_code);

	err |= setup_sigcontext32(regs, &frame->sf_sc);
	err |= __copy_to_user(&frame->sf_mask, set, sizeof(*set));
	if (err)
		goto give_sigsegv;

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
	regs->regs[ 4] = signr;
	regs->regs[ 5] = 0;
	regs->regs[ 6] = (unsigned long) &frame->sf_sc;
	regs->regs[29] = (unsigned long) frame;
	regs->regs[31] = (unsigned long) frame->sf_code;
	regs->cp0_epc = regs->regs[25] = (unsigned long) ka->sa.sa_handler;

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=0x%p pc=0x%lx ra=0x%p\n",
	       current->comm, current->pid,
	       frame, regs->cp0_epc, frame->sf_code);
#endif
	return 0;

give_sigsegv:
	force_sigsegv(signr, current);
	return -EFAULT;
}

int setup_rt_frame_32(struct k_sigaction * ka, struct pt_regs *regs,
	int signr, sigset_t *set, siginfo_t *info)
{
	struct rt_sigframe32 __user *frame;
	int err = 0;
	s32 sp;

	frame = get_sigframe(ka, regs, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		goto give_sigsegv;

	/* Set up to return from userspace.  If provided, use a stub already
	   in userspace.  */
	/*
	 * Set up the return code ...
	 *
	 *         li      v0, __NR_O32_rt_sigreturn
	 *         syscall
	 */
	err |= __put_user(0x24020000 + __NR_O32_rt_sigreturn, frame->rs_code + 0);
	err |= __put_user(0x0000000c                      , frame->rs_code + 1);
	flush_cache_sigtramp((unsigned long) frame->rs_code);

	/* Convert (siginfo_t -> compat_siginfo_t) and copy to user. */
	err |= copy_siginfo_to_user32(&frame->rs_info, info);

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->rs_uc.uc_flags);
	err |= __put_user(0, &frame->rs_uc.uc_link);
	sp = (int) (long) current->sas_ss_sp;
	err |= __put_user(sp,
	                  &frame->rs_uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->regs[29]),
	                  &frame->rs_uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size,
	                  &frame->rs_uc.uc_stack.ss_size);
	err |= setup_sigcontext32(regs, &frame->rs_uc.uc_mcontext);
	err |= __copy_to_user(&frame->rs_uc.uc_sigmask, set, sizeof(*set));

	if (err)
		goto give_sigsegv;

	/*
	 * Arguments to signal handler:
	 *
	 *   a0 = signal number
	 *   a1 = 0 (should be cause)
	 *   a2 = pointer to ucontext
	 *
	 * $25 and c0_epc point to the signal handler, $29 points to
	 * the struct rt_sigframe32.
	 */
	regs->regs[ 4] = signr;
	regs->regs[ 5] = (unsigned long) &frame->rs_info;
	regs->regs[ 6] = (unsigned long) &frame->rs_uc;
	regs->regs[29] = (unsigned long) frame;
	regs->regs[31] = (unsigned long) frame->rs_code;
	regs->cp0_epc = regs->regs[25] = (unsigned long) ka->sa.sa_handler;

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=0x%p pc=0x%lx ra=0x%p\n",
	       current->comm, current->pid,
	       frame, regs->cp0_epc, frame->rs_code);
#endif
	return 0;

give_sigsegv:
	force_sigsegv(signr, current);
	return -EFAULT;
}

static inline int handle_signal(unsigned long sig, siginfo_t *info,
	struct k_sigaction *ka, sigset_t *oldset, struct pt_regs * regs)
{
	int ret;

	switch (regs->regs[0]) {
	case ERESTART_RESTARTBLOCK:
	case ERESTARTNOHAND:
		regs->regs[2] = EINTR;
		break;
	case ERESTARTSYS:
		if (!(ka->sa.sa_flags & SA_RESTART)) {
			regs->regs[2] = EINTR;
			break;
		}
	/* fallthrough */
	case ERESTARTNOINTR:		/* Userland will reload $v0.  */
		regs->regs[7] = regs->regs[26];
		regs->cp0_epc -= 8;
	}

	regs->regs[0] = 0;		/* Don't deal with this again.  */

	if (ka->sa.sa_flags & SA_SIGINFO)
		ret = current->thread.abi->setup_rt_frame(ka, regs, sig, oldset, info);
	else
		ret = current->thread.abi->setup_frame(ka, regs, sig, oldset);

	spin_lock_irq(&current->sighand->siglock);
	sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
	if (!(ka->sa.sa_flags & SA_NODEFER))
		sigaddset(&current->blocked,sig);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	return ret;
}

void do_signal32(struct pt_regs *regs)
{
	struct k_sigaction ka;
	sigset_t *oldset;
	siginfo_t info;
	int signr;

	/*
	 * We want the common case to go fast, which is why we may in certain
	 * cases get here from kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return;

	if (test_thread_flag(TIF_RESTORE_SIGMASK))
		oldset = &current->saved_sigmask;
	else
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, &ka, regs, NULL);
	if (signr > 0) {
		/* Whee! Actually deliver the signal. */
		if (handle_signal(signr, &info, &ka, oldset, regs) == 0) {
			/*
			* A signal was successfully delivered; the saved
			* sigmask will have been stored in the signal frame,
			* and will be restored by sigreturn, so we can simply
			* clear the TIF_RESTORE_SIGMASK flag.
			*/
			if (test_thread_flag(TIF_RESTORE_SIGMASK))
				clear_thread_flag(TIF_RESTORE_SIGMASK);
		}

		return;
	}

	/*
	 * Who's code doesn't conform to the restartable syscall convention
	 * dies here!!!  The li instruction, a single machine instruction,
	 * must directly be followed by the syscall instruction.
	 */
	if (regs->regs[0]) {
		if (regs->regs[2] == ERESTARTNOHAND ||
		    regs->regs[2] == ERESTARTSYS ||
		    regs->regs[2] == ERESTARTNOINTR) {
			regs->regs[7] = regs->regs[26];
			regs->cp0_epc -= 8;
		}
		if (regs->regs[2] == ERESTART_RESTARTBLOCK) {
			regs->regs[2] = __NR_O32_restart_syscall;
			regs->regs[7] = regs->regs[26];
			regs->cp0_epc -= 4;
		}
		regs->regs[0] = 0;	/* Don't deal with this again.  */
	}

	/*
	* If there's no signal to deliver, we just put the saved sigmask
	* back
	*/
	if (test_thread_flag(TIF_RESTORE_SIGMASK)) {
		clear_thread_flag(TIF_RESTORE_SIGMASK);
		sigprocmask(SIG_SETMASK, &current->saved_sigmask, NULL);
	}
}

asmlinkage int sys32_rt_sigaction(int sig, const struct sigaction32 __user *act,
				  struct sigaction32 __user *oact,
				  unsigned int sigsetsize)
{
	struct k_sigaction new_sa, old_sa;
	int ret = -EINVAL;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		goto out;

	if (act) {
		s32 handler;
		int err = 0;

		if (!access_ok(VERIFY_READ, act, sizeof(*act)))
			return -EFAULT;
		err |= __get_user(handler, &act->sa_handler);
		new_sa.sa.sa_handler = (void __user *)(s64)handler;
		err |= __get_user(new_sa.sa.sa_flags, &act->sa_flags);
		err |= get_sigset(&new_sa.sa.sa_mask, &act->sa_mask);
		if (err)
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_sa : NULL, oact ? &old_sa : NULL);

	if (!ret && oact) {
		int err = 0;

		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)))
			return -EFAULT;

		err |= __put_user((u32)(u64)old_sa.sa.sa_handler,
		                   &oact->sa_handler);
		err |= __put_user(old_sa.sa.sa_flags, &oact->sa_flags);
		err |= put_sigset(&old_sa.sa.sa_mask, &oact->sa_mask);
		if (err)
			return -EFAULT;
	}
out:
	return ret;
}

asmlinkage int sys32_rt_sigprocmask(int how, compat_sigset_t __user *set,
	compat_sigset_t __user *oset, unsigned int sigsetsize)
{
	sigset_t old_set, new_set;
	int ret;
	mm_segment_t old_fs = get_fs();

	if (set && get_sigset(&new_set, set))
		return -EFAULT;

	set_fs (KERNEL_DS);
	ret = sys_rt_sigprocmask(how, set ? (sigset_t __user *)&new_set : NULL,
				 oset ? (sigset_t __user *)&old_set : NULL,
				 sigsetsize);
	set_fs (old_fs);

	if (!ret && oset && put_sigset(&old_set, oset))
		return -EFAULT;

	return ret;
}

asmlinkage int sys32_rt_sigpending(compat_sigset_t __user *uset,
	unsigned int sigsetsize)
{
	int ret;
	sigset_t set;
	mm_segment_t old_fs = get_fs();

	set_fs (KERNEL_DS);
	ret = sys_rt_sigpending((sigset_t __user *)&set, sigsetsize);
	set_fs (old_fs);

	if (!ret && put_sigset(&set, uset))
		return -EFAULT;

	return ret;
}

asmlinkage int sys32_rt_sigqueueinfo(int pid, int sig, compat_siginfo_t __user *uinfo)
{
	siginfo_t info;
	int ret;
	mm_segment_t old_fs = get_fs();

	if (copy_from_user (&info, uinfo, 3*sizeof(int)) ||
	    copy_from_user (info._sifields._pad, uinfo->_sifields._pad, SI_PAD_SIZE))
		return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_rt_sigqueueinfo(pid, sig, (siginfo_t __user *)&info);
	set_fs (old_fs);
	return ret;
}

asmlinkage long
sys32_waitid(int which, compat_pid_t pid,
	     compat_siginfo_t __user *uinfo, int options,
	     struct compat_rusage __user *uru)
{
	siginfo_t info;
	struct rusage ru;
	long ret;
	mm_segment_t old_fs = get_fs();

	info.si_signo = 0;
	set_fs (KERNEL_DS);
	ret = sys_waitid(which, pid, (siginfo_t __user *) &info, options,
			 uru ? (struct rusage __user *) &ru : NULL);
	set_fs (old_fs);

	if (ret < 0 || info.si_signo == 0)
		return ret;

	if (uru && (ret = put_compat_rusage(&ru, uru)))
		return ret;

	BUG_ON(info.si_code & __SI_MASK);
	info.si_code |= __SI_CHLD;
	return copy_siginfo_to_user32(uinfo, &info);
}
