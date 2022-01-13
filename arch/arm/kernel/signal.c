// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/kernel/signal.c
 *
 *  Copyright (C) 1995-2009 Russell King
 */
#include <linux/errno.h>
#include <linux/random.h>
#include <linux/signal.h>
#include <linux/personality.h>
#include <linux/uaccess.h>
#include <linux/tracehook.h>
#include <linux/uprobes.h>
#include <linux/syscalls.h>

#include <asm/elf.h>
#include <asm/cacheflush.h>
#include <asm/traps.h>
#include <asm/unistd.h>
#include <asm/vfp.h>

#include "signal.h"

extern const unsigned long sigreturn_codes[17];

static unsigned long signal_return_offset;

#ifdef CONFIG_IWMMXT

static int preserve_iwmmxt_context(struct iwmmxt_sigframe __user *frame)
{
	char kbuf[sizeof(*frame) + 8];
	struct iwmmxt_sigframe *kframe;
	int err = 0;

	/* the iWMMXt context must be 64 bit aligned */
	kframe = (struct iwmmxt_sigframe *)((unsigned long)(kbuf + 8) & ~7);

	if (test_thread_flag(TIF_USING_IWMMXT)) {
		kframe->magic = IWMMXT_MAGIC;
		kframe->size = IWMMXT_STORAGE_SIZE;
		iwmmxt_task_copy(current_thread_info(), &kframe->storage);
	} else {
		/*
		 * For bug-compatibility with older kernels, some space
		 * has to be reserved for iWMMXt even if it's not used.
		 * Set the magic and size appropriately so that properly
		 * written userspace can skip it reliably:
		 */
		*kframe = (struct iwmmxt_sigframe) {
			.magic = DUMMY_MAGIC,
			.size  = IWMMXT_STORAGE_SIZE,
		};
	}

	err = __copy_to_user(frame, kframe, sizeof(*kframe));

	return err;
}

static int restore_iwmmxt_context(char __user **auxp)
{
	struct iwmmxt_sigframe __user *frame =
		(struct iwmmxt_sigframe __user *)*auxp;
	char kbuf[sizeof(*frame) + 8];
	struct iwmmxt_sigframe *kframe;

	/* the iWMMXt context must be 64 bit aligned */
	kframe = (struct iwmmxt_sigframe *)((unsigned long)(kbuf + 8) & ~7);
	if (__copy_from_user(kframe, frame, sizeof(*frame)))
		return -1;

	/*
	 * For non-iWMMXt threads: a single iwmmxt_sigframe-sized dummy
	 * block is discarded for compatibility with setup_sigframe() if
	 * present, but we don't mandate its presence.  If some other
	 * magic is here, it's not for us:
	 */
	if (!test_thread_flag(TIF_USING_IWMMXT) &&
	    kframe->magic != DUMMY_MAGIC)
		return 0;

	if (kframe->size != IWMMXT_STORAGE_SIZE)
		return -1;

	if (test_thread_flag(TIF_USING_IWMMXT)) {
		if (kframe->magic != IWMMXT_MAGIC)
			return -1;

		iwmmxt_task_restore(current_thread_info(), &kframe->storage);
	}

	*auxp += IWMMXT_STORAGE_SIZE;
	return 0;
}

#endif

#ifdef CONFIG_VFP

static int preserve_vfp_context(struct vfp_sigframe __user *frame)
{
	struct vfp_sigframe kframe;
	int err = 0;

	memset(&kframe, 0, sizeof(kframe));
	kframe.magic = VFP_MAGIC;
	kframe.size = VFP_STORAGE_SIZE;

	err = vfp_preserve_user_clear_hwstate(&kframe.ufp, &kframe.ufp_exc);
	if (err)
		return err;

	return __copy_to_user(frame, &kframe, sizeof(kframe));
}

static int restore_vfp_context(char __user **auxp)
{
	struct vfp_sigframe frame;
	int err;

	err = __copy_from_user(&frame, *auxp, sizeof(frame));
	if (err)
		return err;

	if (frame.magic != VFP_MAGIC || frame.size != VFP_STORAGE_SIZE)
		return -EINVAL;

	*auxp += sizeof(frame);
	return vfp_restore_user_hwstate(&frame.ufp, &frame.ufp_exc);
}

#endif

/*
 * Do a signal return; undo the signal stack.  These are aligned to 64-bit.
 */

