// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2000, 2006
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *               Gerhard Tonn (ton@de.ibm.com)                  
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 */

#include <linux/compat.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/tty.h>
#include <linux/personality.h>
#include <linux/binfmts.h>
#include <asm/ucontext.h>
#include <linux/uaccess.h>
#include <asm/lowcore.h>
#include <asm/switch_to.h>
#include "compat_linux.h"
#include "compat_ptrace.h"
#include "entry.h"

typedef struct 
{
	__u8 callee_used_stack[__SIGNAL_FRAMESIZE32];
	struct sigcontext32 sc;
	_sigregs32 sregs;
	int signo;
	_sigregs_ext32 sregs_ext;
	__u16 svc_insn;		/* Offset of svc_insn is NOT fixed! */
} sigframe32;

typedef struct 
{
	__u8 callee_used_stack[__SIGNAL_FRAMESIZE32];
	__u16 svc_insn;
	compat_siginfo_t info;
	struct ucontext32 uc;
} rt_sigframe32;

/* Store registers needed to create the signal frame */
static void store_sigregs(void)
{
	save_access_regs(current->thread.acrs);
	save_fpu_regs();
}

/* Load registers after signal return */
static void load_sigregs(void)
{
	restore_access_regs(current->thread.acrs);
}

static int save_sigregs32(struct pt_regs *regs, _sigregs32 __user *sregs)
{
	_sigregs32 user_sregs;
	int i;

	user_sregs.regs.psw.mask = (__u32)(regs->psw.mask >> 32);
	user_sregs.regs.psw.mask &= PSW32_MASK_USER | PSW32_MASK_RI;
	user_sregs.regs.psw.mask |= PSW32_USER_BITS;
	user_sregs.regs.psw.addr = (__u32) regs->psw.addr |
		(__u32)(regs->psw.mask & PSW_MASK_BA);
	for (i = 0; i < NUM_GPRS; i++)
		user_sregs.regs.gprs[i] = (__u32) regs->gprs[i];
	memcpy(&user_sregs.regs.acrs, current->thread.acrs,
	       sizeof(user_sregs.regs.acrs));
	fpregs_store((_s390_fp_regs *) &user_sregs.fpregs, &current->thread.fpu);
	if (__copy_to_user(sregs, &user_sregs, sizeof(_sigregs32)))
		return -EFAULT;
	return 0;
}

static int restore_sigregs32(struct pt_regs *regs,_sigregs32 __user *sregs)
{
	_sigregs32 user_sregs;
	int i;

	/* Alwys make any pending restarted system call return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	if (__copy_from_user(&user_sregs, &sregs->regs, sizeof(user_sregs)))
		return -EFAULT;

	if (!is_ri_task(current) && (user_sregs.regs.psw.mask & PSW32_MASK_RI))
		return -EINVAL;

	/* Test the floating-point-control word. */
	if (test_fp_ctl(user_sregs.fpregs.fpc))
		return -EINVAL;

	/* Use regs->psw.mask instead of PSW_USER_BITS to preserve PER bit. */
	regs->psw.mask = (regs->psw.mask & ~(PSW_MASK_USER | PSW_MASK_RI)) |
		(__u64)(user_sregs.regs.psw.mask & PSW32_MASK_USER) << 32 |
		(__u64)(user_sregs.regs.psw.mask & PSW32_MASK_RI) << 32 |
		(__u64)(user_sregs.regs.psw.addr & PSW32_ADDR_AMODE);
	/* Check for invalid user address space control. */
	if ((regs->psw.mask & PSW_MASK_ASC) == PSW_ASC_HOME)
		regs->psw.mask = PSW_ASC_PRIMARY |
			(regs->psw.mask & ~PSW_MASK_ASC);
	regs->psw.addr = (__u64)(user_sregs.regs.psw.addr & PSW32_ADDR_INSN);
	for (i = 0; i < NUM_GPRS; i++)
		regs->gprs[i] = (__u64) user_sregs.regs.gprs[i];
	memcpy(&current->thread.acrs, &user_sregs.regs.acrs,
	       sizeof(current->thread.acrs));
	fpregs_load((_s390_fp_regs *) &user_sregs.fpregs, &current->thread.fpu);

	clear_pt_regs_flag(regs, PIF_SYSCALL); /* No longer in a system call */
	return 0;
}

