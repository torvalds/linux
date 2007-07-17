/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#undef DEBUG
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/user.h>
#include <linux/security.h>
#include <linux/unistd.h>
#include <linux/notifier.h>

#include <asm/traps.h>
#include <asm/uaccess.h>
#include <asm/ocd.h>
#include <asm/mmu_context.h>
#include <linux/kdebug.h>

static struct pt_regs *get_user_regs(struct task_struct *tsk)
{
	return (struct pt_regs *)((unsigned long)task_stack_page(tsk) +
				  THREAD_SIZE - sizeof(struct pt_regs));
}

static void ptrace_single_step(struct task_struct *tsk)
{
	pr_debug("ptrace_single_step: pid=%u, SR=0x%08lx\n",
		 tsk->pid, tsk->thread.cpu_context.sr);
	if (!(tsk->thread.cpu_context.sr & SR_D)) {
		/*
		 * Set a breakpoint at the current pc to force the
		 * process into debug mode.  The syscall/exception
		 * exit code will set a breakpoint at the return
		 * address when this flag is set.
		 */
		pr_debug("ptrace_single_step: Setting TIF_BREAKPOINT\n");
		set_tsk_thread_flag(tsk, TIF_BREAKPOINT);
	}

	/* The monitor code will do the actual step for us */
	set_tsk_thread_flag(tsk, TIF_SINGLE_STEP);
}

/*
 * Called by kernel/ptrace.c when detaching
 *
 * Make sure any single step bits, etc. are not set
 */
void ptrace_disable(struct task_struct *child)
{
	clear_tsk_thread_flag(child, TIF_SINGLE_STEP);
}

/*
 * Handle hitting a breakpoint
 */
static void ptrace_break(struct task_struct *tsk, struct pt_regs *regs)
{
	siginfo_t info;

	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code  = TRAP_BRKPT;
	info.si_addr  = (void __user *)instruction_pointer(regs);

	pr_debug("ptrace_break: Sending SIGTRAP to PID %u (pc = 0x%p)\n",
		 tsk->pid, info.si_addr);
	force_sig_info(SIGTRAP, &info, tsk);
}

/*
 * Read the word at offset "offset" into the task's "struct user". We
 * actually access the pt_regs struct stored on the kernel stack.
 */
static int ptrace_read_user(struct task_struct *tsk, unsigned long offset,
			    unsigned long __user *data)
{
	unsigned long *regs;
	unsigned long value;

	pr_debug("ptrace_read_user(%p, %#lx, %p)\n",
		 tsk, offset, data);

	if (offset & 3 || offset >= sizeof(struct user)) {
		printk("ptrace_read_user: invalid offset 0x%08lx\n", offset);
		return -EIO;
	}

	regs = (unsigned long *)get_user_regs(tsk);

	value = 0;
	if (offset < sizeof(struct pt_regs))
		value = regs[offset / sizeof(regs[0])];

	return put_user(value, data);
}

/*
 * Write the word "value" to offset "offset" into the task's "struct
 * user". We actually access the pt_regs struct stored on the kernel
 * stack.
 */
static int ptrace_write_user(struct task_struct *tsk, unsigned long offset,
			     unsigned long value)
{
	unsigned long *regs;

	if (offset & 3 || offset >= sizeof(struct user)) {
		printk("ptrace_write_user: invalid offset 0x%08lx\n", offset);
		return -EIO;
	}

	if (offset >= sizeof(struct pt_regs))
		return 0;

	regs = (unsigned long *)get_user_regs(tsk);
	regs[offset / sizeof(regs[0])] = value;

	return 0;
}

static int ptrace_getregs(struct task_struct *tsk, void __user *uregs)
{
	struct pt_regs *regs = get_user_regs(tsk);

	return copy_to_user(uregs, regs, sizeof(*regs)) ? -EFAULT : 0;
}

