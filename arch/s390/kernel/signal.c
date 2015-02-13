/*
 *    Copyright IBM Corp. 1999, 2006
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *    Based on Intel version
 * 
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 */

#include <linux/sched.h>
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
#include <linux/tracehook.h>
#include <linux/syscalls.h>
#include <linux/compat.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/lowcore.h>
#include <asm/switch_to.h>
#include "entry.h"

/*
 * Layout of an old-style signal-frame:
 *	-----------------------------------------
 *	| save area (_SIGNAL_FRAMESIZE)		|
 *	-----------------------------------------
 *	| struct sigcontext			|
 *	|	oldmask				|
 *	|	_sigregs *			|
 *	-----------------------------------------
 *	| _sigregs with				|
 *	|	_s390_regs_common		|
 *	|	_s390_fp_regs			|
 *	-----------------------------------------
 *	| int signo				|
 *	-----------------------------------------
 *	| _sigregs_ext with			|
 *	|	gprs_high 64 byte (opt)		|
 *	|	vxrs_low 128 byte (opt)		|
 *	|	vxrs_high 256 byte (opt)	|
 *	|	reserved 128 byte (opt)		|
 *	-----------------------------------------
 *	| __u16 svc_insn			|
 *	-----------------------------------------
 * The svc_insn entry with the sigreturn system call opcode does not
 * have a fixed position and moves if gprs_high or vxrs exist.
 * Future extensions will be added to _sigregs_ext.
 */
struct sigframe
{
	__u8 callee_used_stack[__SIGNAL_FRAMESIZE];
	struct sigcontext sc;
	_sigregs sregs;
	int signo;
	_sigregs_ext sregs_ext;
	__u16 svc_insn;		/* Offset of svc_insn is NOT fixed! */
};

/*
 * Layout of an rt signal-frame:
 *	-----------------------------------------
 *	| save area (_SIGNAL_FRAMESIZE)		|
 *	-----------------------------------------
 *	| svc __NR_rt_sigreturn 2 byte		|
 *	-----------------------------------------
 *	| struct siginfo			|
 *	-----------------------------------------
 *	| struct ucontext_extended with		|
 *	|	unsigned long uc_flags		|
 *	|	struct ucontext *uc_link	|
 *	|	stack_t uc_stack		|
 *	|	_sigregs uc_mcontext with	|
 *	|		_s390_regs_common	|
 *	|		_s390_fp_regs		|
 *	|	sigset_t uc_sigmask		|
 *	|	_sigregs_ext uc_mcontext_ext	|
 *	|		gprs_high 64 byte (opt)	|
 *	|		vxrs_low 128 byte (opt)	|
 *	|		vxrs_high 256 byte (opt)|
 *	|		reserved 128 byte (opt)	|
 *	-----------------------------------------
 * Future extensions will be added to _sigregs_ext.
 */
struct rt_sigframe
{
	__u8 callee_used_stack[__SIGNAL_FRAMESIZE];
	__u16 svc_insn;
	struct siginfo info;
	struct ucontext_extended uc;
};

/* Store registers needed to create the signal frame */
static void store_sigregs(void)
{
	save_access_regs(current->thread.acrs);
	save_fp_ctl(&current->thread.fp_regs.fpc);
#ifdef CONFIG_64BIT
	if (current->thread.vxrs) {
		int i;

		save_vx_regs(current->thread.vxrs);
		for (i = 0; i < __NUM_FPRS; i++)
			current->thread.fp_regs.fprs[i] =
				*(freg_t *)(current->thread.vxrs + i);
	} else
#endif
		save_fp_regs(current->thread.fp_regs.fprs);
}

/* Load registers after signal return */
static void load_sigregs(void)
{
	restore_access_regs(current->thread.acrs);
	/* restore_fp_ctl is done in restore_sigregs */
#ifdef CONFIG_64BIT
	if (current->thread.vxrs) {
		int i;

		for (i = 0; i < __NUM_FPRS; i++)
			*(freg_t *)(current->thread.vxrs + i) =
				current->thread.fp_regs.fprs[i];
		restore_vx_regs(current->thread.vxrs);
	} else
#endif
		restore_fp_regs(current->thread.fp_regs.fprs);
}

