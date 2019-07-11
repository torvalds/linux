// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/unicore32/kernel/signal.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/personality.h>
#include <linux/uaccess.h>
#include <linux/tracehook.h>
#include <linux/elf.h>
#include <linux/unistd.h>

#include <asm/cacheflush.h>
#include <asm/ucontext.h>

/*
 * For UniCore syscalls, we encode the syscall number into the instruction.
 */
#define SWI_SYS_SIGRETURN	(0xff000000) /* error number for new abi */
#define SWI_SYS_RT_SIGRETURN	(0xff000000 | (__NR_rt_sigreturn))
#define SWI_SYS_RESTART		(0xff000000 | (__NR_restart_syscall))

#define KERN_SIGRETURN_CODE	(KUSER_VECPAGE_BASE + 0x00000500)
#define KERN_RESTART_CODE	(KERN_SIGRETURN_CODE + sizeof(sigreturn_codes))

const unsigned long sigreturn_codes[3] = {
	SWI_SYS_SIGRETURN, SWI_SYS_RT_SIGRETURN,
};

const unsigned long syscall_restart_code[2] = {
	SWI_SYS_RESTART,	/* swi	__NR_restart_syscall */
	0x69efc004,		/* ldr	pc, [sp], #4 */
};

/*
 * Do a signal return; undo the signal stack.  These are aligned to 64-bit.
 */
struct sigframe {
	struct ucontext uc;
	unsigned long retcode[2];
};

struct rt_sigframe {
	struct siginfo info;
	struct sigframe sig;
};

static int restore_sigframe(struct pt_regs *regs, struct sigframe __user *sf)
{
	sigset_t set;
	int err;

	err = __copy_from_user(&set, &sf->uc.uc_sigmask, sizeof(set));
	if (err == 0)
		set_current_blocked(&set);

	err |= __get_user(regs->UCreg_00, &sf->uc.uc_mcontext.regs.UCreg_00);
	err |= __get_user(regs->UCreg_01, &sf->uc.uc_mcontext.regs.UCreg_01);
	err |= __get_user(regs->UCreg_02, &sf->uc.uc_mcontext.regs.UCreg_02);
	err |= __get_user(regs->UCreg_03, &sf->uc.uc_mcontext.regs.UCreg_03);
	err |= __get_user(regs->UCreg_04, &sf->uc.uc_mcontext.regs.UCreg_04);
	err |= __get_user(regs->UCreg_05, &sf->uc.uc_mcontext.regs.UCreg_05);
	err |= __get_user(regs->UCreg_06, &sf->uc.uc_mcontext.regs.UCreg_06);
	err |= __get_user(regs->UCreg_07, &sf->uc.uc_mcontext.regs.UCreg_07);
	err |= __get_user(regs->UCreg_08, &sf->uc.uc_mcontext.regs.UCreg_08);
	err |= __get_user(regs->UCreg_09, &sf->uc.uc_mcontext.regs.UCreg_09);
	err |= __get_user(regs->UCreg_10, &sf->uc.uc_mcontext.regs.UCreg_10);
	err |= __get_user(regs->UCreg_11, &sf->uc.uc_mcontext.regs.UCreg_11);
	err |= __get_user(regs->UCreg_12, &sf->uc.uc_mcontext.regs.UCreg_12);
	err |= __get_user(regs->UCreg_13, &sf->uc.uc_mcontext.regs.UCreg_13);
	err |= __get_user(regs->UCreg_14, &sf->uc.uc_mcontext.regs.UCreg_14);
	err |= __get_user(regs->UCreg_15, &sf->uc.uc_mcontext.regs.UCreg_15);
	err |= __get_user(regs->UCreg_16, &sf->uc.uc_mcontext.regs.UCreg_16);
	err |= __get_user(regs->UCreg_17, &sf->uc.uc_mcontext.regs.UCreg_17);
	err |= __get_user(regs->UCreg_18, &sf->uc.uc_mcontext.regs.UCreg_18);
	err |= __get_user(regs->UCreg_19, &sf->uc.uc_mcontext.regs.UCreg_19);
	err |= __get_user(regs->UCreg_20, &sf->uc.uc_mcontext.regs.UCreg_20);
	err |= __get_user(regs->UCreg_21, &sf->uc.uc_mcontext.regs.UCreg_21);
	err |= __get_user(regs->UCreg_22, &sf->uc.uc_mcontext.regs.UCreg_22);
	err |= __get_user(regs->UCreg_23, &sf->uc.uc_mcontext.regs.UCreg_23);
	err |= __get_user(regs->UCreg_24, &sf->uc.uc_mcontext.regs.UCreg_24);
	err |= __get_user(regs->UCreg_25, &sf->uc.uc_mcontext.regs.UCreg_25);
	err |= __get_user(regs->UCreg_26, &sf->uc.uc_mcontext.regs.UCreg_26);
	err |= __get_user(regs->UCreg_fp, &sf->uc.uc_mcontext.regs.UCreg_fp);
	err |= __get_user(regs->UCreg_ip, &sf->uc.uc_mcontext.regs.UCreg_ip);
	err |= __get_user(regs->UCreg_sp, &sf->uc.uc_mcontext.regs.UCreg_sp);
	err |= __get_user(regs->UCreg_lr, &sf->uc.uc_mcontext.regs.UCreg_lr);
	err |= __get_user(regs->UCreg_pc, &sf->uc.uc_mcontext.regs.UCreg_pc);
	err |= __get_user(regs->UCreg_asr, &sf->uc.uc_mcontext.regs.UCreg_asr);

	err |= !valid_user_regs(regs);

	return err;
}

