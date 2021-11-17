// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/parisc/kernel/signal.c: Architecture-specific signal
 *  handling support.
 *
 *  Copyright (C) 2000 David Huggins-Daines <dhd@debian.org>
 *  Copyright (C) 2000 Linuxcare, Inc.
 *
 *  Based on the ia64, i386, and alpha versions.
 *
 *  Like the IA-64, we are a recent enough port (we are *starting*
 *  with glibc2.2) that we do not need to support the old non-realtime
 *  Linux signals.  Therefore we don't.
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
#include <linux/tracehook.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/compat.h>
#include <linux/elf.h>
#include <asm/ucontext.h>
#include <asm/rt_sigframe.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/asm-offsets.h>

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

/* Trampoline for calling rt_sigreturn() */
#define INSN_LDI_R25_0	 0x34190000 /* ldi  0,%r25 (in_syscall=0) */
#define INSN_LDI_R25_1	 0x34190002 /* ldi  1,%r25 (in_syscall=1) */
#define INSN_LDI_R20	 0x3414015a /* ldi  __NR_rt_sigreturn,%r20 */
#define INSN_BLE_SR2_R0  0xe4008200 /* be,l 0x100(%sr2,%r0),%sr0,%r31 */
/* For debugging */
#define INSN_DIE_HORRIBLY 0x68000ccc /* stw %r0,0x666(%sr0,%r0) */

static long
restore_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs)
{
	long err = 0;

	err |= __copy_from_user(regs->gr, sc->sc_gr, sizeof(regs->gr));
	err |= __copy_from_user(regs->fr, sc->sc_fr, sizeof(regs->fr));
	err |= __copy_from_user(regs->iaoq, sc->sc_iaoq, sizeof(regs->iaoq));
	err |= __copy_from_user(regs->iasq, sc->sc_iasq, sizeof(regs->iasq));
	err |= __get_user(regs->sar, &sc->sc_sar);
	DBG(2,"restore_sigcontext: iaoq is %#lx / %#lx\n",
			regs->iaoq[0],regs->iaoq[1]);
	DBG(2,"restore_sigcontext: r28 is %ld\n", regs->gr[28]);
	return err;
}

void
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
	DBG(2,"sys_rt_sigreturn: frame is %p\n", frame);

	regs->orig_r28 = 1; /* no restarts for sigreturn */

#ifdef CONFIG_64BIT
	compat_frame = (struct compat_rt_sigframe __user *)frame;
	
	if (is_compat_task()) {
		DBG(2,"sys_rt_sigreturn: ELF32 process.\n");
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
		DBG(1,"sys_rt_sigreturn: compat_frame->uc.uc_mcontext 0x%p\n",
				&compat_frame->uc.uc_mcontext);
// FIXME: Load upper half from register file
		if (restore_sigcontext32(&compat_frame->uc.uc_mcontext, 
					&compat_frame->regs, regs))
			goto give_sigsegv;
		DBG(1,"sys_rt_sigreturn: usp %#08lx stack 0x%p\n", 
				usp, &compat_frame->uc.uc_stack);
		if (compat_restore_altstack(&compat_frame->uc.uc_stack))
			goto give_sigsegv;
	} else
#endif
	{
		DBG(1,"sys_rt_sigreturn: frame->uc.uc_mcontext 0x%p\n",
				&frame->uc.uc_mcontext);
		if (restore_sigcontext(&frame->uc.uc_mcontext, regs))
			goto give_sigsegv;
		DBG(1,"sys_rt_sigreturn: usp %#08lx stack 0x%p\n", 
				usp, &frame->uc.uc_stack);
		if (restore_altstack(&frame->uc.uc_stack))
			goto give_sigsegv;
	}
		


	/* If we are on the syscall path IAOQ will not be restored, and
	 * if we are on the interrupt path we must not corrupt gr31.
	 */
	if (in_syscall)
		regs->gr[31] = regs->iaoq[0];
#if DEBUG_SIG
	DBG(1,"sys_rt_sigreturn: returning to %#lx, DUMPING REGS:\n", regs->iaoq[0]);
	show_regs(regs);
#endif
	return;

give_sigsegv:
	DBG(1,"sys_rt_sigreturn: Sending SIGSEGV\n");
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

	DBG(1,"get_sigframe: ka = %#lx, sp = %#lx, frame_size = %#lx\n",
			(unsigned long)ka, sp, frame_size);
	
	/* Align alternate stack and reserve 64 bytes for the signal
	   handler's frame marker.  */
	if ((ka->sa.sa_flags & SA_ONSTACK) != 0 && ! sas_ss_flags(sp))
		sp = (current->sas_ss_sp + 0x7f) & ~0x3f; /* Stacks grow up! */

	DBG(1,"get_sigframe: Returning sp = %#lx\n", (unsigned long)sp);
	return (void __user *) sp; /* Stacks grow up.  Fun. */
}

