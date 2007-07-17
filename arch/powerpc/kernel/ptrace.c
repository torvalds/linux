/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/m68k/kernel/ptrace.c"
 *  Copyright (C) 1994 by Hamish Macdonald
 *  Taken from linux/kernel/ptrace.c and modified for M680x0.
 *  linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 *
 * Modified by Cort Dougan (cort@hq.fsmlabs.com)
 * and Paul Mackerras (paulus@samba.org).
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
 * this archive for more details.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/security.h>
#include <linux/signal.h>
#include <linux/seccomp.h>
#include <linux/audit.h>
#ifdef CONFIG_PPC32
#include <linux/module.h>
#endif

#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Set of msr bits that gdb can change on behalf of a process.
 */
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
#define MSR_DEBUGCHANGE	0
#else
#define MSR_DEBUGCHANGE	(MSR_SE | MSR_BE)
#endif

/*
 * Max register writeable via put_reg
 */
#ifdef CONFIG_PPC32
#define PT_MAX_PUT_REG	PT_MQ
#else
#define PT_MAX_PUT_REG	PT_CCR
#endif

/*
 * Get contents of register REGNO in task TASK.
 */
unsigned long ptrace_get_reg(struct task_struct *task, int regno)
{
	unsigned long tmp = 0;

	if (task->thread.regs == NULL)
		return -EIO;

	if (regno == PT_MSR) {
		tmp = ((unsigned long *)task->thread.regs)[PT_MSR];
		return tmp | task->thread.fpexc_mode;
	}

	if (regno < (sizeof(struct pt_regs) / sizeof(unsigned long)))
		return ((unsigned long *)task->thread.regs)[regno];

	return -EIO;
}

/*
 * Write contents of register REGNO in task TASK.
 */
int ptrace_put_reg(struct task_struct *task, int regno, unsigned long data)
{
	if (task->thread.regs == NULL)
		return -EIO;

	if (regno <= PT_MAX_PUT_REG || regno == PT_TRAP) {
		if (regno == PT_MSR)
			data = (data & MSR_DEBUGCHANGE)
				| (task->thread.regs->msr & ~MSR_DEBUGCHANGE);
		/* We prevent mucking around with the reserved area of trap
		 * which are used internally by the kernel
		 */
		if (regno == PT_TRAP)
			data &= 0xfff0;
		((unsigned long *)task->thread.regs)[regno] = data;
		return 0;
	}
	return -EIO;
}


static int get_fpregs(void __user *data, struct task_struct *task,
		      int has_fpscr)
{
	unsigned int count = has_fpscr ? 33 : 32;

	if (copy_to_user(data, task->thread.fpr, count * sizeof(double)))
		return -EFAULT;
	return 0;
}

static int set_fpregs(void __user *data, struct task_struct *task,
		      int has_fpscr)
{
	unsigned int count = has_fpscr ? 33 : 32;

	if (copy_from_user(task->thread.fpr, data, count * sizeof(double)))
		return -EFAULT;
	return 0;
}


#ifdef CONFIG_ALTIVEC
/*
 * Get/set all the altivec registers vr0..vr31, vscr, vrsave, in one go.
 * The transfer totals 34 quadword.  Quadwords 0-31 contain the
 * corresponding vector registers.  Quadword 32 contains the vscr as the
 * last word (offset 12) within that quadword.  Quadword 33 contains the
 * vrsave as the first word (offset 0) within the quadword.
 *
 * This definition of the VMX state is compatible with the current PPC32
 * ptrace interface.  This allows signal handling and ptrace to use the
 * same structures.  This also simplifies the implementation of a bi-arch
 * (combined (32- and 64-bit) gdb.
 */

/*
 * Get contents of AltiVec register state in task TASK
 */
static int get_vrregs(unsigned long __user *data, struct task_struct *task)
{
	unsigned long regsize;

	/* copy AltiVec registers VR[0] .. VR[31] */
	regsize = 32 * sizeof(vector128);
	if (copy_to_user(data, task->thread.vr, regsize))
		return -EFAULT;
	data += (regsize / sizeof(unsigned long));

	/* copy VSCR */
	regsize = 1 * sizeof(vector128);
	if (copy_to_user(data, &task->thread.vscr, regsize))
		return -EFAULT;
	data += (regsize / sizeof(unsigned long));

	/* copy VRSAVE */
	if (put_user(task->thread.vrsave, (u32 __user *)data))
		return -EFAULT;

	return 0;
}

/*
 * Write contents of AltiVec register state into task TASK.
 */