asmlinkage int __sys_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (regs->UCreg_sp & 7)
		goto badframe;

	frame = (struct rt_sigframe __user *)regs->UCreg_sp;

	if (!access_ok(frame, sizeof(*frame)))
		goto badframe;

	if (restore_sigframe(regs, &frame->sig))
		goto badframe;

	if (restore_altstack(&frame->sig.uc.uc_stack))
		goto badframe;

	return regs->UCreg_00;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

static int setup_sigframe(struct sigframe __user *sf, struct pt_regs *regs,
		sigset_t *set)
{
	int err = 0;

	err |= __put_user(regs->UCreg_00, &sf->uc.uc_mcontext.regs.UCreg_00);
	err |= __put_user(regs->UCreg_01, &sf->uc.uc_mcontext.regs.UCreg_01);
	err |= __put_user(regs->UCreg_02, &sf->uc.uc_mcontext.regs.UCreg_02);
	err |= __put_user(regs->UCreg_03, &sf->uc.uc_mcontext.regs.UCreg_03);
	err |= __put_user(regs->UCreg_04, &sf->uc.uc_mcontext.regs.UCreg_04);
	err |= __put_user(regs->UCreg_05, &sf->uc.uc_mcontext.regs.UCreg_05);
	err |= __put_user(regs->UCreg_06, &sf->uc.uc_mcontext.regs.UCreg_06);
	err |= __put_user(regs->UCreg_07, &sf->uc.uc_mcontext.regs.UCreg_07);
	err |= __put_user(regs->UCreg_08, &sf->uc.uc_mcontext.regs.UCreg_08);
	err |= __put_user(regs->UCreg_09, &sf->uc.uc_mcontext.regs.UCreg_09);
	err |= __put_user(regs->UCreg_10, &sf->uc.uc_mcontext.regs.UCreg_10);
	err |= __put_user(regs->UCreg_11, &sf->uc.uc_mcontext.regs.UCreg_11);
	err |= __put_user(regs->UCreg_12, &sf->uc.uc_mcontext.regs.UCreg_12);
	err |= __put_user(regs->UCreg_13, &sf->uc.uc_mcontext.regs.UCreg_13);
	err |= __put_user(regs->UCreg_14, &sf->uc.uc_mcontext.regs.UCreg_14);
	err |= __put_user(regs->UCreg_15, &sf->uc.uc_mcontext.regs.UCreg_15);
	err |= __put_user(regs->UCreg_16, &sf->uc.uc_mcontext.regs.UCreg_16);
	err |= __put_user(regs->UCreg_17, &sf->uc.uc_mcontext.regs.UCreg_17);
	err |= __put_user(regs->UCreg_18, &sf->uc.uc_mcontext.regs.UCreg_18);
	err |= __put_user(regs->UCreg_19, &sf->uc.uc_mcontext.regs.UCreg_19);
	err |= __put_user(regs->UCreg_20, &sf->uc.uc_mcontext.regs.UCreg_20);
	err |= __put_user(regs->UCreg_21, &sf->uc.uc_mcontext.regs.UCreg_21);
	err |= __put_user(regs->UCreg_22, &sf->uc.uc_mcontext.regs.UCreg_22);
	err |= __put_user(regs->UCreg_23, &sf->uc.uc_mcontext.regs.UCreg_23);
	err |= __put_user(regs->UCreg_24, &sf->uc.uc_mcontext.regs.UCreg_24);
	err |= __put_user(regs->UCreg_25, &sf->uc.uc_mcontext.regs.UCreg_25);
	err |= __put_user(regs->UCreg_26, &sf->uc.uc_mcontext.regs.UCreg_26);
	err |= __put_user(regs->UCreg_fp, &sf->uc.uc_mcontext.regs.UCreg_fp);
	err |= __put_user(regs->UCreg_ip, &sf->uc.uc_mcontext.regs.UCreg_ip);
	err |= __put_user(regs->UCreg_sp, &sf->uc.uc_mcontext.regs.UCreg_sp);
	err |= __put_user(regs->UCreg_lr, &sf->uc.uc_mcontext.regs.UCreg_lr);
	err |= __put_user(regs->UCreg_pc, &sf->uc.uc_mcontext.regs.UCreg_pc);
	err |= __put_user(regs->UCreg_asr, &sf->uc.uc_mcontext.regs.UCreg_asr);

	err |= __put_user(current->thread.trap_no,
			&sf->uc.uc_mcontext.trap_no);
	err |= __put_user(current->thread.error_code,
			&sf->uc.uc_mcontext.error_code);
	err |= __put_user(current->thread.address,
			&sf->uc.uc_mcontext.fault_address);
	err |= __put_user(set->sig[0], &sf->uc.uc_mcontext.oldmask);

	err |= __copy_to_user(&sf->uc.uc_sigmask, set, sizeof(*set));

	return err;
}