static int ptrace_setregs(struct task_struct *tsk, const void __user *uregs)
{
	struct pt_regs newregs;
	int ret;

	ret = -EFAULT;
	if (copy_from_user(&newregs, uregs, sizeof(newregs)) == 0) {
		struct pt_regs *regs = get_user_regs(tsk);

		ret = -EINVAL;
		if (valid_user_regs(&newregs)) {
			*regs = newregs;
			ret = 0;
		}
	}

	return ret;
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int ret;

	pr_debug("arch_ptrace(%ld, %d, %#lx, %#lx)\n",
		 request, child->pid, addr, data);

	pr_debug("ptrace: Enabling monitor mode...\n");
	__mtdr(DBGREG_DC, __mfdr(DBGREG_DC) | DC_MM | DC_DBE);

	switch (request) {
	/* Read the word at location addr in the child process */
	case PTRACE_PEEKTEXT:
	case PTRACE_PEEKDATA:
		ret = generic_ptrace_peekdata(child, addr, data);
		break;

	case PTRACE_PEEKUSR:
		ret = ptrace_read_user(child, addr,
				       (unsigned long __user *)data);
		break;

	/* Write the word in data at location addr */
	case PTRACE_POKETEXT:
	case PTRACE_POKEDATA:
		ret = access_process_vm(child, addr, &data, sizeof(data), 1);
		if (ret == sizeof(data))
			ret = 0;
		else
			ret = -EIO;
		break;

	case PTRACE_POKEUSR:
		ret = ptrace_write_user(child, addr, data);
		break;

	/* continue and stop at next (return from) syscall */
	case PTRACE_SYSCALL:
	/* restart after signal */
	case PTRACE_CONT:
		ret = -EIO;
		if (!valid_signal(data))
			break;
		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->exit_code = data;
		/* XXX: Are we sure no breakpoints are active here? */
		wake_up_process(child);
		ret = 0;
		break;

	/*
	 * Make the child exit. Best I can do is send it a
	 * SIGKILL. Perhaps it should be put in the status that it
	 * wants to exit.
	 */
	case PTRACE_KILL:
		ret = 0;
		if (child->exit_state == EXIT_ZOMBIE)
			break;
		child->exit_code = SIGKILL;
		wake_up_process(child);
		break;

	/*
	 * execute single instruction.
	 */
	case PTRACE_SINGLESTEP:
		ret = -EIO;
		if (!valid_signal(data))
			break;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		ptrace_single_step(child);
		child->exit_code = data;
		wake_up_process(child);
		ret = 0;
		break;

	/* Detach a process that was attached */
	case PTRACE_DETACH:
		ret = ptrace_detach(child, data);
		break;

	case PTRACE_GETREGS:
		ret = ptrace_getregs(child, (void __user *)data);
		break;

	case PTRACE_SETREGS:
		ret = ptrace_setregs(child, (const void __user *)data);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	pr_debug("sys_ptrace returning %d (DC = 0x%08lx)\n", ret, __mfdr(DBGREG_DC));
	return ret;
}

asmlinkage void syscall_trace(void)
{
	pr_debug("syscall_trace called\n");
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;

	pr_debug("syscall_trace: notifying parent\n");
	/* The 0x80 provides a way for the tracing parent to
	 * distinguish between a syscall stop and SIGTRAP delivery */
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				 ? 0x80 : 0));

	/*
	 * this isn't the same as continuing with a signal, but it
	 * will do for normal use.  strace only continues with a
	 * signal if the stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		pr_debug("syscall_trace: sending signal %d to PID %u\n",
			 current->exit_code, current->pid);
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}

asmlinkage void do_debug_priv(struct pt_regs *regs)
{
	unsigned long dc, ds;
	unsigned long die_val;

	ds = __mfdr(DBGREG_DS);

	pr_debug("do_debug_priv: pc = %08lx, ds = %08lx\n", regs->pc, ds);

	if (ds & DS_SSS)
		die_val = DIE_SSTEP;
	else
		die_val = DIE_BREAKPOINT;

	if (notify_die(die_val, "ptrace", regs, 0, 0, SIGTRAP) == NOTIFY_STOP)
		return;

	if (likely(ds & DS_SSS)) {
		extern void itlb_miss(void);
		extern void tlb_miss_common(void);
		struct thread_info *ti;

		dc = __mfdr(DBGREG_DC);
		dc &= ~DC_SS;
		__mtdr(DBGREG_DC, dc);

		ti = current_thread_info();
		set_ti_thread_flag(ti, TIF_BREAKPOINT);

		/* The TLB miss handlers don't check thread flags */
		if ((regs->pc >= (unsigned long)&itlb_miss)
		    && (regs->pc <= (unsigned long)&tlb_miss_common)) {
			__mtdr(DBGREG_BWA2A, sysreg_read(RAR_EX));
			__mtdr(DBGREG_BWC2A, 0x40000001 | (get_asid() << 1));
		}

		/*
		 * If we're running in supervisor mode, the breakpoint
		 * will take us where we want directly, no need to
		 * single step.
		 */
		if ((regs->sr & MODE_MASK) != MODE_SUPERVISOR)
			set_ti_thread_flag(ti, TIF_SINGLE_STEP);
	} else {
		panic("Unable to handle debug trap at pc = %08lx\n",
		      regs->pc);
	}
}

/*
 * Handle breakpoints, single steps and other debuggy things. To keep
 * things simple initially, we run with interrupts and exceptions
 * disabled all the time.
 */
asmlinkage void do_debug(struct pt_regs *regs)
{
	unsigned long dc, ds;

	ds = __mfdr(DBGREG_DS);
	pr_debug("do_debug: pc = %08lx, ds = %08lx\n", regs->pc, ds);

	if (test_thread_flag(TIF_BREAKPOINT)) {
		pr_debug("TIF_BREAKPOINT set\n");
		/* We're taking care of it */
		clear_thread_flag(TIF_BREAKPOINT);
		__mtdr(DBGREG_BWC2A, 0);
	}

	if (test_thread_flag(TIF_SINGLE_STEP)) {
		pr_debug("TIF_SINGLE_STEP set, ds = 0x%08lx\n", ds);
		if (ds & DS_SSS) {
			dc = __mfdr(DBGREG_DC);
			dc &= ~DC_SS;
			__mtdr(DBGREG_DC, dc);

			clear_thread_flag(TIF_SINGLE_STEP);
			ptrace_break(current, regs);
		}
	} else {
		/* regular breakpoint */
		ptrace_break(current, regs);
	}
}
