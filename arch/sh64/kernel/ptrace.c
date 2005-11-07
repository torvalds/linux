/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * arch/sh64/kernel/ptrace.c
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 *
 * Started from SH3/4 version:
 *   SuperH version:   Copyright (C) 1999, 2000  Kaz Kojima & Niibe Yutaka
 *
 *   Original x86 implementation:
 *	By Ross Biro 1/23/92
 *	edited by Linus Torvalds
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/signal.h>
#include <linux/syscalls.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>

/* This mask defines the bits of the SR which the user is not allowed to
   change, which are everything except S, Q, M, PR, SZ, FR. */
#define SR_MASK      (0xffff8cfd)

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * This routine will get a word from the user area in the process kernel stack.
 */
static inline int get_stack_long(struct task_struct *task, int offset)
{
	unsigned char *stack;

	stack = (unsigned char *)(task->thread.uregs);
	stack += offset;
	return (*((int *)stack));
}

static inline unsigned long
get_fpu_long(struct task_struct *task, unsigned long addr)
{
	unsigned long tmp;
	struct pt_regs *regs;
	regs = (struct pt_regs*)((unsigned char *)task + THREAD_SIZE) - 1;

	if (!tsk_used_math(task)) {
		if (addr == offsetof(struct user_fpu_struct, fpscr)) {
			tmp = FPSCR_INIT;
		} else {
			tmp = 0xffffffffUL; /* matches initial value in fpu.c */
		}
		return tmp;
	}

	if (last_task_used_math == task) {
		grab_fpu();
		fpsave(&task->thread.fpu.hard);
		release_fpu();
		last_task_used_math = 0;
		regs->sr |= SR_FD;
	}

	tmp = ((long *)&task->thread.fpu)[addr / sizeof(unsigned long)];
	return tmp;
}

/*
 * This routine will put a word into the user area in the process kernel stack.
 */
static inline int put_stack_long(struct task_struct *task, int offset,
				 unsigned long data)
{
	unsigned char *stack;

	stack = (unsigned char *)(task->thread.uregs);
	stack += offset;
	*(unsigned long *) stack = data;
	return 0;
}

static inline int
put_fpu_long(struct task_struct *task, unsigned long addr, unsigned long data)
{
	struct pt_regs *regs;

	regs = (struct pt_regs*)((unsigned char *)task + THREAD_SIZE) - 1;

	if (!tsk_used_math(task)) {
		fpinit(&task->thread.fpu.hard);
		set_stopped_child_used_math(task);
	} else if (last_task_used_math == task) {
		grab_fpu();
		fpsave(&task->thread.fpu.hard);
		release_fpu();
		last_task_used_math = 0;
		regs->sr |= SR_FD;
	}

	((long *)&task->thread.fpu)[addr / sizeof(unsigned long)] = data;
	return 0;
}


long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int ret;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA: {
		unsigned long tmp;
		int copied;

		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		ret = -EIO;
		if (copied != sizeof(tmp))
			break;
		ret = put_user(tmp,(unsigned long *) data);
		break;
	}

	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		unsigned long tmp;

		ret = -EIO;
		if ((addr & 3) || addr < 0)
			break;

		if (addr < sizeof(struct pt_regs))
			tmp = get_stack_long(child, addr);
		else if ((addr >= offsetof(struct user, fpu)) &&
			 (addr <  offsetof(struct user, u_fpvalid))) {
			tmp = get_fpu_long(child, addr - offsetof(struct user, fpu));
		} else if (addr == offsetof(struct user, u_fpvalid)) {
			tmp = !!tsk_used_math(child);
		} else {
			break;
		}
		ret = put_user(tmp, (unsigned long *)data);
		break;
	}

	/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = 0;
		if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
			break;
		ret = -EIO;
		break;

	case PTRACE_POKEUSR:
                /* write the word at location addr in the USER area. We must
                   disallow any changes to certain SR bits or u_fpvalid, since
                   this could crash the kernel or result in a security
                   loophole. */
		ret = -EIO;
		if ((addr & 3) || addr < 0)
			break;

		if (addr < sizeof(struct pt_regs)) {
			/* Ignore change of top 32 bits of SR */
			if (addr == offsetof (struct pt_regs, sr)+4)
			{
				ret = 0;
				break;
			}
			/* If lower 32 bits of SR, ignore non-user bits */
			if (addr == offsetof (struct pt_regs, sr))
			{
				long cursr = get_stack_long(child, addr);
				data &= ~(SR_MASK);
				data |= (cursr & SR_MASK);
			}
			ret = put_stack_long(child, addr, data);
		}
		else if ((addr >= offsetof(struct user, fpu)) &&
			 (addr <  offsetof(struct user, u_fpvalid))) {
			ret = put_fpu_long(child, addr - offsetof(struct user, fpu), data);
		}
		break;

	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT: { /* restart after signal. */
		ret = -EIO;
		if (!valid_signal(data))
			break;
		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->exit_code = data;
		wake_up_process(child);
		ret = 0;
		break;
	}

