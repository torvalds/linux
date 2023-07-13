// SPDX-License-Identifier: GPL-2.0
/*
 *  PA-RISC architecture-specific signal handling support.
 *
 *  Copyright (C) 2000 David Huggins-Daines <dhd@debian.org>
 *  Copyright (C) 2000 Linuxcare, Inc.
 *  Copyright (C) 2000-2022 Helge Deller <deller@gmx.de>
 *  Copyright (C) 2022 John David Anglin <dave.anglin@bell.net>
 *
 *  Based on the ia64, i386, and alpha versions.
 */

#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/resume_user_mode.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/compat.h>
#include <linux/elf.h>
#include <asm/ucontext.h>
#include <asm/rt_sigframe.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/asm-offsets.h>
#include <asm/vdso.h>

#ifdef CONFIG_COMPAT
#include "signal32.h"
#endif

#define DEBUG_SIG 0 
#define DEBUG_SIG_LEVEL 2

#if DEBUG_SIG
#define DBG(LEVEL, ...) \
        ((DEBUG_SIG_LEVEL >= LEVEL) \
	? printk(__VA_ARGS__) : (void) 0)
#else
#define DBG(LEVEL, ...)
#endif
	
/* gcc will complain if a pointer is cast to an integer of different
 * size.  If you really need to do this (and we do for an ELF32 user
 * application in an ELF64 kernel) then you have to do a cast to an
 * integer of the same size first.  The A() macro accomplishes
 * this. */
#define A(__x)	((unsigned long)(__x))

/*
 * Do a signal return - restore sigcontext.
 */

static long
restore_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs)
{
	long err = 0;

	err |= __copy_from_user(regs->gr, sc->sc_gr, sizeof(regs->gr));
	err |= __copy_from_user(regs->fr, sc->sc_fr, sizeof(regs->fr));
	err |= __copy_from_user(regs->iaoq, sc->sc_iaoq, sizeof(regs->iaoq));
	err |= __copy_from_user(regs->iasq, sc->sc_iasq, sizeof(regs->iasq));
	err |= __get_user(regs->sar, &sc->sc_sar);
	DBG(2, "%s: iaoq is %#lx / %#lx\n",
			__func__, regs->iaoq[0], regs->iaoq[1]);
	DBG(2, "%s: r28 is %ld\n", __func__, regs->gr[28]);
	return err;
}

asmlinkage void
sys_rt_sigreturn(struct pt_regs *regs, int in_syscall)
{
	struct rt_sigframe __user *frame;
	sigset_t set;
	unsigned long usp = (regs->gr[30] & ~(0x01UL));
	unsigned long sigframe_size = PARISC_RT_SIGFRAME_SIZE;
#ifdef CONFIG_64BIT
	struct compat_rt_sigframe __user * compat_frame;
	
	if (is_compat_task())
		sigframe_size = PARISC_RT_SIGFRAME_SIZE32;
#endif

	current->restart_block.fn = do_no_restart_syscall;

	/* Unwind the user stack to get the rt_sigframe structure. */
	frame = (struct rt_sigframe __user *)
		(usp - sigframe_size);
	DBG(2, "%s: frame is %p pid %d\n", __func__, frame, task_pid_nr(current));

	regs->orig_r28 = 1; /* no restarts for sigreturn */

#ifdef CONFIG_64BIT
	compat_frame = (struct compat_rt_sigframe __user *)frame;
	
	if (is_compat_task()) {
		if (get_compat_sigset(&set, &compat_frame->uc.uc_sigmask))
			goto give_sigsegv;
	} else
#endif
	{
		if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
			goto give_sigsegv;
	}
		
	set_current_blocked(&set);

	/* Good thing we saved the old gr[30], eh? */
#ifdef CONFIG_64BIT
	if (is_compat_task()) {
		DBG(1, "%s: compat_frame->uc.uc_mcontext 0x%p\n",
				__func__, &compat_frame->uc.uc_mcontext);
// FIXME: Load upper half from register file
		if (restore_sigcontext32(&compat_frame->uc.uc_mcontext, 
					&compat_frame->regs, regs))
			goto give_sigsegv;
		DBG(1, "%s: usp %#08lx stack 0x%p\n",
				__func__, usp, &compat_frame->uc.uc_stack);
		if (compat_restore_altstack(&compat_frame->uc.uc_stack))
			goto give_sigsegv;
	} else
#endif
	{
		DBG(1, "%s: frame->uc.uc_mcontext 0x%p\n",
				__func__, &frame->uc.uc_mcontext);
		if (restore_sigcontext(&frame->uc.uc_mcontext, regs))
			goto give_sigsegv;
		DBG(1, "%s: usp %#08lx stack 0x%p\n",
				__func__, usp, &frame->uc.uc_stack);
		if (restore_altstack(&frame->uc.uc_stack))
			goto give_sigsegv;
	}
		


	/* If we are on the syscall path IAOQ will not be restored, and
	 * if we are on the interrupt path we must not corrupt gr31.
	 */
	if (in_syscall)
		regs->gr[31] = regs->iaoq[0];

	return;

give_sigsegv:
	DBG(1, "%s: Sending SIGSEGV\n", __func__);
	force_sig(SIGSEGV);
	return;
}