static int restore_sigframe(struct pt_regs *regs, struct sigframe __user *sf)
{
	struct sigcontext context;
	char __user *aux;
	sigset_t set;
	int err;

	err = __copy_from_user(&set, &sf->uc.uc_sigmask, sizeof(set));
	if (err == 0)
		set_current_blocked(&set);

	err |= __copy_from_user(&context, &sf->uc.uc_mcontext, sizeof(context));
	if (err == 0) {
		regs->ARM_r0 = context.arm_r0;
		regs->ARM_r1 = context.arm_r1;
		regs->ARM_r2 = context.arm_r2;
		regs->ARM_r3 = context.arm_r3;
		regs->ARM_r4 = context.arm_r4;
		regs->ARM_r5 = context.arm_r5;
		regs->ARM_r6 = context.arm_r6;
		regs->ARM_r7 = context.arm_r7;
		regs->ARM_r8 = context.arm_r8;
		regs->ARM_r9 = context.arm_r9;
		regs->ARM_r10 = context.arm_r10;
		regs->ARM_fp = context.arm_fp;
		regs->ARM_ip = context.arm_ip;
		regs->ARM_sp = context.arm_sp;
		regs->ARM_lr = context.arm_lr;
		regs->ARM_pc = context.arm_pc;
		regs->ARM_cpsr = context.arm_cpsr;
	}

	err |= !valid_user_regs(regs);

	aux = (char __user *) sf->uc.uc_regspace;
#ifdef CONFIG_IWMMXT
	if (err == 0)
		err |= restore_iwmmxt_context(&aux);
#endif
#ifdef CONFIG_VFP
	if (err == 0)
		err |= restore_vfp_context(&aux);
#endif

	return err;
}

asmlinkage int sys_sigreturn(struct pt_regs *regs)
{
	struct sigframe __user *frame;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (regs->ARM_sp & 7)
		goto badframe;

	frame = (struct sigframe __user *)regs->ARM_sp;

	if (!access_ok(frame, sizeof (*frame)))
		goto badframe;

	if (restore_sigframe(regs, frame))
		goto badframe;

	return regs->ARM_r0;

badframe:
	force_sig(SIGSEGV);
	return 0;
}

asmlinkage int sys_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;

	/* Always make any pending restarted system calls return -EINTR */
	current->restart_block.fn = do_no_restart_syscall;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (regs->ARM_sp & 7)
		goto badframe;

	frame = (struct rt_sigframe __user *)regs->ARM_sp;

	if (!access_ok(frame, sizeof (*frame)))
		goto badframe;

	if (restore_sigframe(regs, &frame->sig))
		goto badframe;

	if (restore_altstack(&frame->sig.uc.uc_stack))
		goto badframe;

	return regs->ARM_r0;

badframe:
	force_sig(SIGSEGV);
	return 0;
}

static int
setup_sigframe(struct sigframe __user *sf, struct pt_regs *regs, sigset_t *set)
{
	struct aux_sigframe __user *aux;
	struct sigcontext context;
	int err = 0;

	context = (struct sigcontext) {
		.arm_r0        = regs->ARM_r0,
		.arm_r1        = regs->ARM_r1,
		.arm_r2        = regs->ARM_r2,
		.arm_r3        = regs->ARM_r3,
		.arm_r4        = regs->ARM_r4,
		.arm_r5        = regs->ARM_r5,
		.arm_r6        = regs->ARM_r6,
		.arm_r7        = regs->ARM_r7,
		.arm_r8        = regs->ARM_r8,
		.arm_r9        = regs->ARM_r9,
		.arm_r10       = regs->ARM_r10,
		.arm_fp        = regs->ARM_fp,
		.arm_ip        = regs->ARM_ip,
		.arm_sp        = regs->ARM_sp,
		.arm_lr        = regs->ARM_lr,
		.arm_pc        = regs->ARM_pc,
		.arm_cpsr      = regs->ARM_cpsr,

		.trap_no       = current->thread.trap_no,
		.error_code    = current->thread.error_code,
		.fault_address = current->thread.address,
		.oldmask       = set->sig[0],
	};

	err |= __copy_to_user(&sf->uc.uc_mcontext, &context, sizeof(context));

	err |= __copy_to_user(&sf->uc.uc_sigmask, set, sizeof(*set));

	aux = (struct aux_sigframe __user *) sf->uc.uc_regspace;
#ifdef CONFIG_IWMMXT
	if (err == 0)
		err |= preserve_iwmmxt_context(&aux->iwmmxt);
#endif
#ifdef CONFIG_VFP
	if (err == 0)
		err |= preserve_vfp_context(&aux->vfp);
#endif
	err |= __put_user(0, &aux->end_magic);

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
	if (!access_ok(frame, framesize))
		frame = NULL;

	return frame;
}