static int save_sigregs_ext32(struct pt_regs *regs,
			      _sigregs_ext32 __user *sregs_ext)
{
	__u32 gprs_high[NUM_GPRS];
	__u64 vxrs[__NUM_VXRS_LOW];
	int i;

	/* Save high gprs to signal stack */
	for (i = 0; i < NUM_GPRS; i++)
		gprs_high[i] = regs->gprs[i] >> 32;
	if (__copy_to_user(&sregs_ext->gprs_high, &gprs_high,
			   sizeof(sregs_ext->gprs_high)))
		return -EFAULT;

	/* Save vector registers to signal stack */
	if (MACHINE_HAS_VX) {
		for (i = 0; i < __NUM_VXRS_LOW; i++)
			vxrs[i] = *((__u64 *)(current->thread.fpu.vxrs + i) + 1);
		if (__copy_to_user(&sregs_ext->vxrs_low, vxrs,
				   sizeof(sregs_ext->vxrs_low)) ||
		    __copy_to_user(&sregs_ext->vxrs_high,
				   current->thread.fpu.vxrs + __NUM_VXRS_LOW,
				   sizeof(sregs_ext->vxrs_high)))
			return -EFAULT;
	}
	return 0;
}

static int restore_sigregs_ext32(struct pt_regs *regs,
				 _sigregs_ext32 __user *sregs_ext)
{
	__u32 gprs_high[NUM_GPRS];
	__u64 vxrs[__NUM_VXRS_LOW];
	int i;

	/* Restore high gprs from signal stack */
	if (__copy_from_user(&gprs_high, &sregs_ext->gprs_high,
			     sizeof(sregs_ext->gprs_high)))
		return -EFAULT;
	for (i = 0; i < NUM_GPRS; i++)
		*(__u32 *)&regs->gprs[i] = gprs_high[i];

	/* Restore vector registers from signal stack */
	if (MACHINE_HAS_VX) {
		if (__copy_from_user(vxrs, &sregs_ext->vxrs_low,
				     sizeof(sregs_ext->vxrs_low)) ||
		    __copy_from_user(current->thread.fpu.vxrs + __NUM_VXRS_LOW,
				     &sregs_ext->vxrs_high,
				     sizeof(sregs_ext->vxrs_high)))
			return -EFAULT;
		for (i = 0; i < __NUM_VXRS_LOW; i++)
			*((__u64 *)(current->thread.fpu.vxrs + i) + 1) = vxrs[i];
	}
	return 0;
}

COMPAT_SYSCALL_DEFINE0(sigreturn)
{
	struct pt_regs *regs = task_pt_regs(current);
	sigframe32 __user *frame = (sigframe32 __user *)regs->gprs[15];
	sigset_t set;

	if (get_compat_sigset(&set, (compat_sigset_t __user *)frame->sc.oldmask))
		goto badframe;
	set_current_blocked(&set);
	save_fpu_regs();
	if (restore_sigregs32(regs, &frame->sregs))
		goto badframe;
	if (restore_sigregs_ext32(regs, &frame->sregs_ext))
		goto badframe;
	load_sigregs();
	return regs->gprs[2];
badframe:
	force_sig(SIGSEGV);
	return 0;
}

COMPAT_SYSCALL_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = task_pt_regs(current);
	rt_sigframe32 __user *frame = (rt_sigframe32 __user *)regs->gprs[15];
	sigset_t set;

	if (get_compat_sigset(&set, &frame->uc.uc_sigmask))
		goto badframe;
	set_current_blocked(&set);
	if (compat_restore_altstack(&frame->uc.uc_stack))
		goto badframe;
	save_fpu_regs();
	if (restore_sigregs32(regs, &frame->uc.uc_mcontext))
		goto badframe;
	if (restore_sigregs_ext32(regs, &frame->uc.uc_mcontext_ext))
		goto badframe;
	load_sigregs();
	return regs->gprs[2];