/* Returns non-zero on fault. */
static int save_sigregs(struct pt_regs *regs, _sigregs __user *sregs)
{
	_sigregs user_sregs;

	/* Copy a 'clean' PSW mask to the user to avoid leaking
	   information about whether PER is currently on.  */
	user_sregs.regs.psw.mask = PSW_USER_BITS |
		(regs->psw.mask & (PSW_MASK_USER | PSW_MASK_RI));
	user_sregs.regs.psw.addr = regs->psw.addr;
	memcpy(&user_sregs.regs.gprs, &regs->gprs, sizeof(sregs->regs.gprs));
	memcpy(&user_sregs.regs.acrs, current->thread.acrs,
	       sizeof(user_sregs.regs.acrs));
	memcpy(&user_sregs.fpregs, &current->thread.fp_regs,
	       sizeof(user_sregs.fpregs));
	if (__copy_to_user(sregs, &user_sregs, sizeof(_sigregs)))
		return -EFAULT;
	return 0;
}

static int restore_sigregs(struct pt_regs *regs, _sigregs __user *sregs)
{
	_sigregs user_sregs;

	/* Alwys make any pending restarted system call return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	if (__copy_from_user(&user_sregs, sregs, sizeof(user_sregs)))
		return -EFAULT;

	if (!is_ri_task(current) && (user_sregs.regs.psw.mask & PSW_MASK_RI))
		return -EINVAL;

	/* Loading the floating-point-control word can fail. Do that first. */
	if (restore_fp_ctl(&user_sregs.fpregs.fpc))
		return -EINVAL;

	/* Use regs->psw.mask instead of PSW_USER_BITS to preserve PER bit. */
	regs->psw.mask = (regs->psw.mask & ~(PSW_MASK_USER | PSW_MASK_RI)) |
		(user_sregs.regs.psw.mask & (PSW_MASK_USER | PSW_MASK_RI));
	/* Check for invalid user address space control. */
	if ((regs->psw.mask & PSW_MASK_ASC) == PSW_ASC_HOME)
		regs->psw.mask = PSW_ASC_PRIMARY |
			(regs->psw.mask & ~PSW_MASK_ASC);
	/* Check for invalid amode */
	if (regs->psw.mask & PSW_MASK_EA)
		regs->psw.mask |= PSW_MASK_BA;
	regs->psw.addr = user_sregs.regs.psw.addr;
	memcpy(&regs->gprs, &user_sregs.regs.gprs, sizeof(sregs->regs.gprs));
	memcpy(&current->thread.acrs, &user_sregs.regs.acrs,
	       sizeof(current->thread.acrs));

	memcpy(&current->thread.fp_regs, &user_sregs.fpregs,
	       sizeof(current->thread.fp_regs));

	clear_pt_regs_flag(regs, PIF_SYSCALL); /* No longer in a system call */
	return 0;
}

/* Returns non-zero on fault. */
static int save_sigregs_ext(struct pt_regs *regs,
			    _sigregs_ext __user *sregs_ext)
{
#ifdef CONFIG_64BIT
	__u64 vxrs[__NUM_VXRS_LOW];
	int i;

	/* Save vector registers to signal stack */
	if (current->thread.vxrs) {
		for (i = 0; i < __NUM_VXRS_LOW; i++)
			vxrs[i] = *((__u64 *)(current->thread.vxrs + i) + 1);
		if (__copy_to_user(&sregs_ext->vxrs_low, vxrs,
				   sizeof(sregs_ext->vxrs_low)) ||
		    __copy_to_user(&sregs_ext->vxrs_high,
				   current->thread.vxrs + __NUM_VXRS_LOW,
				   sizeof(sregs_ext->vxrs_high)))
			return -EFAULT;
	}
#endif
	return 0;
}