static int
setup_return(struct pt_regs *regs, struct ksignal *ksig,
	     unsigned long __user *rc, void __user *frame)
{
	unsigned long handler = (unsigned long)ksig->ka.sa.sa_handler;
	unsigned long handler_fdpic_GOT = 0;
	unsigned long retcode;
	unsigned int idx, thumb = 0;
	unsigned long cpsr = regs->ARM_cpsr & ~(PSR_f | PSR_E_BIT);
	bool fdpic = IS_ENABLED(CONFIG_BINFMT_ELF_FDPIC) &&
		     (current->personality & FDPIC_FUNCPTRS);

	if (fdpic) {
		unsigned long __user *fdpic_func_desc =
					(unsigned long __user *)handler;
		if (__get_user(handler, &fdpic_func_desc[0]) ||
		    __get_user(handler_fdpic_GOT, &fdpic_func_desc[1]))
			return 1;
	}

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

		/*
		 * Clear the If-Then Thumb-2 execution state.  ARM spec
		 * requires this to be all 000s in ARM mode.  Snapdragon
		 * S4/Krait misbehaves on a Thumb=>ARM signal transition
		 * without this.
		 *
		 * We must do this whenever we are running on a Thumb-2
		 * capable CPU, which includes ARMv6T2.  However, we elect
		 * to always do this to simplify the code; this field is
		 * marked UNK/SBZP for older architectures.
		 */
		cpsr &= ~PSR_IT_MASK;

		if (thumb) {
			cpsr |= PSR_T_BIT;
		} else
			cpsr &= ~PSR_T_BIT;
	}
#endif

	if (ksig->ka.sa.sa_flags & SA_RESTORER) {
		retcode = (unsigned long)ksig->ka.sa.sa_restorer;
		if (fdpic) {
			/*
			 * We need code to load the function descriptor.
			 * That code follows the standard sigreturn code
			 * (6 words), and is made of 3 + 2 words for each
			 * variant. The 4th copied word is the actual FD
			 * address that the assembly code expects.
			 */
			idx = 6 + thumb * 3;
			if (ksig->ka.sa.sa_flags & SA_SIGINFO)
				idx += 5;
			if (__put_user(sigreturn_codes[idx],   rc  ) ||
			    __put_user(sigreturn_codes[idx+1], rc+1) ||
			    __put_user(sigreturn_codes[idx+2], rc+2) ||
			    __put_user(retcode,                rc+3))
				return 1;
			goto rc_finish;
		}
	} else {
		idx = thumb << 1;
		if (ksig->ka.sa.sa_flags & SA_SIGINFO)
			idx += 3;

		/*
		 * Put the sigreturn code on the stack no matter which return
		 * mechanism we use in order to remain ABI compliant
		 */
		if (__put_user(sigreturn_codes[idx],   rc) ||
		    __put_user(sigreturn_codes[idx+1], rc+1))
			return 1;

rc_finish:
#ifdef CONFIG_MMU
		if (cpsr & MODE32_BIT) {
			struct mm_struct *mm = current->mm;

			/*
			 * 32-bit code can use the signal return page
			 * except when the MPU has protected the vectors
			 * page from PL0
			 */
			retcode = mm->context.sigpage + signal_return_offset +
				  (idx << 2) + thumb;
		} else
#endif
		{
			/*
			 * Ensure that the instruction cache sees
			 * the return code written onto the stack.
			 */
			flush_icache_range((unsigned long)rc,
					   (unsigned long)(rc + 3));

			retcode = ((unsigned long)rc) + thumb;
		}
	}

	regs->ARM_r0 = ksig->sig;
	regs->ARM_sp = (unsigned long)frame;
	regs->ARM_lr = retcode;
	regs->ARM_pc = handler;
	if (fdpic)
		regs->ARM_r9 = handler_fdpic_GOT;
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
	err = __put_user(0x5ac3c35a, &frame->uc.uc_flags);

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

	err |= __put_user(0, &frame->sig.uc.uc_flags);
	err |= __put_user(NULL, &frame->sig.uc.uc_link);

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
	 * Perform fixup for the pre-signal frame.
	 */
	rseq_signal_deliver(ksig, regs);

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
			fallthrough;
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
	/*
	 * The assembly code enters us with IRQs off, but it hasn't
	 * informed the tracing code of that for efficiency reasons.
	 * Update the trace code with the current status.
	 */
	trace_hardirqs_off();
	do {
		if (likely(thread_flags & _TIF_NEED_RESCHED)) {
			schedule();
		} else {
			if (unlikely(!user_mode(regs)))
				return 0;
			local_irq_enable();
			if (thread_flags & (_TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL)) {
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
			} else if (thread_flags & _TIF_UPROBE) {
				uprobe_notify_resume(regs);
			} else {
				tracehook_notify_resume(regs);
			}
		}
		local_irq_disable();
		thread_flags = read_thread_flags();
	} while (thread_flags & _TIF_WORK_MASK);
	return 0;
}

