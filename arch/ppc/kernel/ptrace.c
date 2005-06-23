/*
 *  arch/ppc/kernel/ptrace.c
 *
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/m68k/kernel/ptrace.c"
 *  Copyright (C) 1994 by Hamish Macdonald
 *  Taken from linux/kernel/ptrace.c and modified for M680x0.
 *  linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 *
 * Modified by Cort Dougan (cort@hq.fsmlabs.com)
 * and Paul Mackerras (paulus@linuxcare.com.au).
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
 * this archive for more details.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/security.h>
#include <linux/signal.h>
#include <linux/seccomp.h>
#include <linux/audit.h>
#include <linux/module.h>

#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>

/*
 * Set of msr bits that gdb can change on behalf of a process.
 */
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
#define MSR_DEBUGCHANGE	0
#else
#define MSR_DEBUGCHANGE	(MSR_SE | MSR_BE)
#endif

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Get contents of register REGNO in task TASK.
 */
static inline unsigned long get_reg(struct task_struct *task, int regno)
{
	if (regno < sizeof(struct pt_regs) / sizeof(unsigned long)
	    && task->thread.regs != NULL)
		return ((unsigned long *)task->thread.regs)[regno];
	return (0);
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int put_reg(struct task_struct *task, int regno,
			  unsigned long data)
{
	if (regno <= PT_MQ && task->thread.regs != NULL) {
		if (regno == PT_MSR)
			data = (data & MSR_DEBUGCHANGE)
				| (task->thread.regs->msr & ~MSR_DEBUGCHANGE);
		((unsigned long *)task->thread.regs)[regno] = data;
		return 0;
	}
	return -EIO;
}

#ifdef CONFIG_ALTIVEC
/*
 * Get contents of AltiVec register state in task TASK
 */
static inline int get_vrregs(unsigned long __user *data, struct task_struct *task)
{
	int i, j;

	if (!access_ok(VERIFY_WRITE, data, 133 * sizeof(unsigned long)))
		return -EFAULT;

	/* copy AltiVec registers VR[0] .. VR[31] */
	for (i = 0; i < 32; i++)
		for (j = 0; j < 4; j++, data++)
			if (__put_user(task->thread.vr[i].u[j], data))
				return -EFAULT;

	/* copy VSCR */
	for (i = 0; i < 4; i++, data++)
		if (__put_user(task->thread.vscr.u[i], data))
			return -EFAULT;

        /* copy VRSAVE */
	if (__put_user(task->thread.vrsave, data))
		return -EFAULT;

	return 0;
}

/*
 * Write contents of AltiVec register state into task TASK.
 */
static inline int set_vrregs(struct task_struct *task, unsigned long __user *data)
{
	int i, j;

	if (!access_ok(VERIFY_READ, data, 133 * sizeof(unsigned long)))
		return -EFAULT;

	/* copy AltiVec registers VR[0] .. VR[31] */
	for (i = 0; i < 32; i++)
		for (j = 0; j < 4; j++, data++)
			if (__get_user(task->thread.vr[i].u[j], data))
				return -EFAULT;

	/* copy VSCR */
	for (i = 0; i < 4; i++, data++)
		if (__get_user(task->thread.vscr.u[i], data))
			return -EFAULT;

	/* copy VRSAVE */
	if (__get_user(task->thread.vrsave, data))
		return -EFAULT;

	return 0;
}
#endif

#ifdef CONFIG_SPE

/*
 * For get_evrregs/set_evrregs functions 'data' has the following layout:
 *
 * struct {
 *   u32 evr[32];
 *   u64 acc;
 *   u32 spefscr;
 * }
 */

/*
 * Get contents of SPE register state in task TASK.
 */
static inline int get_evrregs(unsigned long *data, struct task_struct *task)
{
	int i;

	if (!access_ok(VERIFY_WRITE, data, 35 * sizeof(unsigned long)))
		return -EFAULT;

	/* copy SPEFSCR */
	if (__put_user(task->thread.spefscr, &data[34]))
		return -EFAULT;

	/* copy SPE registers EVR[0] .. EVR[31] */
	for (i = 0; i < 32; i++, data++)
		if (__put_user(task->thread.evr[i], data))
			return -EFAULT;

	/* copy ACC */
	if (__put_user64(task->thread.acc, (unsigned long long *)data))
		return -EFAULT;

	return 0;
}

/*
 * Write contents of SPE register state into task TASK.
 */
static inline int set_evrregs(struct task_struct *task, unsigned long *data)
{
	int i;

	if (!access_ok(VERIFY_READ, data, 35 * sizeof(unsigned long)))
		return -EFAULT;

	/* copy SPEFSCR */
	if (__get_user(task->thread.spefscr, &data[34]))
		return -EFAULT;

	/* copy SPE registers EVR[0] .. EVR[31] */
	for (i = 0; i < 32; i++, data++)
		if (__get_user(task->thread.evr[i], data))
			return -EFAULT;
	/* copy ACC */
	if (__get_user64(task->thread.acc, (unsigned long long*)data))
		return -EFAULT;

	return 0;
}
#endif /* CONFIG_SPE */

static inline void
set_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;

	if (regs != NULL) {
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
		task->thread.dbcr0 = DBCR0_IDM | DBCR0_IC;
		regs->msr |= MSR_DE;
#else
		regs->msr |= MSR_SE;
#endif
	}
}