static int restore_sigregs_ext(struct pt_regs *regs,
			       _sigregs_ext __user *sregs_ext)
{
#ifdef CONFIG_64BIT
	__u64 vxrs[__NUM_VXRS_LOW];
	int i;

	/* Restore vector registers from signal stack */
	if (current->thread.vxrs) {
		if (__copy_from_user(vxrs, &sregs_ext->vxrs_low,
				     sizeof(sregs_ext->vxrs_low)) ||
		    __copy_from_user(current->thread.vxrs + __NUM_VXRS_LOW,
				     &sregs_ext->vxrs_high,
				     sizeof(sregs_ext->vxrs_high)))
			return -EFAULT;
		for (i = 0; i < __NUM_VXRS_LOW; i++)
			*((__u64 *)(current->thread.vxrs + i) + 1) = vxrs[i];
	}
#endif
	return 0;
}

SYSCALL_DEFINE0(sigreturn)
{
	struct pt_regs *regs = task_pt_regs(current);
	struct sigframe __user *frame =
		(struct sigframe __user *) regs->gprs[15];
	sigset_t set;

	if (__copy_from_user(&set.sig, &frame->sc.oldmask, _SIGMASK_COPY_SIZE))
		goto badframe;
	set_current_blocked(&set);
	if (restore_sigregs(regs, &frame->sregs))
		goto badframe;
	if (restore_sigregs_ext(regs, &frame->sregs_ext))
		goto badframe;
	load_sigregs();
	return regs->gprs[2];
badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

SYSCALL_DEFINE0(rt_sigreturn)
{
	struct pt_regs *regs = task_pt_regs(current);
	struct rt_sigframe __user *frame =
		(struct rt_sigframe __user *)regs->gprs[15];
	sigset_t set;

	if (__copy_from_user(&set.sig, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;
	set_current_blocked(&set);
	if (restore_altstack(&frame->uc.uc_stack))
		goto badframe;
	if (restore_sigregs(regs, &frame->uc.uc_mcontext))
		goto badframe;
	if (restore_sigregs_ext(regs, &frame->uc.uc_mcontext_ext))
		goto badframe;
	load_sigregs();
	return regs->gprs[2];
badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

/*
 * Determine which stack to use..
 */
static inline void __user *
get_sigframe(struct k_sigaction *ka, struct pt_regs * regs, size_t frame_size)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = regs->gprs[15];

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

static inline int map_signal(int sig)
{
	if (current_thread_info()->exec_domain
	    && current_thread_info()->exec_domain->signal_invmap
	    && sig < 32)
		return current_thread_info()->exec_domain->signal_invmap[sig];
	else
		return sig;
}

static int setup_frame(int sig, struct k_sigaction *ka,
		       sigset_t *set, struct pt_regs * regs)
{
	struct sigframe __user *frame;
	struct sigcontext sc;
	unsigned long restorer;
	size_t frame_size;

	/*
	 * gprs_high are only present for a 31-bit task running on
	 * a 64-bit kernel (see compat_signal.c) but the space for
	 * gprs_high need to be allocated if vector registers are
	 * included in the signal frame on a 31-bit system.
	 */
	frame_size = sizeof(*frame) - sizeof(frame->sregs_ext);
	if (MACHINE_HAS_VX)
		frame_size += sizeof(frame->sregs_ext);
	frame = get_sigframe(ka, regs, frame_size);
	if (frame == (void __user *) -1UL)
		return -EFAULT;

	/* Set up backchain. */
	if (__put_user(regs->gprs[15], (addr_t __user *) frame))
		return -EFAULT;

	/* Create struct sigcontext on the signal stack */
	memcpy(&sc.oldmask, &set->sig, _SIGMASK_COPY_SIZE);
	sc.sregs = (_sigregs __user __force *) &frame->sregs;
	if (__copy_to_user(&frame->sc, &sc, sizeof(frame->sc)))
		return -EFAULT;

	/* Store registers needed to create the signal frame */
	store_sigregs();

	/* Create _sigregs on the signal stack */
	if (save_sigregs(regs, &frame->sregs))
		return -EFAULT;

	/* Place signal number on stack to allow backtrace from handler.  */
	if (__put_user(regs->gprs[2], (int __user *) &frame->signo))
		return -EFAULT;

	/* Create _sigregs_ext on the signal stack */
	if (save_sigregs_ext(regs, &frame->sregs_ext))
		return -EFAULT;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		restorer = (unsigned long) ka->sa.sa_restorer | PSW_ADDR_AMODE;
	} else {
		/* Signal frame without vector registers are short ! */
		__u16 __user *svc = (void __user *) frame + frame_size - 2;
		if (__put_user(S390_SYSCALL_OPCODE | __NR_sigreturn, svc))
			return -EFAULT;
		restorer = (unsigned long) svc | PSW_ADDR_AMODE;
	}

	/* Set up registers for signal handler */
	regs->gprs[14] = restorer;
	regs->gprs[15] = (unsigned long) frame;
	/* Force default amode and default user address space control. */
	regs->psw.mask = PSW_MASK_EA | PSW_MASK_BA |
		(PSW_USER_BITS & PSW_MASK_ASC) |
		(regs->psw.mask & ~PSW_MASK_ASC);
	regs->psw.addr = (unsigned long) ka->sa.sa_handler | PSW_ADDR_AMODE;

	regs->gprs[2] = map_signal(sig);
	regs->gprs[3] = (unsigned long) &frame->sc;

	/* We forgot to include these in the sigcontext.
	   To avoid breaking binary compatibility, they are passed as args. */
	if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL ||
	    sig == SIGTRAP || sig == SIGFPE) {
		/* set extra registers only for synchronous signals */
		regs->gprs[4] = regs->int_code & 127;
		regs->gprs[5] = regs->int_parm_long;
		regs->gprs[6] = task_thread_info(current)->last_break;
	}
	return 0;
}

static int setup_rt_frame(struct ksignal *ksig, sigset_t *set,
			  struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	unsigned long uc_flags, restorer;
	size_t frame_size;

	frame_size = sizeof(struct rt_sigframe) - sizeof(_sigregs_ext);
	/*
	 * gprs_high are only present for a 31-bit task running on
	 * a 64-bit kernel (see compat_signal.c) but the space for
	 * gprs_high need to be allocated if vector registers are
	 * included in the signal frame on a 31-bit system.
	 */
	uc_flags = 0;
#ifdef CONFIG_64BIT
	if (MACHINE_HAS_VX) {
		frame_size += sizeof(_sigregs_ext);
		if (current->thread.vxrs)
			uc_flags |= UC_VXRS;
	}
#endif
	frame = get_sigframe(&ksig->ka, regs, frame_size);
	if (frame == (void __user *) -1UL)
		return -EFAULT;

	/* Set up backchain. */
	if (__put_user(regs->gprs[15], (addr_t __user *) frame))
		return -EFAULT;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ksig->ka.sa.sa_flags & SA_RESTORER) {
		restorer = (unsigned long)
			ksig->ka.sa.sa_restorer | PSW_ADDR_AMODE;
	} else {
		__u16 __user *svc = &frame->svc_insn;
		if (__put_user(S390_SYSCALL_OPCODE | __NR_rt_sigreturn, svc))
			return -EFAULT;
		restorer = (unsigned long) svc | PSW_ADDR_AMODE;
	}

	/* Create siginfo on the signal stack */
	if (copy_siginfo_to_user(&frame->info, &ksig->info))
		return -EFAULT;

	/* Store registers needed to create the signal frame */
	store_sigregs();

	/* Create ucontext on the signal stack. */
	if (__put_user(uc_flags, &frame->uc.uc_flags) ||
	    __put_user(NULL, &frame->uc.uc_link) ||
	    __save_altstack(&frame->uc.uc_stack, regs->gprs[15]) ||
	    save_sigregs(regs, &frame->uc.uc_mcontext) ||
	    __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set)) ||
	    save_sigregs_ext(regs, &frame->uc.uc_mcontext_ext))
		return -EFAULT;

	/* Set up registers for signal handler */
	regs->gprs[14] = restorer;
	regs->gprs[15] = (unsigned long) frame;
	/* Force default amode and default user address space control. */
	regs->psw.mask = PSW_MASK_EA | PSW_MASK_BA |
		(PSW_USER_BITS & PSW_MASK_ASC) |
		(regs->psw.mask & ~PSW_MASK_ASC);
	regs->psw.addr = (unsigned long) ksig->ka.sa.sa_handler | PSW_ADDR_AMODE;

	regs->gprs[2] = map_signal(ksig->sig);
	regs->gprs[3] = (unsigned long) &frame->info;
	regs->gprs[4] = (unsigned long) &frame->uc;
	regs->gprs[5] = task_thread_info(current)->last_break;
	return 0;
}