static inline void __user *get_sigframe(struct k_sigaction *ka,
		struct pt_regs *regs, int framesize)
{
	unsigned long sp = regs->UCreg_sp;
	void __user *frame;

	/*
	 * This is the X/Open sanctioned signal stack switching.
	 */
	if ((ka->sa.sa_flags & SA_ONSTACK) && !sas_ss_flags(sp))
		sp = current->sas_ss_sp + current->sas_ss_size;

	/*
	 * ATPCS B01 mandates 8-byte alignment
	 */
	frame = (void __user *)((sp - framesize) & ~7);

	/*
	 * Check that we can actually write to the signal frame.
	 */
	if (!access_ok(frame, framesize))
		frame = NULL;

	return frame;
}

static int setup_return(struct pt_regs *regs, struct k_sigaction *ka,
	     unsigned long __user *rc, void __user *frame, int usig)
{
	unsigned long handler = (unsigned long)ka->sa.sa_handler;
	unsigned long retcode;
	unsigned long asr = regs->UCreg_asr & ~PSR_f;

	unsigned int idx = 0;

	if (ka->sa.sa_flags & SA_SIGINFO)
		idx += 1;

	if (__put_user(sigreturn_codes[idx],   rc) ||
	    __put_user(sigreturn_codes[idx+1], rc+1))
		return 1;

	retcode = KERN_SIGRETURN_CODE + (idx << 2);

	regs->UCreg_00 = usig;
	regs->UCreg_sp = (unsigned long)frame;
	regs->UCreg_lr = retcode;
	regs->UCreg_pc = handler;
	regs->UCreg_asr = asr;

	return 0;
}

static int setup_frame(struct ksignal *ksig, sigset_t *set,
		       struct pt_regs *regs)
{
	struct sigframe __user *frame = get_sigframe(&ksig->ka, regs, sizeof(*frame));
	int err = 0;

	if (!frame)
		return 1;

	/*
	 * Set uc.uc_flags to a value which sc.trap_no would never have.
	 */
	err |= __put_user(0x5ac3c35a, &frame->uc.uc_flags);

	err |= setup_sigframe(frame, regs, set);
	if (err == 0)
		err |= setup_return(regs, &ksig->ka, frame->retcode, frame,
				    ksig->sig);

	return err;
}

static int setup_rt_frame(struct ksignal *ksig, sigset_t *set,
			  struct pt_regs *regs)
{
	struct rt_sigframe __user *frame =
			get_sigframe(&ksig->ka, regs, sizeof(*frame));
	int err = 0;