static inline void
clear_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;

	if (regs != NULL) {
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
		task->thread.dbcr0 = 0;
		regs->msr &= ~MSR_DE;
#else
		regs->msr &= ~MSR_SE;
#endif
	}
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* make sure the single step bit is not set. */
	clear_single_step(child);
}

int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int ret = -EPERM;

	lock_kernel();
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->ptrace & PT_PTRACED)
			goto out;
		ret = security_ptrace(current->parent, current);
		if (ret)
			goto out;
		/* set the ptrace bit in the process flags. */
		current->ptrace |= PT_PTRACED;
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	if (child)
		get_task_struct(child);
	read_unlock(&tasklist_lock);
	if (!child)
		goto out;

	ret = -EPERM;
	if (pid == 1)		/* you may not mess with init */
		goto out_tsk;

	if (request == PTRACE_ATTACH) {
		ret = ptrace_attach(child);
		goto out_tsk;
	}

	ret = ptrace_check_attach(child, request == PTRACE_KILL);
	if (ret < 0)
		goto out_tsk;

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
		ret = put_user(tmp,(unsigned long __user *) data);
		break;
	}

	/* read the word at location addr in the USER area. */
	/* XXX this will need fixing for 64-bit */
	case PTRACE_PEEKUSR: {
		unsigned long index, tmp;

		ret = -EIO;
		/* convert to index and check */
		index = (unsigned long) addr >> 2;
		if ((addr & 3) || index > PT_FPSCR
		    || child->thread.regs == NULL)
			break;

		CHECK_FULL_REGS(child->thread.regs);
		if (index < PT_FPR0) {
			tmp = get_reg(child, (int) index);
		} else {
			preempt_disable();
			if (child->thread.regs->msr & MSR_FP)
				giveup_fpu(child);
			preempt_enable();
			tmp = ((unsigned long *)child->thread.fpr)[index - PT_FPR0];
		}
		ret = put_user(tmp,(unsigned long __user *) data);
		break;
	}

	/* If I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = 0;
		if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
			break;
		ret = -EIO;
		break;

	/* write the word at location addr in the USER area */
	case PTRACE_POKEUSR: {
		unsigned long index;

		ret = -EIO;
		/* convert to index and check */
		index = (unsigned long) addr >> 2;
		if ((addr & 3) || index > PT_FPSCR
		    || child->thread.regs == NULL)
			break;

		CHECK_FULL_REGS(child->thread.regs);
		if (index == PT_ORIG_R3)
			break;
		if (index < PT_FPR0) {
			ret = put_reg(child, index, data);
		} else {
			preempt_disable();
			if (child->thread.regs->msr & MSR_FP)
				giveup_fpu(child);
			preempt_enable();
			((unsigned long *)child->thread.fpr)[index - PT_FPR0] = data;
			ret = 0;
		}
		break;
	}

	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT: { /* restart after signal. */
		ret = -EIO;
		if (!valid_signal(data))
			break;
		if (request == PTRACE_SYSCALL) {
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		} else {
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		}
		child->exit_code = data;
		/* make sure the single step bit is not set. */
		clear_single_step(child);
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
		/* make sure the single step bit is not set. */
		clear_single_step(child);
		wake_up_process(child);
		break;
	}

	case PTRACE_SINGLESTEP: {  /* set the trap flag. */
		ret = -EIO;
		if (!valid_signal(data))
			break;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		set_single_step(child);
		child->exit_code = data;
		/* give it a chance to run. */
		wake_up_process(child);
		ret = 0;
		break;
	}

	case PTRACE_DETACH:
		ret = ptrace_detach(child, data);
		break;