/*
 * make the child exit.  Best I can do is send it a sigkill.
 * perhaps it should be put in the status that it wants to
 * exit.
 */
	case PTRACE_KILL: {
		ret = 0;
		if (child->exit_state == EXIT_ZOMBIE)	/* already dead */
			break;
		child->exit_code = SIGKILL;
		wake_up_process(child);
		break;
	}

	case PTRACE_SINGLESTEP: {  /* set the trap flag. */
		struct pt_regs *regs;

		ret = -EIO;
		if (!valid_signal(data))
			break;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		if ((child->ptrace & PT_DTRACE) == 0) {
			/* Spurious delayed TF traps may occur */
			child->ptrace |= PT_DTRACE;
		}

		regs = child->thread.uregs;

		regs->sr |= SR_SSTEP;	/* auto-resetting upon exception */

		child->exit_code = data;
		/* give it a chance to run. */
		wake_up_process(child);
		ret = 0;
		break;
	}

	case PTRACE_DETACH: /* detach a process that was attached. */
		ret = ptrace_detach(child, data);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}
	return ret;
}

asmlinkage int sh64_ptrace(long request, long pid, long addr, long data)
{
	extern void poke_real_address_q(unsigned long long addr, unsigned long long data);
#define WPC_DBRMODE 0x0d104008
	static int first_call = 1;

	lock_kernel();
	if (first_call) {
		/* Set WPC.DBRMODE to 0.  This makes all debug events get
		 * delivered through RESVEC, i.e. into the handlers in entry.S.
		 * (If the kernel was downloaded using a remote gdb, WPC.DBRMODE
		 * would normally be left set to 1, which makes debug events get
		 * delivered through DBRVEC, i.e. into the remote gdb's
		 * handlers.  This prevents ptrace getting them, and confuses
		 * the remote gdb.) */
		printk("DBRMODE set to 0 to permit native debugging\n");
		poke_real_address_q(WPC_DBRMODE, 0);
		first_call = 0;
	}
	unlock_kernel();

	return sys_ptrace(request, pid, addr, data);
}

asmlinkage void syscall_trace(void)
{
	struct task_struct *tsk = current;

	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(tsk->ptrace & PT_PTRACED))
		return;

	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				 ? 0x80 : 0));
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (tsk->exit_code) {
		send_sig(tsk->exit_code, tsk, 1);
		tsk->exit_code = 0;
	}
}

/* Called with interrupts disabled */
asmlinkage void do_single_step(unsigned long long vec, struct pt_regs *regs)
{
	/* This is called after a single step exception (DEBUGSS).
	   There is no need to change the PC, as it is a post-execution
	   exception, as entry.S does not do anything to the PC for DEBUGSS.
	   We need to clear the Single Step setting in SR to avoid
	   continually stepping. */
	local_irq_enable();
	regs->sr &= ~SR_SSTEP;
	force_sig(SIGTRAP, current);
}

/* Called with interrupts disabled */
asmlinkage void do_software_break_point(unsigned long long vec,
					struct pt_regs *regs)
{
	/* We need to forward step the PC, to counteract the backstep done
	   in signal.c. */
	local_irq_enable();
	force_sig(SIGTRAP, current);
	regs->pc += 4;
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
        /* nothing to do.. */
}