/*
 * Set up a signal frame.
 */

static inline void __user *
get_sigframe(struct k_sigaction *ka, unsigned long sp, size_t frame_size)
{
	/*FIXME: ELF32 vs. ELF64 has different frame_size, but since we
	  don't use the parameter it doesn't matter */

	DBG(1, "%s: ka = %#lx, sp = %#lx, frame_size = %zu\n",
			__func__, (unsigned long)ka, sp, frame_size);
	
	/* Align alternate stack and reserve 64 bytes for the signal
	   handler's frame marker.  */
	if ((ka->sa.sa_flags & SA_ONSTACK) != 0 && ! sas_ss_flags(sp))
		sp = (current->sas_ss_sp + 0x7f) & ~0x3f; /* Stacks grow up! */

	DBG(1, "%s: Returning sp = %#lx\n", __func__, (unsigned long)sp);
	return (void __user *) sp; /* Stacks grow up.  Fun. */
}

static long
setup_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs, long in_syscall)
		 
{
	unsigned long flags = 0;
	long err = 0;

	if (on_sig_stack((unsigned long) sc))
		flags |= PARISC_SC_FLAG_ONSTACK;
	if (in_syscall) {
		flags |= PARISC_SC_FLAG_IN_SYSCALL;
		/* regs->iaoq is undefined in the syscall return path */
		err |= __put_user(regs->gr[31], &sc->sc_iaoq[0]);
		err |= __put_user(regs->gr[31]+4, &sc->sc_iaoq[1]);
		err |= __put_user(regs->sr[3], &sc->sc_iasq[0]);
		err |= __put_user(regs->sr[3], &sc->sc_iasq[1]);
		DBG(1, "%s: iaoq %#lx / %#lx (in syscall)\n",
			__func__, regs->gr[31], regs->gr[31]+4);
	} else {
		err |= __copy_to_user(sc->sc_iaoq, regs->iaoq, sizeof(regs->iaoq));
		err |= __copy_to_user(sc->sc_iasq, regs->iasq, sizeof(regs->iasq));
		DBG(1, "%s: iaoq %#lx / %#lx (not in syscall)\n",
			__func__, regs->iaoq[0], regs->iaoq[1]);
	}

	err |= __put_user(flags, &sc->sc_flags);
	err |= __copy_to_user(sc->sc_gr, regs->gr, sizeof(regs->gr));
	err |= __copy_to_user(sc->sc_fr, regs->fr, sizeof(regs->fr));
	err |= __put_user(regs->sar, &sc->sc_sar);
	DBG(1, "%s: r28 is %ld\n", __func__, regs->gr[28]);

	return err;
}

