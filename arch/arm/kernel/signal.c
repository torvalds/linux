/*
 *  linux/arch/arm/kernel/signal.c
 *
 *  Copyright (C) 1995-2009 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/personality.h>
#include <linux/uaccess.h>
#include <linux/tracehook.h>

#include <asm/elf.h>
#include <asm/cacheflush.h>
#include <asm/ucontext.h>
#include <asm/unistd.h>
#include <asm/vfp.h>

#include "signal.h"

/*
 * For ARM syscalls, we encode the syscall number into the instruction.
 */
#define SWI_SYS_SIGRETURN	(0xef000000|(__NR_sigreturn)|(__NR_OABI_SYSCALL_BASE))
#define SWI_SYS_RT_SIGRETURN	(0xef000000|(__NR_rt_sigreturn)|(__NR_OABI_SYSCALL_BASE))

/*
 * With EABI, the syscall number has to be loaded into r7.
 */
#define MOV_R7_NR_SIGRETURN	(0xe3a07000 | (__NR_sigreturn - __NR_SYSCALL_BASE))
#define MOV_R7_NR_RT_SIGRETURN	(0xe3a07000 | (__NR_rt_sigreturn - __NR_SYSCALL_BASE))

/*
 * For Thumb syscalls, we pass the syscall number via r7.  We therefore
 * need two 16-bit instructions.
 */
#define SWI_THUMB_SIGRETURN	(0xdf00 << 16 | 0x2700 | (__NR_sigreturn - __NR_SYSCALL_BASE))
#define SWI_THUMB_RT_SIGRETURN	(0xdf00 << 16 | 0x2700 | (__NR_rt_sigreturn - __NR_SYSCALL_BASE))

const unsigned long sigreturn_codes[7] = {
	MOV_R7_NR_SIGRETURN,    SWI_SYS_SIGRETURN,    SWI_THUMB_SIGRETURN,
	MOV_R7_NR_RT_SIGRETURN, SWI_SYS_RT_SIGRETURN, SWI_THUMB_RT_SIGRETURN,
};

#ifdef CONFIG_CRUNCH
static int preserve_crunch_context(struct crunch_sigframe __user *frame)
{
	char kbuf[sizeof(*frame) + 8];
	struct crunch_sigframe *kframe;

	/* the crunch context must be 64 bit aligned */
	kframe = (struct crunch_sigframe *)((unsigned long)(kbuf + 8) & ~7);
	kframe->magic = CRUNCH_MAGIC;
	kframe->size = CRUNCH_STORAGE_SIZE;
	crunch_task_copy(current_thread_info(), &kframe->storage);
	return __copy_to_user(frame, kframe, sizeof(*frame));
}

static int restore_crunch_context(struct crunch_sigframe __user *frame)
{
	char kbuf[sizeof(*frame) + 8];
	struct crunch_sigframe *kframe;

	/* the crunch context must be 64 bit aligned */
	kframe = (struct crunch_sigframe *)((unsigned long)(kbuf + 8) & ~7);
	if (__copy_from_user(kframe, frame, sizeof(*frame)))
		return -1;
	if (kframe->magic != CRUNCH_MAGIC ||
	    kframe->size != CRUNCH_STORAGE_SIZE)
		return -1;
	crunch_task_restore(current_thread_info(), &kframe->storage);
	return 0;
}
#endif

#ifdef CONFIG_IWMMXT

static int preserve_iwmmxt_context(struct iwmmxt_sigframe *frame)
{
	char kbuf[sizeof(*frame) + 8];
	struct iwmmxt_sigframe *kframe;

	/* the iWMMXt context must be 64 bit aligned */
	kframe = (struct iwmmxt_sigframe *)((unsigned long)(kbuf + 8) & ~7);
	kframe->magic = IWMMXT_MAGIC;
	kframe->size = IWMMXT_STORAGE_SIZE;
	iwmmxt_task_copy(current_thread_info(), &kframe->storage);
	return __copy_to_user(frame, kframe, sizeof(*frame));
}