#ifdef CONFIG_ALTIVEC
	case PTRACE_GETVRREGS:
		/* Get the child altivec register state. */
		preempt_disable();
		if (child->thread.regs->msr & MSR_VEC)
			giveup_altivec(child);
		preempt_enable();
		ret = get_vrregs((unsigned long __user *)data, child);
		break;

	case PTRACE_SETVRREGS:
		/* Set the child altivec register state. */
		/* this is to clear the MSR_VEC bit to force a reload
		 * of register state from memory */
		preempt_disable();
		if (child->thread.regs->msr & MSR_VEC)
			giveup_altivec(child);
		preempt_enable();
		ret = set_vrregs(child, (unsigned long __user *)data);
		break;
#endif
#ifdef CONFIG_SPE
	case PTRACE_GETEVRREGS:
		/* Get the child spe register state. */
		if (child->thread.regs->msr & MSR_SPE)
			giveup_spe(child);
		ret = get_evrregs((unsigned long __user *)data, child);
		break;

	case PTRACE_SETEVRREGS:
		/* Set the child spe register state. */
		/* this is to clear the MSR_SPE bit to force a reload
		 * of register state from memory */
		if (child->thread.regs->msr & MSR_SPE)
			giveup_spe(child);
		ret = set_evrregs(child, (unsigned long __user *)data);
		break;
#endif

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}
out_tsk:
	put_task_struct(child);
out:
	unlock_kernel();
	return ret;
}

static void do_syscall_trace(void)
{
	/* the 0x80 provides a way for the tracing parent to distinguish
	   between a syscall stop and SIGTRAP delivery */
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				 ? 0x80 : 0));

	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}

void do_syscall_trace_enter(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE)
	    && (current->ptrace & PT_PTRACED))
		do_syscall_trace();

	if (unlikely(current->audit_context))
		audit_syscall_entry(current, AUDIT_ARCH_PPC,
				    regs->gpr[0],
				    regs->gpr[3], regs->gpr[4],
				    regs->gpr[5], regs->gpr[6]);
}

void do_syscall_trace_leave(struct pt_regs *regs)
{
	secure_computing(regs->gpr[0]);

	if (unlikely(current->audit_context))
		audit_syscall_exit(current,
				   (regs->ccr&0x1000)?AUDITSC_FAILURE:AUDITSC_SUCCESS,
				   regs->result);

	if ((test_thread_flag(TIF_SYSCALL_TRACE))
	    && (current->ptrace & PT_PTRACED))
		do_syscall_trace();
}

EXPORT_SYMBOL(do_syscall_trace_enter);
EXPORT_SYMBOL(do_syscall_trace_leave);