static long
setup_rt_frame(struct ksignal *ksig, sigset_t *set, struct pt_regs *regs,
	       long in_syscall)
{
	struct rt_sigframe __user *frame;
	unsigned long rp, usp;
	unsigned long haddr, sigframe_size;
	unsigned long start;
	int err = 0;
#ifdef CONFIG_64BIT
	struct compat_rt_sigframe __user * compat_frame;
#endif
	
	usp = (regs->gr[30] & ~(0x01UL));
	sigframe_size = PARISC_RT_SIGFRAME_SIZE;
#ifdef CONFIG_64BIT
	if (is_compat_task()) {
		/* The gcc alloca implementation leaves garbage in the upper 32 bits of sp */
		usp = (compat_uint_t)usp;
		sigframe_size = PARISC_RT_SIGFRAME_SIZE32;
	}
#endif
	frame = get_sigframe(&ksig->ka, usp, sigframe_size);

	DBG(1, "%s: frame %p info %p\n", __func__, frame, &ksig->info);

	start = (unsigned long) frame;
	if (start >= TASK_SIZE_MAX - sigframe_size)
		return -EFAULT;
	
#ifdef CONFIG_64BIT

	compat_frame = (struct compat_rt_sigframe __user *)frame;
	
	if (is_compat_task()) {
		DBG(1, "%s: frame->info = 0x%p\n", __func__, &compat_frame->info);
		err |= copy_siginfo_to_user32(&compat_frame->info, &ksig->info);
		err |= __compat_save_altstack( &compat_frame->uc.uc_stack, regs->gr[30]);
		DBG(1, "%s: frame->uc = 0x%p\n", __func__, &compat_frame->uc);
		DBG(1, "%s: frame->uc.uc_mcontext = 0x%p\n",
			__func__, &compat_frame->uc.uc_mcontext);
		err |= setup_sigcontext32(&compat_frame->uc.uc_mcontext, 
					&compat_frame->regs, regs, in_syscall);
		err |= put_compat_sigset(&compat_frame->uc.uc_sigmask, set,
					 sizeof(compat_sigset_t));
	} else
#endif
	{	
		DBG(1, "%s: frame->info = 0x%p\n", __func__, &frame->info);
		err |= copy_siginfo_to_user(&frame->info, &ksig->info);
		err |= __save_altstack(&frame->uc.uc_stack, regs->gr[30]);
		DBG(1, "%s: frame->uc = 0x%p\n", __func__, &frame->uc);
		DBG(1, "%s: frame->uc.uc_mcontext = 0x%p\n",
			__func__, &frame->uc.uc_mcontext);
		err |= setup_sigcontext(&frame->uc.uc_mcontext, regs, in_syscall);
		/* FIXME: Should probably be converted as well for the compat case */
		err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	}
	
	if (err)
		return -EFAULT;

#ifdef CONFIG_64BIT
	if (!is_compat_task())
		rp = VDSO64_SYMBOL(current, sigtramp_rt);
	else
#endif
		rp = VDSO32_SYMBOL(current, sigtramp_rt);

	if (in_syscall)
		rp += 4*4; /* skip 4 instructions and start at ldi 1,%r25 */

	haddr = A(ksig->ka.sa.sa_handler);
	/* The sa_handler may be a pointer to a function descriptor */
#ifdef CONFIG_64BIT
	if (is_compat_task()) {
#endif
		if (haddr & PA_PLABEL_FDESC) {
			Elf32_Fdesc fdesc;
			Elf32_Fdesc __user *ufdesc = (Elf32_Fdesc __user *)A(haddr & ~3);

			err = __copy_from_user(&fdesc, ufdesc, sizeof(fdesc));

			if (err)
				return -EFAULT;

			haddr = fdesc.addr;
			regs->gr[19] = fdesc.gp;
		}
#ifdef CONFIG_64BIT
	} else {
		Elf64_Fdesc fdesc;
		Elf64_Fdesc __user *ufdesc = (Elf64_Fdesc __user *)A(haddr & ~3);
		
		err = __copy_from_user(&fdesc, ufdesc, sizeof(fdesc));
		
		if (err)
			return -EFAULT;
		
		haddr = fdesc.addr;
		regs->gr[19] = fdesc.gp;
		DBG(1, "%s: 64 bit signal, exe=%#lx, r19=%#lx, in_syscall=%d\n",
		     __func__, haddr, regs->gr[19], in_syscall);
	}
#endif

	/* The syscall return path will create IAOQ values from r31.
	 */
	if (in_syscall) {
		regs->gr[31] = haddr;
#ifdef CONFIG_64BIT
		if (!test_thread_flag(TIF_32BIT))
			sigframe_size |= 1; /* XXX ???? */
#endif
	} else {
		unsigned long psw = USER_PSW;
#ifdef CONFIG_64BIT
		if (!test_thread_flag(TIF_32BIT))
			psw |= PSW_W;
#endif

		/* If we are singlestepping, arrange a trap to be delivered
		   when we return to userspace. Note the semantics -- we
		   should trap before the first insn in the handler is
		   executed. Ref:
			http://sources.redhat.com/ml/gdb/2004-11/msg00245.html
		 */
		if (pa_psw(current)->r) {
			pa_psw(current)->r = 0;
			psw |= PSW_R;
			mtctl(-1, 0);
		}

		regs->gr[0] = psw;
		regs->iaoq[0] = haddr | PRIV_USER;
		regs->iaoq[1] = regs->iaoq[0] + 4;
	}

	regs->gr[2]  = rp;			/* userland return pointer */
	regs->gr[26] = ksig->sig;               /* signal number */
	
#ifdef CONFIG_64BIT
	if (is_compat_task()) {
		regs->gr[25] = A(&compat_frame->info); /* siginfo pointer */
		regs->gr[24] = A(&compat_frame->uc);   /* ucontext pointer */
	} else
#endif
	{		
		regs->gr[25] = A(&frame->info); /* siginfo pointer */
		regs->gr[24] = A(&frame->uc);   /* ucontext pointer */
	}
	
	DBG(1, "%s: making sigreturn frame: %#lx + %#lx = %#lx\n", __func__,
	       regs->gr[30], sigframe_size,
	       regs->gr[30] + sigframe_size);
	/* Raise the user stack pointer to make a proper call frame. */
	regs->gr[30] = (A(frame) + sigframe_size);


	DBG(1, "%s: sig deliver (%s,%d) frame=0x%p sp=%#lx iaoq=%#lx/%#lx rp=%#lx\n",
	       __func__, current->comm, current->pid, frame, regs->gr[30],
	       regs->iaoq[0], regs->iaoq[1], rp);

	return 0;
}

