/*
 * Copyright (C) 2003 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/cache.h>
#include <linux/sched.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/compat.h>
#include <linux/bitops.h>

#include <asm/asm.h>
#include <asm/cacheflush.h>
#include <asm/sim.h>
#include <asm/uaccess.h>
#include <asm/ucontext.h>
#include <asm/system.h>
#include <asm/fpu.h>
#include <asm/cpu-features.h>
#include <asm/war.h>

#include "signal-common.h"

/*
 * Including <asm/unistd.h> would give use the 64-bit syscall numbers ...
 */
#define __NR_N32_rt_sigreturn		6211
#define __NR_N32_restart_syscall	6214

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

/* IRIX compatible stack_t  */
typedef struct sigaltstack32 {
	s32 ss_sp;
	compat_size_t ss_size;
	int ss_flags;
} stack32_t;

struct ucontextn32 {
	u32                 uc_flags;
	s32                 uc_link;
	stack32_t           uc_stack;
	struct sigcontext   uc_mcontext;
	sigset_t            uc_sigmask;   /* mask last for extensibility */
};

struct rt_sigframe_n32 {
	u32 rs_ass[4];			/* argument save space for o32 */
#if ICACHE_REFILLS_WORKAROUND_WAR
	u32 rs_pad[2];
#else
	u32 rs_code[2];			/* signal trampoline */
#endif
	struct siginfo rs_info;
	struct ucontextn32 rs_uc;
#if ICACHE_REFILLS_WORKAROUND_WAR
	u32 rs_code[8] ____cacheline_aligned;		/* signal trampoline */
#endif
};

extern void sigset_from_compat (sigset_t *set, compat_sigset_t *compat);

save_static_function(sysn32_rt_sigsuspend);
__attribute_used__ noinline static int
_sysn32_rt_sigsuspend(nabi_no_regargs struct pt_regs regs)
{
	compat_sigset_t __user *unewset;
	compat_sigset_t uset;
	size_t sigsetsize;
	sigset_t newset;

	/* XXX Don't preclude handling different sized sigset_t's.  */
	sigsetsize = regs.regs[5];
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	unewset = (compat_sigset_t __user *) regs.regs[4];
	if (copy_from_user(&uset, unewset, sizeof(uset)))
		return -EFAULT;
	sigset_from_compat (&newset, &uset);
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

save_static_function(sysn32_rt_sigreturn);
__attribute_used__ noinline static void
_sysn32_rt_sigreturn(nabi_no_regargs struct pt_regs regs)
{
	struct rt_sigframe_n32 __user *frame;
	sigset_t set;
	stack_t st;
	s32 sp;

	frame = (struct rt_sigframe_n32 __user *) regs.regs[29];
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->rs_uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(&regs, &frame->rs_uc.uc_mcontext))
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
	do_sigaltstack((stack_t __user *)&st, NULL, regs.regs[29]);

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

int setup_rt_frame_n32(struct k_sigaction * ka,
	struct pt_regs *regs, int signr, sigset_t *set, siginfo_t *info)
{
	struct rt_sigframe_n32 __user *frame;
	int err = 0;
	s32 sp;

	frame = get_sigframe(ka, regs, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		goto give_sigsegv;

	install_sigtramp(frame->rs_code, __NR_N32_rt_sigreturn);

	/* Create siginfo.  */
	err |= copy_siginfo_to_user(&frame->rs_info, info);

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
	err |= setup_sigcontext(regs, &frame->rs_uc.uc_mcontext);
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
	 * the struct rt_sigframe.
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
	       frame, regs->cp0_epc, regs->regs[31]);
#endif
	return 0;

give_sigsegv:
	force_sigsegv(signr, current);
	return -EFAULT;
}