badframe:
	force_sig(SIGSEGV);
	return 0;
}	

/*
 * Set up a signal frame.
 */


/*
 * Determine which stack to use..
 */
static inline void __user *
get_sigframe(struct k_sigaction *ka, struct pt_regs * regs, size_t frame_size)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = (unsigned long) A(regs->gprs[15]);

	/* Overflow on alternate signal stack gives SIGSEGV. */
	if (on_sig_stack(sp) && !on_sig_stack((sp - frame_size) & -8UL))
		return (void __user *) -1UL;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (! sas_ss_flags(sp))
			sp = current->sas_ss_sp + current->sas_ss_size;
	}

	return (void __user *)((sp - frame_size) & -8ul);
}

static int setup_frame32(struct ksignal *ksig, sigset_t *set,
			 struct pt_regs *regs)
{
	int sig = ksig->sig;
	sigframe32 __user *frame;
	unsigned long restorer;
	size_t frame_size;

	/*
	 * gprs_high are always present for 31-bit compat tasks.
	 * The space for vector registers is only allocated if
	 * the machine supports it
	 */
	frame_size = sizeof(*frame) - sizeof(frame->sregs_ext.__reserved);
	if (!MACHINE_HAS_VX)
		frame_size -= sizeof(frame->sregs_ext.vxrs_low) +
			      sizeof(frame->sregs_ext.vxrs_high);
	frame = get_sigframe(&ksig->ka, regs, frame_size);
	if (frame == (void __user *) -1UL)
		return -EFAULT;

	/* Set up backchain. */
	if (__put_user(regs->gprs[15], (unsigned int __user *) frame))
		return -EFAULT;

	/* Create struct sigcontext32 on the signal stack */
	if (put_compat_sigset((compat_sigset_t __user *)frame->sc.oldmask,
			      set, sizeof(compat_sigset_t)))
		return -EFAULT;
	if (__put_user(ptr_to_compat(&frame->sregs), &frame->sc.sregs))
		return -EFAULT;

	/* Store registers needed to create the signal frame */
	store_sigregs();

	/* Create _sigregs32 on the signal stack */
	if (save_sigregs32(regs, &frame->sregs))
		return -EFAULT;

	/* Place signal number on stack to allow backtrace from handler.  */
	if (__put_user(regs->gprs[2], (int __force __user *) &frame->signo))
		return -EFAULT;

	/* Create _sigregs_ext32 on the signal stack */
	if (save_sigregs_ext32(regs, &frame->sregs_ext))
		return -EFAULT;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ksig->ka.sa.sa_flags & SA_RESTORER) {
		restorer = (unsigned long __force)
			ksig->ka.sa.sa_restorer | PSW32_ADDR_AMODE;
	} else {
		/* Signal frames without vectors registers are short ! */
		__u16 __user *svc = (void __user *) frame + frame_size - 2;
		if (__put_user(S390_SYSCALL_OPCODE | __NR_sigreturn, svc))
			return -EFAULT;
		restorer = (unsigned long __force) svc | PSW32_ADDR_AMODE;
        }

	/* Set up registers for signal handler */
	regs->gprs[14] = restorer;
	regs->gprs[15] = (__force __u64) frame;
	/* Force 31 bit amode and default user address space control. */
	regs->psw.mask = PSW_MASK_BA |
		(PSW_USER_BITS & PSW_MASK_ASC) |
		(regs->psw.mask & ~PSW_MASK_ASC);
	regs->psw.addr = (__force __u64) ksig->ka.sa.sa_handler;

	regs->gprs[2] = sig;
	regs->gprs[3] = (__force __u64) &frame->sc;

	/* We forgot to include these in the sigcontext.
	   To avoid breaking binary compatibility, they are passed as args. */
	if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL ||
	    sig == SIGTRAP || sig == SIGFPE) {
		/* set extra registers only for synchronous signals */
		regs->gprs[4] = regs->int_code & 127;
		regs->gprs[5] = regs->int_parm_long;
		regs->gprs[6] = current->thread.last_break;
	}

	return 0;
}