/*
 * OK, we're invoking a handler.
 */	

static void
handle_signal(struct ksignal *ksig, struct pt_regs *regs, long in_syscall)
{
	int ret;
	sigset_t *oldset = sigmask_to_save();

	DBG(1, "%s: sig=%d, ka=%p, info=%p, oldset=%p, regs=%p\n",
	       __func__, ksig->sig, &ksig->ka, &ksig->info, oldset, regs);
	
	/* Set up the stack frame */
	ret = setup_rt_frame(ksig, oldset, regs, in_syscall);

	signal_setup_done(ret, ksig, test_thread_flag(TIF_SINGLESTEP) ||
			  test_thread_flag(TIF_BLOCKSTEP));

	DBG(1, "%s: Exit (success), regs->gr[28] = %ld\n",
		__func__, regs->gr[28]);
}

/*
 * Check how the syscall number gets loaded into %r20 within
 * the delay branch in userspace and adjust as needed.
 */

static void check_syscallno_in_delay_branch(struct pt_regs *regs)
{
	u32 opcode, source_reg;
	u32 __user *uaddr;
	int err;

	/* Usually we don't have to restore %r20 (the system call number)
	 * because it gets loaded in the delay slot of the branch external
	 * instruction via the ldi instruction.
	 * In some cases a register-to-register copy instruction might have
	 * been used instead, in which case we need to copy the syscall
	 * number into the source register before returning to userspace.
	 */

	/* A syscall is just a branch, so all we have to do is fiddle the
	 * return pointer so that the ble instruction gets executed again.
	 */
	regs->gr[31] -= 8; /* delayed branching */

	/* Get assembler opcode of code in delay branch */
	uaddr = (unsigned int *) ((regs->gr[31] & ~3) + 4);
	err = get_user(opcode, uaddr);
	if (err)
		return;

	/* Check if delay branch uses "ldi int,%r20" */
	if ((opcode & 0xffff0000) == 0x34140000)
		return;	/* everything ok, just return */

	/* Check if delay branch uses "nop" */
	if (opcode == INSN_NOP)
		return;

	/* Check if delay branch uses "copy %rX,%r20" */
	if ((opcode & 0xffe0ffff) == 0x08000254) {
		source_reg = (opcode >> 16) & 31;
		regs->gr[source_reg] = regs->gr[20];
		return;
	}

	pr_warn("syscall restart: %s (pid %d): unexpected opcode 0x%08x\n",
		current->comm, task_pid_nr(current), opcode);
}