static long
setup_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs, int in_syscall)
		 
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
		DBG(1,"setup_sigcontext: iaoq %#lx / %#lx (in syscall)\n",
			regs->gr[31], regs->gr[31]+4);
	} else {
		err |= __copy_to_user(sc->sc_iaoq, regs->iaoq, sizeof(regs->iaoq));
		err |= __copy_to_user(sc->sc_iasq, regs->iasq, sizeof(regs->iasq));
		DBG(1,"setup_sigcontext: iaoq %#lx / %#lx (not in syscall)\n", 
			regs->iaoq[0], regs->iaoq[1]);
	}

	err |= __put_user(flags, &sc->sc_flags);
	err |= __copy_to_user(sc->sc_gr, regs->gr, sizeof(regs->gr));
	err |= __copy_to_user(sc->sc_fr, regs->fr, sizeof(regs->fr));
	err |= __put_user(regs->sar, &sc->sc_sar);
	DBG(1,"setup_sigcontext: r28 is %ld\n", regs->gr[28]);

	return err;
}

static long
setup_rt_frame(struct ksignal *ksig, sigset_t *set, struct pt_regs *regs,
	       int in_syscall)
{
	struct rt_sigframe __user *frame;
	unsigned long rp, usp;
	unsigned long haddr, sigframe_size;
	unsigned long start, end;
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

	DBG(1,"SETUP_RT_FRAME: START\n");
	DBG(1,"setup_rt_frame: frame %p info %p\n", frame, ksig->info);

	start = (unsigned long) frame;
	if (start >= user_addr_max() - sigframe_size)
		return -EFAULT;
	
#ifdef CONFIG_64BIT

	compat_frame = (struct compat_rt_sigframe __user *)frame;
	
	if (is_compat_task()) {
		DBG(1,"setup_rt_frame: frame->info = 0x%p\n", &compat_frame->info);
		err |= copy_siginfo_to_user32(&compat_frame->info, &ksig->info);
		err |= __compat_save_altstack( &compat_frame->uc.uc_stack, regs->gr[30]);
		DBG(1,"setup_rt_frame: frame->uc = 0x%p\n", &compat_frame->uc);
		DBG(1,"setup_rt_frame: frame->uc.uc_mcontext = 0x%p\n", &compat_frame->uc.uc_mcontext);
		err |= setup_sigcontext32(&compat_frame->uc.uc_mcontext, 
					&compat_frame->regs, regs, in_syscall);
		err |= put_compat_sigset(&compat_frame->uc.uc_sigmask, set,
					 sizeof(compat_sigset_t));
	} else
#endif
	{	
		DBG(1,"setup_rt_frame: frame->info = 0x%p\n", &frame->info);
		err |= copy_siginfo_to_user(&frame->info, &ksig->info);
		err |= __save_altstack(&frame->uc.uc_stack, regs->gr[30]);
		DBG(1,"setup_rt_frame: frame->uc = 0x%p\n", &frame->uc);
		DBG(1,"setup_rt_frame: frame->uc.uc_mcontext = 0x%p\n", &frame->uc.uc_mcontext);
		err |= setup_sigcontext(&frame->uc.uc_mcontext, regs, in_syscall);
		/* FIXME: Should probably be converted as well for the compat case */
		err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	}
	
	if (err)
		return -EFAULT;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace. The first words of tramp are used to
	   save the previous sigrestartblock trampoline that might be
	   on the stack. We start the sigreturn trampoline at 
	   SIGRESTARTBLOCK_TRAMP. */
	err |= __put_user(in_syscall ? INSN_LDI_R25_1 : INSN_LDI_R25_0,
			&frame->tramp[SIGRESTARTBLOCK_TRAMP+0]);
	err |= __put_user(INSN_BLE_SR2_R0, 
			&frame->tramp[SIGRESTARTBLOCK_TRAMP+1]);
	err |= __put_user(INSN_LDI_R20,
			&frame->tramp[SIGRESTARTBLOCK_TRAMP+2]);

	start = (unsigned long) &frame->tramp[SIGRESTARTBLOCK_TRAMP+0];
	end = (unsigned long) &frame->tramp[SIGRESTARTBLOCK_TRAMP+3];
	flush_user_dcache_range_asm(start, end);
	flush_user_icache_range_asm(start, end);

	/* TRAMP Words 0-4, Length 5 = SIGRESTARTBLOCK_TRAMP
	 * TRAMP Words 5-7, Length 3 = SIGRETURN_TRAMP
	 * So the SIGRETURN_TRAMP is at the end of SIGRESTARTBLOCK_TRAMP
	 */
	rp = (unsigned long) &frame->tramp[SIGRESTARTBLOCK_TRAMP];

	if (err)
		return -EFAULT;

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
		DBG(1,"setup_rt_frame: 64 bit signal, exe=%#lx, r19=%#lx, in_syscall=%d\n",
		     haddr, regs->gr[19], in_syscall);
	}