static int restore_iwmmxt_context(struct iwmmxt_sigframe *frame)
{
	char kbuf[sizeof(*frame) + 8];
	struct iwmmxt_sigframe *kframe;

	/* the iWMMXt context must be 64 bit aligned */
	kframe = (struct iwmmxt_sigframe *)((unsigned long)(kbuf + 8) & ~7);
	if (__copy_from_user(kframe, frame, sizeof(*frame)))
		return -1;
	if (kframe->magic != IWMMXT_MAGIC ||
	    kframe->size != IWMMXT_STORAGE_SIZE)
		return -1;
	iwmmxt_task_restore(current_thread_info(), &kframe->storage);
	return 0;
}

#endif

#ifdef CONFIG_VFP

static int preserve_vfp_context(struct vfp_sigframe __user *frame)
{
	const unsigned long magic = VFP_MAGIC;
	const unsigned long size = VFP_STORAGE_SIZE;
	int err = 0;

	__put_user_error(magic, &frame->magic, err);
	__put_user_error(size, &frame->size, err);

	if (err)
		return -EFAULT;

	return vfp_preserve_user_clear_hwstate(&frame->ufp, &frame->ufp_exc);
}

static int restore_vfp_context(struct vfp_sigframe __user *frame)
{
	unsigned long magic;
	unsigned long size;
	int err = 0;

	__get_user_error(magic, &frame->magic, err);
	__get_user_error(size, &frame->size, err);

	if (err)
		return -EFAULT;
	if (magic != VFP_MAGIC || size != VFP_STORAGE_SIZE)
		return -EINVAL;

	return vfp_restore_user_hwstate(&frame->ufp, &frame->ufp_exc);
}

#endif

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
	struct aux_sigframe __user *aux;
	sigset_t set;
	int err;

	err = __copy_from_user(&set, &sf->uc.uc_sigmask, sizeof(set));
	if (err == 0)
		set_current_blocked(&set);

	__get_user_error(regs->ARM_r0, &sf->uc.uc_mcontext.arm_r0, err);
	__get_user_error(regs->ARM_r1, &sf->uc.uc_mcontext.arm_r1, err);
	__get_user_error(regs->ARM_r2, &sf->uc.uc_mcontext.arm_r2, err);
	__get_user_error(regs->ARM_r3, &sf->uc.uc_mcontext.arm_r3, err);
	__get_user_error(regs->ARM_r4, &sf->uc.uc_mcontext.arm_r4, err);
	__get_user_error(regs->ARM_r5, &sf->uc.uc_mcontext.arm_r5, err);
	__get_user_error(regs->ARM_r6, &sf->uc.uc_mcontext.arm_r6, err);
	__get_user_error(regs->ARM_r7, &sf->uc.uc_mcontext.arm_r7, err);
	__get_user_error(regs->ARM_r8, &sf->uc.uc_mcontext.arm_r8, err);
	__get_user_error(regs->ARM_r9, &sf->uc.uc_mcontext.arm_r9, err);
	__get_user_error(regs->ARM_r10, &sf->uc.uc_mcontext.arm_r10, err);
	__get_user_error(regs->ARM_fp, &sf->uc.uc_mcontext.arm_fp, err);
	__get_user_error(regs->ARM_ip, &sf->uc.uc_mcontext.arm_ip, err);
	__get_user_error(regs->ARM_sp, &sf->uc.uc_mcontext.arm_sp, err);
	__get_user_error(regs->ARM_lr, &sf->uc.uc_mcontext.arm_lr, err);
	__get_user_error(regs->ARM_pc, &sf->uc.uc_mcontext.arm_pc, err);
	__get_user_error(regs->ARM_cpsr, &sf->uc.uc_mcontext.arm_cpsr, err);

	err |= !valid_user_regs(regs);

	aux = (struct aux_sigframe __user *) sf->uc.uc_regspace;
#ifdef CONFIG_CRUNCH
	if (err == 0)
		err |= restore_crunch_context(&aux->crunch);
#endif
#ifdef CONFIG_IWMMXT
	if (err == 0 && test_thread_flag(TIF_USING_IWMMXT))
		err |= restore_iwmmxt_context(&aux->iwmmxt);
