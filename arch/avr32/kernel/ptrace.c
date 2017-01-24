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
#include <linux/uaccess.h>
#include <asm/ocd.h>
#include <asm/mmu_context.h>
#include <linux/kdebug.h>

static struct pt_regs *get_user_regs(struct task_struct *tsk)
{
	return (struct pt_regs *)((unsigned long)task_stack_page(tsk) +
				  THREAD_SIZE - sizeof(struct pt_regs));
}

void user_enable_single_step(struct task_struct *tsk)
{
	pr_debug("user_enable_single_step: pid=%u, PC=0x%08lx, SR=0x%08lx\n",
		 tsk->pid, task_pt_regs(tsk)->pc, task_pt_regs(tsk)->sr);

	/*
	 * We can't schedule in Debug mode, so when TIF_BREAKPOINT is
	 * set, the system call or exception handler will do a
	 * breakpoint to enter monitor mode before returning to
	 * userspace.
	 *
	 * The monitor code will then notice that TIF_SINGLE_STEP is
	 * set and return to userspace with single stepping enabled.
	 * The CPU will then enter monitor mode again after exactly
	 * one instruction has been executed, and the monitor code
	 * will then send a SIGTRAP to the process.
	 */
	set_tsk_thread_flag(tsk, TIF_BREAKPOINT);
	set_tsk_thread_flag(tsk, TIF_SINGLE_STEP);
}

void user_disable_single_step(struct task_struct *child)
{
	/* XXX(hch): a no-op here seems wrong.. */
}

/*
 * Called by kernel/ptrace.c when detaching
 *
 * Make sure any single step bits, etc. are not set
 */