#endif

	/* The syscall return path will create IAOQ values from r31.
	 */
	if (in_syscall) {
		regs->gr[31] = haddr;
#ifdef CONFIG_64BIT
		if (!test_thread_flag(TIF_32BIT))
			sigframe_size |= 1;
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
		regs->iaoq[0] = haddr | 3;
		regs->iaoq[1] = regs->iaoq[0] + 4;
	}

	regs->gr[2]  = rp;                /* userland return pointer */
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
	
	DBG(1,"setup_rt_frame: making sigreturn frame: %#lx + %#lx = %#lx\n",
	       regs->gr[30], sigframe_size,
	       regs->gr[30] + sigframe_size);
	/* Raise the user stack pointer to make a proper call frame. */
	regs->gr[30] = (A(frame) + sigframe_size);


	DBG(1,"setup_rt_frame: sig deliver (%s,%d) frame=0x%p sp=%#lx iaoq=%#lx/%#lx rp=%#lx\n",
	       current->comm, current->pid, frame, regs->gr[30],
	       regs->iaoq[0], regs->iaoq[1], rp);

	return 0;
}

/*
 * OK, we're invoking a handler.
 */	

static void
handle_signal(struct ksignal *ksig, struct pt_regs *regs, int in_syscall)
{
	int ret;
	sigset_t *oldset = sigmask_to_save();

	DBG(1,"handle_signal: sig=%ld, ka=%p, info=%p, oldset=%p, regs=%p\n",
	       ksig->sig, ksig->ka, ksig->info, oldset, regs);
	
	/* Set up the stack frame */
	ret = setup_rt_frame(ksig, oldset, regs, in_syscall);

	signal_setup_done(ret, ksig, test_thread_flag(TIF_SINGLESTEP) ||
			  test_thread_flag(TIF_BLOCKSTEP));

	DBG(1,KERN_DEBUG "do_signal: Exit (success), regs->gr[28] = %ld\n",
		regs->gr[28]);
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
	/* Check the return code */
	switch (regs->gr[28]) {
	case -ERESTART_RESTARTBLOCK:
	case -ERESTARTNOHAND:
		DBG(1,"ERESTARTNOHAND: returning -EINTR\n");
		regs->gr[28] = -EINTR;
		break;
	case -ERESTARTSYS:
		if (!(ka->sa.sa_flags & SA_RESTART)) {
			DBG(1,"ERESTARTSYS: putting -EINTR\n");
			regs->gr[28] = -EINTR;
			break;
		}
		fallthrough;
	case -ERESTARTNOINTR:
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
	switch(regs->gr[28]) {
	case -ERESTART_RESTARTBLOCK: {
		/* Restart the system call - no handlers present */
		unsigned int *usp = (unsigned int *)regs->gr[30];
		unsigned long start = (unsigned long) &usp[2];
		unsigned long end  = (unsigned long) &usp[5];
		long err = 0;

		/* check that we don't exceed the stack */
		if (A(&usp[0]) >= user_addr_max() - 5 * sizeof(int))
			return;

		/* Setup a trampoline to restart the syscall
		 * with __NR_restart_syscall
		 *
		 *  0: <return address (orig r31)>
		 *  4: <2nd half for 64-bit>
		 *  8: ldw 0(%sp), %r31
		 * 12: be 0x100(%sr2, %r0)
		 * 16: ldi __NR_restart_syscall, %r20
		 */
#ifdef CONFIG_64BIT
		err |= put_user(regs->gr[31] >> 32, &usp[0]);
		err |= put_user(regs->gr[31] & 0xffffffff, &usp[1]);
		err |= put_user(0x0fc010df, &usp[2]);
#else
		err |= put_user(regs->gr[31], &usp[0]);
		err |= put_user(0x0fc0109f, &usp[2]);
#endif
		err |= put_user(0xe0008200, &usp[3]);
		err |= put_user(0x34140000, &usp[4]);

		WARN_ON(err);

		/* flush data/instruction cache for new insns */
		flush_user_dcache_range_asm(start, end);
		flush_user_icache_range_asm(start, end);

		regs->gr[31] = regs->gr[30] + 8;
		return;
	}
	case -ERESTARTNOHAND:
	case -ERESTARTSYS:
	case -ERESTARTNOINTR:
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
asmlinkage void
do_signal(struct pt_regs *regs, long in_syscall)
{
	struct ksignal ksig;

	DBG(1,"\ndo_signal: regs=0x%p, sr7 %#lx, in_syscall=%d\n",
	       regs, regs->sr[7], in_syscall);

	if (get_signal(&ksig)) {
		DBG(3,"do_signal: signr = %d, regs->gr[28] = %ld\n", signr, regs->gr[28]);
		/* Restart a system call if necessary. */
		if (in_syscall)
			syscall_restart(regs, &ksig.ka);

		handle_signal(&ksig, regs, in_syscall);
		return;
	}

	/* Did we come from a system call? */
	if (in_syscall)
		insert_restart_trampoline(regs);
	
	DBG(1,"do_signal: Exit (not delivered), regs->gr[28] = %ld\n", 
		regs->gr[28]);

	restore_saved_sigmask();
}

void do_notify_resume(struct pt_regs *regs, long in_syscall)
{
	if (test_thread_flag(TIF_SIGPENDING) ||
	    test_thread_flag(TIF_NOTIFY_SIGNAL))
		do_signal(regs, in_syscall);

	if (test_thread_flag(TIF_NOTIFY_RESUME))
		tracehook_notify_resume(regs);
}