static int setup_rt_frame32(struct ksignal *ksig, sigset_t *set,
			    struct pt_regs *regs)
{
	rt_sigframe32 __user *frame;
	unsigned long restorer;
	size_t frame_size;
	u32 uc_flags;

	frame_size = sizeof(*frame) -
		     sizeof(frame->uc.uc_mcontext_ext.__reserved);
	/*
	 * gprs_high are always present for 31-bit compat tasks.
	 * The space for vector registers is only allocated if
	 * the machine supports it
	 */
	uc_flags = UC_GPRS_HIGH;
	if (MACHINE_HAS_VX) {
		uc_flags |= UC_VXRS;
	} else
		frame_size -= sizeof(frame->uc.uc_mcontext_ext.vxrs_low) +
			      sizeof(frame->uc.uc_mcontext_ext.vxrs_high);
	frame = get_sigframe(&ksig->ka, regs, frame_size);
	if (frame == (void __user *) -1UL)
		return -EFAULT;

	/* Set up backchain. */
	if (__put_user(regs->gprs[15], (unsigned int __force __user *) frame))
		return -EFAULT;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ksig->ka.sa.sa_flags & SA_RESTORER) {
		restorer = (unsigned long __force)
			ksig->ka.sa.sa_restorer | PSW32_ADDR_AMODE;
	} else {
		__u16 __user *svc = &frame->svc_insn;
		if (__put_user(S390_SYSCALL_OPCODE | __NR_rt_sigreturn, svc))
			return -EFAULT;
		restorer = (unsigned long __force) svc | PSW32_ADDR_AMODE;
	}

	/* Create siginfo on the signal stack */
	if (copy_siginfo_to_user32(&frame->info, &ksig->info))
		return -EFAULT;

	/* Store registers needed to create the signal frame */
	store_sigregs();

	/* Create ucontext on the signal stack. */
	if (__put_user(uc_flags, &frame->uc.uc_flags) ||
	    __put_user(0, &frame->uc.uc_link) ||
	    __compat_save_altstack(&frame->uc.uc_stack, regs->gprs[15]) ||
	    save_sigregs32(regs, &frame->uc.uc_mcontext) ||
	    put_compat_sigset(&frame->uc.uc_sigmask, set, sizeof(compat_sigset_t)) ||
	    save_sigregs_ext32(regs, &frame->uc.uc_mcontext_ext))
		return -EFAULT;

	/* Set up registers for signal handler */
	regs->gprs[14] = restorer;
	regs->gprs[15] = (__force __u64) frame;
	/* Force 31 bit amode and default user address space control. */
	regs->psw.mask = PSW_MASK_BA |
		(PSW_USER_BITS & PSW_MASK_ASC) |
		(regs->psw.mask & ~PSW_MASK_ASC);
	regs->psw.addr = (__u64 __force) ksig->ka.sa.sa_handler;

	regs->gprs[2] = ksig->sig;
	regs->gprs[3] = (__force __u64) &frame->info;
	regs->gprs[4] = (__force __u64) &frame->uc;
	regs->gprs[5] = current->thread.last_break;
	return 0;
}

/*
 * OK, we're invoking a handler
 */	

void handle_signal32(struct ksignal *ksig, sigset_t *oldset,
		     struct pt_regs *regs)
{
	int ret;

	/* Set up the stack frame */
	if (ksig->ka.sa.sa_flags & SA_SIGINFO)
		ret = setup_rt_frame32(ksig, oldset, regs);
	else
		ret = setup_frame32(ksig, oldset, regs);

	signal_setup_done(ret, ksig, test_thread_flag(TIF_SINGLE_STEP));
}