void ptrace_disable(struct task_struct *child)
{
	clear_tsk_thread_flag(child, TIF_SINGLE_STEP);
	clear_tsk_thread_flag(child, TIF_BREAKPOINT);
	ocd_disable(child);
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

	if (offset & 3 || offset >= sizeof(struct user)) {
		printk("ptrace_read_user: invalid offset 0x%08lx\n", offset);
		return -EIO;
	}

	regs = (unsigned long *)get_user_regs(tsk);

	value = 0;
	if (offset < sizeof(struct pt_regs))
		value = regs[offset / sizeof(regs[0])];

	pr_debug("ptrace_read_user(%s[%u], %#lx, %p) -> %#lx\n",
		 tsk->comm, tsk->pid, offset, data, value);

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

	pr_debug("ptrace_write_user(%s[%u], %#lx, %#lx)\n",
			tsk->comm, tsk->pid, offset, value);

	if (offset & 3 || offset >= sizeof(struct user)) {
		pr_debug("  invalid offset 0x%08lx\n", offset);
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

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret;
	void __user *datap = (void __user *) data;

	switch (request) {
	/* Read the word at location addr in the child process */
	case PTRACE_PEEKTEXT:
	case PTRACE_PEEKDATA:
		ret = generic_ptrace_peekdata(child, addr, data);
		break;

	case PTRACE_PEEKUSR:
		ret = ptrace_read_user(child, addr, datap);
		break;

	/* Write the word in data at location addr */
	case PTRACE_POKETEXT:
	case PTRACE_POKEDATA:
		ret = generic_ptrace_pokedata(child, addr, data);
		break;

	case PTRACE_POKEUSR:
		ret = ptrace_write_user(child, addr, data);
		break;

	case PTRACE_GETREGS:
		ret = ptrace_getregs(child, datap);
		break;

	case PTRACE_SETREGS:
		ret = ptrace_setregs(child, datap);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

asmlinkage void syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;

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

/*
 * debug_trampoline() is an assembly stub which will store all user
 * registers on the stack and execute a breakpoint instruction.
 *
 * If we single-step into an exception handler which runs with
 * interrupts disabled the whole time so it doesn't have to check for
 * pending work, its return address will be modified so that it ends
 * up returning to debug_trampoline.
 *
 * If the exception handler decides to store the user context and
 * enable interrupts after all, it will restore the original return
 * address and status register value. Before it returns, it will
 * notice that TIF_BREAKPOINT is set and execute a breakpoint
 * instruction.
 */
extern void debug_trampoline(void);

asmlinkage struct pt_regs *do_debug(struct pt_regs *regs)
{
	struct thread_info	*ti;
	unsigned long		trampoline_addr;
	u32			status;
	u32			ctrl;
	int			code;

	status = ocd_read(DS);
	ti = current_thread_info();
	code = TRAP_BRKPT;

	pr_debug("do_debug: status=0x%08x PC=0x%08lx SR=0x%08lx tif=0x%08lx\n",
			status, regs->pc, regs->sr, ti->flags);

	if (!user_mode(regs)) {
		unsigned long	die_val = DIE_BREAKPOINT;

		if (status & (1 << OCD_DS_SSS_BIT))
			die_val = DIE_SSTEP;

		if (notify_die(die_val, "ptrace", regs, 0, 0, SIGTRAP)
				== NOTIFY_STOP)
			return regs;

		if ((status & (1 << OCD_DS_SWB_BIT))
				&& test_and_clear_ti_thread_flag(
					ti, TIF_BREAKPOINT)) {
			/*
			 * Explicit breakpoint from trampoline or
			 * exception/syscall/interrupt handler.
			 *
			 * The real saved regs are on the stack right
			 * after the ones we saved on entry.
			 */
			regs++;
			pr_debug("  -> TIF_BREAKPOINT done, adjusted regs:"
					"PC=0x%08lx SR=0x%08lx\n",
					regs->pc, regs->sr);
			BUG_ON(!user_mode(regs));

			if (test_thread_flag(TIF_SINGLE_STEP)) {
				pr_debug("Going to do single step...\n");
				return regs;
			}

			/*
			 * No TIF_SINGLE_STEP means we're done
			 * stepping over a syscall. Do the trap now.
			 */
			code = TRAP_TRACE;
		} else if ((status & (1 << OCD_DS_SSS_BIT))
				&& test_ti_thread_flag(ti, TIF_SINGLE_STEP)) {

			pr_debug("Stepped into something, "
					"setting TIF_BREAKPOINT...\n");
			set_ti_thread_flag(ti, TIF_BREAKPOINT);

			/*
			 * We stepped into an exception, interrupt or
			 * syscall handler. Some exception handlers
			 * don't check for pending work, so we need to
			 * set up a trampoline just in case.
			 *
			 * The exception entry code will undo the
			 * trampoline stuff if it does a full context
			 * save (which also means that it'll check for
			 * pending work later.)
			 */
			if ((regs->sr & MODE_MASK) == MODE_EXCEPTION) {
				trampoline_addr
					= (unsigned long)&debug_trampoline;

				pr_debug("Setting up trampoline...\n");
				ti->rar_saved = sysreg_read(RAR_EX);
				ti->rsr_saved = sysreg_read(RSR_EX);
				sysreg_write(RAR_EX, trampoline_addr);
				sysreg_write(RSR_EX, (MODE_EXCEPTION
							| SR_EM | SR_GM));
				BUG_ON(ti->rsr_saved & MODE_MASK);
			}

			/*
			 * If we stepped into a system call, we
			 * shouldn't do a single step after we return
			 * since the return address is right after the
			 * "scall" instruction we were told to step
			 * over.
			 */
			if ((regs->sr & MODE_MASK) == MODE_SUPERVISOR) {
				pr_debug("Supervisor; no single step\n");
				clear_ti_thread_flag(ti, TIF_SINGLE_STEP);
			}

			ctrl = ocd_read(DC);
			ctrl &= ~(1 << OCD_DC_SS_BIT);
			ocd_write(DC, ctrl);

			return regs;
		} else {
			printk(KERN_ERR "Unexpected OCD_DS value: 0x%08x\n",
					status);
			printk(KERN_ERR "Thread flags: 0x%08lx\n", ti->flags);
			die("Unhandled debug trap in kernel mode",
					regs, SIGTRAP);
		}
	} else if (status & (1 << OCD_DS_SSS_BIT)) {
		/* Single step in user mode */
		code = TRAP_TRACE;

		ctrl = ocd_read(DC);
		ctrl &= ~(1 << OCD_DC_SS_BIT);
		ocd_write(DC, ctrl);
	}

	pr_debug("Sending SIGTRAP: code=%d PC=0x%08lx SR=0x%08lx\n",
			code, regs->pc, regs->sr);

	clear_thread_flag(TIF_SINGLE_STEP);
	_exception(SIGTRAP, regs, code, instruction_pointer(regs));

	return regs;
}