static inline void
syscall_restart(struct pt_regs *regs, struct k_sigaction *ka)
{
	if (regs->orig_r28)
		return;
	regs->orig_r28 = 1; /* no more restarts */

	DBG(1, "%s:  orig_r28 = %ld  pid %d  r20 %ld\n",
		__func__, regs->orig_r28, task_pid_nr(current), regs->gr[20]);

	/* Check the return code */
	switch (regs->gr[28]) {
	case -ERESTART_RESTARTBLOCK:
	case -ERESTARTNOHAND:
		DBG(1, "%s: ERESTARTNOHAND: returning -EINTR\n", __func__);
		regs->gr[28] = -EINTR;
		break;
	case -ERESTARTSYS:
		if (!(ka->sa.sa_flags & SA_RESTART)) {
			DBG(1, "%s: ERESTARTSYS: putting -EINTR pid %d\n",
				__func__, task_pid_nr(current));
			regs->gr[28] = -EINTR;
			break;
		}
		fallthrough;
	case -ERESTARTNOINTR:
		DBG(1, "%s: %ld\n", __func__, regs->gr[28]);
		check_syscallno_in_delay_branch(regs);
		break;
	}
}

static inline void
insert_restart_trampoline(struct pt_regs *regs)
{
	if (regs->orig_r28)
		return;
	regs->orig_r28 = 1; /* no more restarts */

	DBG(2, "%s: gr28 = %ld pid %d\n",
		__func__, regs->gr[28], task_pid_nr(current));

	switch (regs->gr[28]) {
	case -ERESTART_RESTARTBLOCK: {
		/* Restart the system call - no handlers present */
		unsigned int *usp = (unsigned int *)regs->gr[30];
		unsigned long rp;
		long err = 0;

		/* check that we don't exceed the stack */
		if (A(&usp[0]) >= TASK_SIZE_MAX - 5 * sizeof(int))
			return;

		/* Call trampoline in vdso to restart the syscall
		 * with __NR_restart_syscall.
		 * Original return addresses are on stack like this:
		 *
		 *  0: <return address (orig r31)>
		 *  4: <2nd half for 64-bit>
		 */
#ifdef CONFIG_64BIT
		if (!is_compat_task()) {
			err |= put_user(regs->gr[31] >> 32, &usp[0]);
			err |= put_user(regs->gr[31] & 0xffffffff, &usp[1]);
			rp = VDSO64_SYMBOL(current, restart_syscall);
		} else
#endif
		{
			err |= put_user(regs->gr[31], &usp[0]);
			rp = VDSO32_SYMBOL(current, restart_syscall);
		}
		WARN_ON(err);

		regs->gr[31] = rp;
		DBG(1, "%s: ERESTART_RESTARTBLOCK\n", __func__);
		return;
	}
	case -EINTR:
		/* ok, was handled before and should be returned. */
		break;
	case -ERESTARTNOHAND:
	case -ERESTARTSYS:
	case -ERESTARTNOINTR:
		DBG(1, "%s: Type %ld\n", __func__, regs->gr[28]);
		check_syscallno_in_delay_branch(regs);
		return;
	default:
		break;
	}
}

/*
 * We need to be able to restore the syscall arguments (r21-r26) to
 * restart syscalls.  Thus, the syscall path should save them in the
 * pt_regs structure (it's okay to do so since they are caller-save
 * registers).  As noted below, the syscall number gets restored for
 * us due to the magic of delayed branching.
 */
static void do_signal(struct pt_regs *regs, long in_syscall)
{
	struct ksignal ksig;
	int restart_syscall;
	bool has_handler;

	has_handler = get_signal(&ksig);

	restart_syscall = 0;
	if (in_syscall)
		restart_syscall = 1;

	if (has_handler) {
		/* Restart a system call if necessary. */
		if (restart_syscall)
			syscall_restart(regs, &ksig.ka);

		handle_signal(&ksig, regs, in_syscall);
		DBG(1, "%s: Handled signal pid %d\n",
			__func__, task_pid_nr(current));
		return;
	}

	/* Do we need to restart the system call? */
	if (restart_syscall)
		insert_restart_trampoline(regs);
	
	DBG(1, "%s: Exit (not delivered), regs->gr[28] = %ld  orig_r28 = %ld  pid %d\n",
		__func__, regs->gr[28], regs->orig_r28, task_pid_nr(current));

	restore_saved_sigmask();
}

asmlinkage void do_notify_resume(struct pt_regs *regs, long in_syscall)
{
	if (test_thread_flag(TIF_SIGPENDING) ||
	    test_thread_flag(TIF_NOTIFY_SIGNAL))
		do_signal(regs, in_syscall);

	if (test_thread_flag(TIF_NOTIFY_RESUME))
		resume_user_mode_work(regs);
}