static void handle_signal(struct ksignal *ksig, sigset_t *oldset,
			  struct pt_regs *regs)
{
	int ret;

	/* Set up the stack frame */
	if (ksig->ka.sa.sa_flags & SA_SIGINFO)
		ret = setup_rt_frame(ksig, oldset, regs);
	else
		ret = setup_frame(ksig->sig, &ksig->ka, oldset, regs);

	signal_setup_done(ret, ksig, test_thread_flag(TIF_SINGLE_STEP));
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals that
 * the kernel can handle, and then we build all the user-level signal handling
 * stack-frames in one go after that.
 */
void do_signal(struct pt_regs *regs)
{
	struct ksignal ksig;
	sigset_t *oldset = sigmask_to_save();

	/*
	 * Get signal to deliver. When running under ptrace, at this point
	 * the debugger may change all our registers, including the system
	 * call information.
	 */
	current_thread_info()->system_call =
		test_pt_regs_flag(regs, PIF_SYSCALL) ? regs->int_code : 0;

	if (get_signal(&ksig)) {
		/* Whee!  Actually deliver the signal.  */
		if (current_thread_info()->system_call) {
			regs->int_code = current_thread_info()->system_call;
			/* Check for system call restarting. */
			switch (regs->gprs[2]) {
			case -ERESTART_RESTARTBLOCK:
			case -ERESTARTNOHAND:
				regs->gprs[2] = -EINTR;
				break;
			case -ERESTARTSYS:
				if (!(ksig.ka.sa.sa_flags & SA_RESTART)) {
					regs->gprs[2] = -EINTR;
					break;
				}
			/* fallthrough */
			case -ERESTARTNOINTR:
				regs->gprs[2] = regs->orig_gpr2;
				regs->psw.addr =
					__rewind_psw(regs->psw,
						     regs->int_code >> 16);
				break;
			}
		}
		/* No longer in a system call */
		clear_pt_regs_flag(regs, PIF_SYSCALL);

		if (is_compat_task())
			handle_signal32(&ksig, oldset, regs);
		else
			handle_signal(&ksig, oldset, regs);
		return;
	}

	/* No handlers present - check for system call restart */
	clear_pt_regs_flag(regs, PIF_SYSCALL);
	if (current_thread_info()->system_call) {
		regs->int_code = current_thread_info()->system_call;
		switch (regs->gprs[2]) {
		case -ERESTART_RESTARTBLOCK:
			/* Restart with sys_restart_syscall */
			regs->int_code = __NR_restart_syscall;
		/* fallthrough */
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			/* Restart system call with magic TIF bit. */
			regs->gprs[2] = regs->orig_gpr2;
			set_pt_regs_flag(regs, PIF_SYSCALL);
			if (test_thread_flag(TIF_SINGLE_STEP))
				clear_pt_regs_flag(regs, PIF_PER_TRAP);
			break;
		}
	}

	/*
	 * If there's no signal to deliver, we just put the saved sigmask back.
	 */
	restore_saved_sigmask();
}

void do_notify_resume(struct pt_regs *regs)
{
	clear_thread_flag(TIF_NOTIFY_RESUME);
	tracehook_notify_resume(regs);
}