struct page *get_signal_page(void)
{
	unsigned long ptr;
	unsigned offset;
	struct page *page;
	void *addr;

	page = alloc_pages(GFP_KERNEL, 0);

	if (!page)
		return NULL;

	addr = page_address(page);

	/* Poison the entire page */
	memset32(addr, __opcode_to_mem_arm(0xe7fddef1),
		 PAGE_SIZE / sizeof(u32));

	/* Give the signal return code some randomness */
	offset = 0x200 + (get_random_int() & 0x7fc);
	signal_return_offset = offset;

	/* Copy signal return handlers into the page */
	memcpy(addr + offset, sigreturn_codes, sizeof(sigreturn_codes));

	/* Flush out all instructions in this page */
	ptr = (unsigned long)addr;
	flush_icache_range(ptr, ptr + PAGE_SIZE);

	return page;
}

#ifdef CONFIG_DEBUG_RSEQ
asmlinkage void do_rseq_syscall(struct pt_regs *regs)
{
	rseq_syscall(regs);
}
#endif

/*
 * Compile-time assertions for siginfo_t offsets. Check NSIG* as well, as
 * changes likely come with new fields that should be added below.
 */
static_assert(NSIGILL	== 11);
static_assert(NSIGFPE	== 15);
static_assert(NSIGSEGV	== 9);
static_assert(NSIGBUS	== 5);
static_assert(NSIGTRAP	== 6);
static_assert(NSIGCHLD	== 6);
static_assert(NSIGSYS	== 2);
static_assert(sizeof(siginfo_t) == 128);
static_assert(__alignof__(siginfo_t) == 4);
static_assert(offsetof(siginfo_t, si_signo)	== 0x00);
static_assert(offsetof(siginfo_t, si_errno)	== 0x04);
static_assert(offsetof(siginfo_t, si_code)	== 0x08);
static_assert(offsetof(siginfo_t, si_pid)	== 0x0c);
static_assert(offsetof(siginfo_t, si_uid)	== 0x10);
static_assert(offsetof(siginfo_t, si_tid)	== 0x0c);
static_assert(offsetof(siginfo_t, si_overrun)	== 0x10);
static_assert(offsetof(siginfo_t, si_status)	== 0x14);
static_assert(offsetof(siginfo_t, si_utime)	== 0x18);
static_assert(offsetof(siginfo_t, si_stime)	== 0x1c);
static_assert(offsetof(siginfo_t, si_value)	== 0x14);
static_assert(offsetof(siginfo_t, si_int)	== 0x14);
static_assert(offsetof(siginfo_t, si_ptr)	== 0x14);
static_assert(offsetof(siginfo_t, si_addr)	== 0x0c);
static_assert(offsetof(siginfo_t, si_addr_lsb)	== 0x10);
static_assert(offsetof(siginfo_t, si_lower)	== 0x14);
static_assert(offsetof(siginfo_t, si_upper)	== 0x18);
static_assert(offsetof(siginfo_t, si_pkey)	== 0x14);
static_assert(offsetof(siginfo_t, si_perf_data)	== 0x10);
static_assert(offsetof(siginfo_t, si_perf_type)	== 0x14);
static_assert(offsetof(siginfo_t, si_band)	== 0x0c);
static_assert(offsetof(siginfo_t, si_fd)	== 0x10);
static_assert(offsetof(siginfo_t, si_call_addr)	== 0x0c);
static_assert(offsetof(siginfo_t, si_syscall)	== 0x10);
static_assert(offsetof(siginfo_t, si_arch)	== 0x14);