#endif
#ifdef CONFIG_VFP
	if (err == 0)
		err |= restore_vfp_context(&aux->vfp);
#endif

	return err;
}

asmlinkage int sys_sigreturn(struct pt_regs *regs)
{
	struct sigframe __user *frame;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (regs->ARM_sp & 7)
		goto badframe;

	frame = (struct sigframe __user *)regs->ARM_sp;

	if (!access_ok(VERIFY_READ, frame, sizeof (*frame)))
		goto badframe;

	if (restore_sigframe(regs, frame))
		goto badframe;

	return regs->ARM_r0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage int sys_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;

	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (regs->ARM_sp & 7)
		goto badframe;

	frame = (struct rt_sigframe __user *)regs->ARM_sp;

	if (!access_ok(VERIFY_READ, frame, sizeof (*frame)))
		goto badframe;

	if (restore_sigframe(regs, &frame->sig))
		goto badframe;

	if (restore_altstack(&frame->sig.uc.uc_stack))
		goto badframe;

	return regs->ARM_r0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

static int
setup_sigframe(struct sigframe __user *sf, struct pt_regs *regs, sigset_t *set)
{
	struct aux_sigframe __user *aux;
	int err = 0;

	__put_user_error(regs->ARM_r0, &sf->uc.uc_mcontext.arm_r0, err);
	__put_user_error(regs->ARM_r1, &sf->uc.uc_mcontext.arm_r1, err);
	__put_user_error(regs->ARM_r2, &sf->uc.uc_mcontext.arm_r2, err);
	__put_user_error(regs->ARM_r3, &sf->uc.uc_mcontext.arm_r3, err);
	__put_user_error(regs->ARM_r4, &sf->uc.uc_mcontext.arm_r4, err);
	__put_user_error(regs->ARM_r5, &sf->uc.uc_mcontext.arm_r5, err);
	__put_user_error(regs->ARM_r6, &sf->uc.uc_mcontext.arm_r6, err);
	__put_user_error(regs->ARM_r7, &sf->uc.uc_mcontext.arm_r7, err);
	__put_user_error(regs->ARM_r8, &sf->uc.uc_mcontext.arm_r8, err);
	__put_user_error(regs->ARM_r9, &sf->uc.uc_mcontext.arm_r9, err);
	__put_user_error(regs->ARM_r10, &sf->uc.uc_mcontext.arm_r10, err);
	__put_user_error(regs->ARM_fp, &sf->uc.uc_mcontext.arm_fp, err);
	__put_user_error(regs->ARM_ip, &sf->uc.uc_mcontext.arm_ip, err);
	__put_user_error(regs->ARM_sp, &sf->uc.uc_mcontext.arm_sp, err);
	__put_user_error(regs->ARM_lr, &sf->uc.uc_mcontext.arm_lr, err);
	__put_user_error(regs->ARM_pc, &sf->uc.uc_mcontext.arm_pc, err);
	__put_user_error(regs->ARM_cpsr, &sf->uc.uc_mcontext.arm_cpsr, err);

	__put_user_error(current->thread.trap_no, &sf->uc.uc_mcontext.trap_no, err);
	__put_user_error(current->thread.error_code, &sf->uc.uc_mcontext.error_code, err);
	__put_user_error(current->thread.address, &sf->uc.uc_mcontext.fault_address, err);
	__put_user_error(set->sig[0], &sf->uc.uc_mcontext.oldmask, err);

	err |= __copy_to_user(&sf->uc.uc_sigmask, set, sizeof(*set));

	aux = (struct aux_sigframe __user *) sf->uc.uc_regspace;
#ifdef CONFIG_CRUNCH
	if (err == 0)
		err |= preserve_crunch_context(&aux->crunch);
#endif
#ifdef CONFIG_IWMMXT
	if (err == 0 && test_thread_flag(TIF_USING_IWMMXT))
		err |= preserve_iwmmxt_context(&aux->iwmmxt);
#endif
#ifdef CONFIG_VFP
	if (err == 0)
		err |= preserve_vfp_context(&aux->vfp);
#endif
	__put_user_error(0, &aux->end_magic, err);

	return err;
}

static inline void __user *
get_sigframe(struct ksignal *ksig, struct pt_regs *regs, int framesize)
{
	unsigned long sp = sigsp(regs->ARM_sp, ksig);
	void __user *frame;

	/*
	 * ATPCS B01 mandates 8-byte alignment
	 */
	frame = (void __user *)((sp - framesize) & ~7);

	/*
	 * Check that we can actually write to the signal frame.
	 */
	if (!access_ok(VERIFY_WRITE, frame, framesize))
		frame = NULL;

	return frame;
}

/*
 * translate the signal
 */
static inline int map_sig(int sig)
{
	struct thread_info *thread = current_thread_info();
	if (sig < 32 && thread->exec_domain && thread->exec_domain->signal_invmap)
		sig = thread->exec_domain->signal_invmap[sig];
	return sig;
}

static int
setup_return(struct pt_regs *regs, struct ksignal *ksig,
	     unsigned long __user *rc, void __user *frame)
{
	unsigned long handler = (unsigned long)ksig->ka.sa.sa_handler;
	unsigned long retcode;
	int thumb = 0;
	unsigned long cpsr = regs->ARM_cpsr & ~(PSR_f | PSR_E_BIT);

	cpsr |= PSR_ENDSTATE;

	/*
	 * Maybe we need to deliver a 32-bit signal to a 26-bit task.
	 */
	if (ksig->ka.sa.sa_flags & SA_THIRTYTWO)
		cpsr = (cpsr & ~MODE_MASK) | USR_MODE;

#ifdef CONFIG_ARM_THUMB
	if (elf_hwcap & HWCAP_THUMB) {
		/*
		 * The LSB of the handler determines if we're going to
		 * be using THUMB or ARM mode for this signal handler.
		 */
		thumb = handler & 1;

		if (thumb) {
			cpsr |= PSR_T_BIT;
#if __LINUX_ARM_ARCH__ >= 7
			/* clear the If-Then Thumb-2 execution state */
			cpsr &= ~PSR_IT_MASK;
#endif
		} else
			cpsr &= ~PSR_T_BIT;
	}
#endif

	if (ksig->ka.sa.sa_flags & SA_RESTORER) {
		retcode = (unsigned long)ksig->ka.sa.sa_restorer;
	} else {
		unsigned int idx = thumb << 1;

		if (ksig->ka.sa.sa_flags & SA_SIGINFO)
			idx += 3;

		/*
		 * Put the sigreturn code on the stack no matter which return
		 * mechanism we use in order to remain ABI compliant
		 */
		if (__put_user(sigreturn_codes[idx],   rc) ||
		    __put_user(sigreturn_codes[idx+1], rc+1))
			return 1;

		if ((cpsr & MODE32_BIT) && !IS_ENABLED(CONFIG_ARM_MPU)) {
			/*
			 * 32-bit code can use the new high-page
			 * signal return code support except when the MPU has
			 * protected the vectors page from PL0
			 */
			retcode = KERN_SIGRETURN_CODE + (idx << 2) + thumb;
		} else {
			/*
			 * Ensure that the instruction cache sees
			 * the return code written onto the stack.
			 */
			flush_icache_range((unsigned long)rc,
					   (unsigned long)(rc + 2));

			retcode = ((unsigned long)rc) + thumb;
		}
	}

	regs->ARM_r0 = map_sig(ksig->sig);
	regs->ARM_sp = (unsigned long)frame;
	regs->ARM_lr = retcode;
	regs->ARM_pc = handler;
	regs->ARM_cpsr = cpsr;

	return 0;
}

static int
setup_frame(struct ksignal *ksig, sigset_t *set, struct pt_regs *regs)
{
	struct sigframe __user *frame = get_sigframe(ksig, regs, sizeof(*frame));
	int err = 0;

	if (!frame)
		return 1;

	/*
	 * Set uc.uc_flags to a value which sc.trap_no would never have.
	 */
	__put_user_error(0x5ac3c35a, &frame->uc.uc_flags, err);

	err |= setup_sigframe(frame, regs, set);
	if (err == 0)
		err = setup_return(regs, ksig, frame->retcode, frame);

	return err;
}

static int
setup_rt_frame(struct ksignal *ksig, sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame = get_sigframe(ksig, regs, sizeof(*frame));
	int err = 0;

	if (!frame)
		return 1;

	err |= copy_siginfo_to_user(&frame->info, &ksig->info);

	__put_user_error(0, &frame->sig.uc.uc_flags, err);
	__put_user_error(NULL, &frame->sig.uc.uc_link, err);

	err |= __save_altstack(&frame->sig.uc.uc_stack, regs->ARM_sp);
	err |= setup_sigframe(&frame->sig, regs, set);
	if (err == 0)
		err = setup_return(regs, ksig, frame->sig.retcode, frame);

	if (err == 0) {
		/*
		 * For realtime signals we must also set the second and third
		 * arguments for the signal handler.
		 *   -- Peter Maydell <pmaydell@chiark.greenend.org.uk> 2000-12-06
		 */
		regs->ARM_r1 = (unsigned long)&frame->info;
		regs->ARM_r2 = (unsigned long)&frame->sig.uc;
	}

	return err;
}

/*
 * OK, we're invoking a handler
 */	
static void handle_signal(struct ksignal *ksig, struct pt_regs *regs)
{
	sigset_t *oldset = sigmask_to_save();
	int ret;

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
static int do_signal(struct pt_regs *regs, int syscall)
{
	unsigned int retval = 0, continue_addr = 0, restart_addr = 0;
	struct ksignal ksig;
	int restart = 0;

	/*
	 * If we were from a system call, check for system call restarting...
	 */
	if (syscall) {
		continue_addr = regs->ARM_pc;
		restart_addr = continue_addr - (thumb_mode(regs) ? 2 : 4);
		retval = regs->ARM_r0;

		/*
		 * Prepare for system call restart.  We do this here so that a
		 * debugger will see the already changed PSW.
		 */
		switch (retval) {
		case -ERESTART_RESTARTBLOCK:
			restart -= 2;
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			restart++;
			regs->ARM_r0 = regs->ARM_ORIG_r0;
			regs->ARM_pc = restart_addr;
			break;
		}
	}

	/*
	 * Get the signal to deliver.  When running under ptrace, at this
	 * point the debugger may change all our registers ...
	 */
	/*
	 * Depending on the signal settings we may need to revert the
	 * decision to restart the system call.  But skip this if a
	 * debugger has chosen to restart at a different PC.
	 */
	if (get_signal(&ksig)) {
		/* handler */
		if (unlikely(restart) && regs->ARM_pc == restart_addr) {
			if (retval == -ERESTARTNOHAND ||
			    retval == -ERESTART_RESTARTBLOCK
			    || (retval == -ERESTARTSYS
				&& !(ksig.ka.sa.sa_flags & SA_RESTART))) {
				regs->ARM_r0 = -EINTR;
				regs->ARM_pc = continue_addr;
			}
		}
		handle_signal(&ksig, regs);
	} else {
		/* no handler */
		restore_saved_sigmask();
		if (unlikely(restart) && regs->ARM_pc == restart_addr) {
			regs->ARM_pc = continue_addr;
			return restart;
		}
	}
	return 0;
}

asmlinkage int
do_work_pending(struct pt_regs *regs, unsigned int thread_flags, int syscall)
{
	do {
		if (likely(thread_flags & _TIF_NEED_RESCHED)) {
			schedule();
		} else {
			if (unlikely(!user_mode(regs)))
				return 0;
			local_irq_enable();
			if (thread_flags & _TIF_SIGPENDING) {
				int restart = do_signal(regs, syscall);
				if (unlikely(restart)) {
					/*
					 * Restart without handlers.
					 * Deal with it without leaving
					 * the kernel space.
					 */
					return restart;
				}
				syscall = 0;
			} else {
				clear_thread_flag(TIF_NOTIFY_RESUME);
				tracehook_notify_resume(regs);
			}
		}
		local_irq_disable();
		thread_flags = current_thread_info()->flags;
	} while (thread_flags & _TIF_WORK_MASK);
	return 0;
}