	if (!frame)
		return 1;

	err |= copy_siginfo_to_user(&frame->info, &ksig->info);

	err |= __put_user(0, &frame->sig.uc.uc_flags);
	err |= __put_user(NULL, &frame->sig.uc.uc_link);
	err |= __save_altstack(&frame->sig.uc.uc_stack, regs->UCreg_sp);
	err |= setup_sigframe(&frame->sig, regs, set);
	if (err == 0)
		err |= setup_return(regs, &ksig->ka, frame->sig.retcode, frame,
				    ksig->sig);

	if (err == 0) {
		/*
		 * For realtime signals we must also set the second and third
		 * arguments for the signal handler.
		 */
		regs->UCreg_01 = (unsigned long)&frame->info;
		regs->UCreg_02 = (unsigned long)&frame->sig.uc;
	}

	return err;
}

static inline void setup_syscall_restart(struct pt_regs *regs)
{
	regs->UCreg_00 = regs->UCreg_ORIG_00;
	regs->UCreg_pc -= 4;
}

/*
 * OK, we're invoking a handler
 */
static void handle_signal(struct ksignal *ksig, struct pt_regs *regs,
			  int syscall)
{
	struct thread_info *thread = current_thread_info();
	sigset_t *oldset = sigmask_to_save();
	int usig = ksig->sig;
	int ret;

	/*
	 * If we were from a system call, check for system call restarting...
	 */
	if (syscall) {
		switch (regs->UCreg_00) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
			regs->UCreg_00 = -EINTR;
			break;
		case -ERESTARTSYS:
			if (!(ksig->ka.sa.sa_flags & SA_RESTART)) {
				regs->UCreg_00 = -EINTR;
				break;
			}
			/* fallthrough */
		case -ERESTARTNOINTR:
			setup_syscall_restart(regs);
		}
	}

	/*
	 * Set up the stack frame
	 */
	if (ksig->ka.sa.sa_flags & SA_SIGINFO)
		ret = setup_rt_frame(ksig, oldset, regs);
	else
		ret = setup_frame(ksig, oldset, regs);

	/*
	 * Check that the resulting registers are actually sane.
	 */
	ret |= !valid_user_regs(regs);

	signal_setup_done(ret, ksig, 0);
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
static void do_signal(struct pt_regs *regs, int syscall)
{
	struct ksignal ksig;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return;

	if (get_signal(&ksig)) {
		handle_signal(&ksig, regs, syscall);
		return;
	}

	/*
	 * No signal to deliver to the process - restart the syscall.
	 */
	if (syscall) {
		if (regs->UCreg_00 == -ERESTART_RESTARTBLOCK) {
				u32 __user *usp;

				regs->UCreg_sp -= 4;
				usp = (u32 __user *)regs->UCreg_sp;

				if (put_user(regs->UCreg_pc, usp) == 0) {
					regs->UCreg_pc = KERN_RESTART_CODE;
				} else {
					regs->UCreg_sp += 4;
					force_sigsegv(0, current);
				}
		}
		if (regs->UCreg_00 == -ERESTARTNOHAND ||
		    regs->UCreg_00 == -ERESTARTSYS ||
		    regs->UCreg_00 == -ERESTARTNOINTR) {
			setup_syscall_restart(regs);
		}
	}
	/* If there's no signal to deliver, we just put the saved
	 * sigmask back.
	 */
	restore_saved_sigmask();
}

asmlinkage void do_notify_resume(struct pt_regs *regs,
		unsigned int thread_flags, int syscall)
{
	if (thread_flags & _TIF_SIGPENDING)
		do_signal(regs, syscall);

	if (thread_flags & _TIF_NOTIFY_RESUME) {
		clear_thread_flag(TIF_NOTIFY_RESUME);
		tracehook_notify_resume(regs);
	}
}

/*
 * Copy signal return handlers into the vector page, and
 * set sigreturn to be a pointer to these.
 */
void __init early_signal_init(void)
{
	memcpy((void *)kuser_vecpage_to_vectors(KERN_SIGRETURN_CODE),
			sigreturn_codes, sizeof(sigreturn_codes));
	memcpy((void *)kuser_vecpage_to_vectors(KERN_RESTART_CODE),
			syscall_restart_code, sizeof(syscall_restart_code));
	/* Need not to flush icache, since early_trap_init will do it last. */
}