static int set_vrregs(struct task_struct *task, unsigned long __user *data)
{
	unsigned long regsize;

	/* copy AltiVec registers VR[0] .. VR[31] */
	regsize = 32 * sizeof(vector128);
	if (copy_from_user(task->thread.vr, data, regsize))
		return -EFAULT;
	data += (regsize / sizeof(unsigned long));

	/* copy VSCR */
	regsize = 1 * sizeof(vector128);
	if (copy_from_user(&task->thread.vscr, data, regsize))
		return -EFAULT;
	data += (regsize / sizeof(unsigned long));

	/* copy VRSAVE */
	if (get_user(task->thread.vrsave, (u32 __user *)data))
		return -EFAULT;

	return 0;
}
#endif /* CONFIG_ALTIVEC */

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
static int get_evrregs(unsigned long *data, struct task_struct *task)
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
static int set_evrregs(struct task_struct *task, unsigned long *data)
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


static void set_single_step(struct task_struct *task)
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
	set_tsk_thread_flag(task, TIF_SINGLESTEP);
}

static void clear_single_step(struct task_struct *task)
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
	clear_tsk_thread_flag(task, TIF_SINGLESTEP);
}

static int ptrace_set_debugreg(struct task_struct *task, unsigned long addr,
			       unsigned long data)
{
	/* We only support one DABR and no IABRS at the moment */
	if (addr > 0)
		return -EINVAL;

	/* The bottom 3 bits are flags */
	if ((data & ~0x7UL) >= TASK_SIZE)
		return -EIO;

	/* Ensure translation is on */
	if (data && !(data & DABR_TRANSLATION))
		return -EIO;

	task->thread.dabr = data;
	return 0;
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

/*
 * Here are the old "legacy" powerpc specific getregs/setregs ptrace calls,
 * we mark them as obsolete now, they will be removed in a future version
 */
static long arch_ptrace_old(struct task_struct *child, long request, long addr,
			    long data)
{
	int ret = -EPERM;

	switch(request) {
	case PPC_PTRACE_GETREGS: { /* Get GPRs 0 - 31. */
		int i;
		unsigned long *reg = &((unsigned long *)child->thread.regs)[0];
		unsigned long __user *tmp = (unsigned long __user *)addr;

		for (i = 0; i < 32; i++) {
			ret = put_user(*reg, tmp);
			if (ret)
				break;
			reg++;
			tmp++;
		}
		break;
	}

	case PPC_PTRACE_SETREGS: { /* Set GPRs 0 - 31. */
		int i;
		unsigned long *reg = &((unsigned long *)child->thread.regs)[0];
		unsigned long __user *tmp = (unsigned long __user *)addr;

		for (i = 0; i < 32; i++) {
			ret = get_user(*reg, tmp);
			if (ret)
				break;
			reg++;
			tmp++;
		}
		break;
	}

	case PPC_PTRACE_GETFPREGS: { /* Get FPRs 0 - 31. */
		flush_fp_to_thread(child);
		ret = get_fpregs((void __user *)addr, child, 0);
		break;
	}

	case PPC_PTRACE_SETFPREGS: { /* Get FPRs 0 - 31. */
		flush_fp_to_thread(child);
		ret = set_fpregs((void __user *)addr, child, 0);
		break;
	}

	}
	return ret;
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int ret = -EPERM;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA:
		ret = generic_ptrace_peekdata(child, addr, data);
		break;

	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		unsigned long index, tmp;

		ret = -EIO;
		/* convert to index and check */
#ifdef CONFIG_PPC32
		index = (unsigned long) addr >> 2;
		if ((addr & 3) || (index > PT_FPSCR)
		    || (child->thread.regs == NULL))
#else
		index = (unsigned long) addr >> 3;
		if ((addr & 7) || (index > PT_FPSCR))
#endif
			break;

		CHECK_FULL_REGS(child->thread.regs);
		if (index < PT_FPR0) {
			tmp = ptrace_get_reg(child, (int) index);
		} else {
			flush_fp_to_thread(child);
			tmp = ((unsigned long *)child->thread.fpr)[index - PT_FPR0];
		}
		ret = put_user(tmp,(unsigned long __user *) data);
		break;
	}

	/* If I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = generic_ptrace_pokedata(child, addr, data);
		break;

	/* write the word at location addr in the USER area */
	case PTRACE_POKEUSR: {
		unsigned long index;

		ret = -EIO;
		/* convert to index and check */
#ifdef CONFIG_PPC32
		index = (unsigned long) addr >> 2;
		if ((addr & 3) || (index > PT_FPSCR)
		    || (child->thread.regs == NULL))
#else
		index = (unsigned long) addr >> 3;
		if ((addr & 7) || (index > PT_FPSCR))
#endif
			break;

		CHECK_FULL_REGS(child->thread.regs);
		if (index < PT_FPR0) {
			ret = ptrace_put_reg(child, index, data);
		} else {
			flush_fp_to_thread(child);
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
		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
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

	case PTRACE_GET_DEBUGREG: {
		ret = -EINVAL;
		/* We only support one DABR and no IABRS at the moment */
		if (addr > 0)
			break;
		ret = put_user(child->thread.dabr,
			       (unsigned long __user *)data);
		break;
	}

	case PTRACE_SET_DEBUGREG:
		ret = ptrace_set_debugreg(child, addr, data);
		break;

	case PTRACE_DETACH:
		ret = ptrace_detach(child, data);
		break;

#ifdef CONFIG_PPC64
	case PTRACE_GETREGS64:
#endif
	case PTRACE_GETREGS: { /* Get all pt_regs from the child. */
		int ui;
	  	if (!access_ok(VERIFY_WRITE, (void __user *)data,
			       sizeof(struct pt_regs))) {
			ret = -EIO;
			break;
		}
		ret = 0;
		for (ui = 0; ui < PT_REGS_COUNT; ui ++) {
			ret |= __put_user(ptrace_get_reg(child, ui),
					  (unsigned long __user *) data);
			data += sizeof(long);
		}
		break;
	}

#ifdef CONFIG_PPC64
	case PTRACE_SETREGS64:
#endif
	case PTRACE_SETREGS: { /* Set all gp regs in the child. */
		unsigned long tmp;
		int ui;
	  	if (!access_ok(VERIFY_READ, (void __user *)data,
			       sizeof(struct pt_regs))) {
			ret = -EIO;
			break;
		}
		ret = 0;
		for (ui = 0; ui < PT_REGS_COUNT; ui ++) {
			ret = __get_user(tmp, (unsigned long __user *) data);
			if (ret)
				break;
			ptrace_put_reg(child, ui, tmp);
			data += sizeof(long);
		}
		break;
	}

	case PTRACE_GETFPREGS: { /* Get the child FPU state (FPR0...31 + FPSCR) */
		flush_fp_to_thread(child);
		ret = get_fpregs((void __user *)data, child, 1);
		break;
	}

	case PTRACE_SETFPREGS: { /* Set the child FPU state (FPR0...31 + FPSCR) */
		flush_fp_to_thread(child);
		ret = set_fpregs((void __user *)data, child, 1);
		break;
	}

#ifdef CONFIG_ALTIVEC
	case PTRACE_GETVRREGS:
		/* Get the child altivec register state. */
		flush_altivec_to_thread(child);
		ret = get_vrregs((unsigned long __user *)data, child);
		break;

	case PTRACE_SETVRREGS:
		/* Set the child altivec register state. */
		flush_altivec_to_thread(child);
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

	/* Old reverse args ptrace callss */
	case PPC_PTRACE_GETREGS: /* Get GPRs 0 - 31. */
	case PPC_PTRACE_SETREGS: /* Set GPRs 0 - 31. */
	case PPC_PTRACE_GETFPREGS: /* Get FPRs 0 - 31. */
	case PPC_PTRACE_SETFPREGS: /* Get FPRs 0 - 31. */
		ret = arch_ptrace_old(child, request, addr, data);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}
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
	secure_computing(regs->gpr[0]);

	if (test_thread_flag(TIF_SYSCALL_TRACE)
	    && (current->ptrace & PT_PTRACED))
		do_syscall_trace();

	if (unlikely(current->audit_context)) {
#ifdef CONFIG_PPC64
		if (!test_thread_flag(TIF_32BIT))
			audit_syscall_entry(AUDIT_ARCH_PPC64,
					    regs->gpr[0],
					    regs->gpr[3], regs->gpr[4],
					    regs->gpr[5], regs->gpr[6]);
		else
#endif
			audit_syscall_entry(AUDIT_ARCH_PPC,
					    regs->gpr[0],
					    regs->gpr[3] & 0xffffffff,
					    regs->gpr[4] & 0xffffffff,
					    regs->gpr[5] & 0xffffffff,
					    regs->gpr[6] & 0xffffffff);
	}
}

void do_syscall_trace_leave(struct pt_regs *regs)
{
	if (unlikely(current->audit_context))
		audit_syscall_exit((regs->ccr&0x10000000)?AUDITSC_FAILURE:AUDITSC_SUCCESS,
				   regs->result);

	if ((test_thread_flag(TIF_SYSCALL_TRACE)
	     || test_thread_flag(TIF_SINGLESTEP))
	    && (current->ptrace & PT_PTRACED))
		do_syscall_trace();
}
