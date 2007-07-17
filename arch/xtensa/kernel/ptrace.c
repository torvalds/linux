// TODO some minor issues
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005  Tensilica Inc.
 *
 * Joe Taylor	<joe@tensilica.com, joetylr@yahoo.com>
 * Chris Zankel <chris@zankel.net>
 * Scott Foehner<sfoehner@yahoo.com>,
 * Kevin Chea
 * Marc Gauthier<marc@tensilica.com> <marc@alumni.uwaterloo.ca>
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/security.h>
#include <linux/signal.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/ptrace.h>
#include <asm/elf.h>

#define TEST_KERNEL	// verify kernel operations FIXME: remove


/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */

void ptrace_disable(struct task_struct *child)
{
	/* Nothing to do.. */
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int ret = -EPERM;

	switch (request) {
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA:
		ret = generic_ptrace_peekdata(child, addr, data);
		goto out;

	/* Read the word at location addr in the USER area.  */

	case PTRACE_PEEKUSR:
		{
		struct pt_regs *regs;
		unsigned long tmp;

		regs = task_pt_regs(child);
		tmp = 0;  /* Default return value. */

		switch(addr) {

		case REG_AR_BASE ... REG_AR_BASE + XCHAL_NUM_AREGS - 1:
			{
			int ar = addr - REG_AR_BASE - regs->windowbase * 4;
			ar &= (XCHAL_NUM_AREGS - 1);
			if (ar < 16 && ar + (regs->wmask >> 4) * 4 >= 0)
				tmp = regs->areg[ar];
			else
				ret = -EIO;
			break;
			}
		case REG_A_BASE ... REG_A_BASE + 15:
			tmp = regs->areg[addr - REG_A_BASE];
			break;
		case REG_PC:
			tmp = regs->pc;
			break;
		case REG_PS:
			/* Note:  PS.EXCM is not set while user task is running;
			 * its being set in regs is for exception handling
			 * convenience.  */
			tmp = (regs->ps & ~(1 << PS_EXCM_BIT));
			break;
		case REG_WB:
			tmp = regs->windowbase;
			break;
		case REG_WS:
			tmp = regs->windowstart;
			break;
		case REG_LBEG:
			tmp = regs->lbeg;
			break;
		case REG_LEND:
			tmp = regs->lend;
			break;
		case REG_LCOUNT:
			tmp = regs->lcount;
			break;
		case REG_SAR:
			tmp = regs->sar;
			break;
		case REG_DEPC:
			tmp = regs->depc;
			break;
		case REG_EXCCAUSE:
			tmp = regs->exccause;
			break;
		case REG_EXCVADDR:
			tmp = regs->excvaddr;
			break;
		case SYSCALL_NR:
			tmp = regs->syscall;
			break;
		default:
			tmp = 0;
			ret = -EIO;
			goto out;
		}
		ret = put_user(tmp, (unsigned long *) data);
		goto out;
		}

	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		if (access_process_vm(child, addr, &data, sizeof(data), 1)
		    == sizeof(data))
			break;
		ret = -EIO;
		goto out;

	case PTRACE_POKEUSR:
		{
		struct pt_regs *regs;
		regs = task_pt_regs(child);

		switch (addr) {
		case REG_AR_BASE ... REG_AR_BASE + XCHAL_NUM_AREGS - 1:
			{
			int ar = addr - REG_AR_BASE - regs->windowbase * 4;
			if (ar < 16 && ar + (regs->wmask >> 4) * 4 >= 0)
				regs->areg[ar & (XCHAL_NUM_AREGS - 1)] = data;
			else
				ret = -EIO;
			break;
			}
		case REG_A_BASE ... REG_A_BASE + 15:
			regs->areg[addr - REG_A_BASE] = data;
			break;
		case REG_PC:
			regs->pc = data;
			break;
		case SYSCALL_NR:
			regs->syscall = data;
			break;
#ifdef TEST_KERNEL
		case REG_WB:
			regs->windowbase = data;
			break;
		case REG_WS:
			regs->windowstart = data;
			break;
#endif

		default:
			/* The rest are not allowed. */
			ret = -EIO;
			break;
		}
		break;
		}

	/* continue and stop at next (return from) syscall */
	case PTRACE_SYSCALL:
	case PTRACE_CONT: /* restart after signal. */
	{
		ret = -EIO;
		if (!valid_signal(data))
			break;
		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->exit_code = data;
		/* Make sure the single step bit is not set. */
		child->ptrace &= ~PT_SINGLESTEP;
		wake_up_process(child);
		ret = 0;
		break;
	}

	/*
	 * make the child exit.  Best I can do is send it a sigkill.
	 * perhaps it should be put in the status that it wants to
	 * exit.
	 */
	case PTRACE_KILL:
		ret = 0;
		if (child->exit_state == EXIT_ZOMBIE)	/* already dead */
			break;
		child->exit_code = SIGKILL;
		child->ptrace &= ~PT_SINGLESTEP;
		wake_up_process(child);
		break;

	case PTRACE_SINGLESTEP:
		ret = -EIO;
		if (!valid_signal(data))
			break;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->ptrace |= PT_SINGLESTEP;
		child->exit_code = data;
		wake_up_process(child);
		ret = 0;
		break;

	case PTRACE_GETREGS:
	{
		/* 'data' points to user memory in which to write.
		 * Mainly due to the non-live register values, we
		 * reformat the register values into something more
		 * standard.  For convenience, we use the handy
		 * elf_gregset_t format. */

		xtensa_gregset_t format;
		struct pt_regs *regs = task_pt_regs(child);

		do_copy_regs (&format, regs, child);

		/* Now, copy to user space nice and easy... */
		ret = 0;
		if (copy_to_user((void *)data, &format, sizeof(elf_gregset_t)))
			ret = -EFAULT;
		break;
	}

	case PTRACE_SETREGS:
	{
		/* 'data' points to user memory that contains the new
		 * values in the elf_gregset_t format. */

		xtensa_gregset_t format;
		struct pt_regs *regs = task_pt_regs(child);

		if (copy_from_user(&format,(void *)data,sizeof(elf_gregset_t))){
			ret = -EFAULT;
			break;
		}

		/* FIXME: Perhaps we want some sanity checks on
		 * these user-space values?  See ARM version.  Are
		 * debuggers a security concern? */

		do_restore_regs (&format, regs, child);

		ret = 0;
		break;
	}

	case PTRACE_GETFPREGS:
	{
		/* 'data' points to user memory in which to write.
		 * For convenience, we use the handy
		 * elf_fpregset_t format. */

		elf_fpregset_t fpregs;
		struct pt_regs *regs = task_pt_regs(child);

		do_save_fpregs (&fpregs, regs, child);

		/* Now, copy to user space nice and easy... */
		ret = 0;
		if (copy_to_user((void *)data, &fpregs, sizeof(elf_fpregset_t)))
			ret = -EFAULT;

		break;
	}

	case PTRACE_SETFPREGS:
	{
		/* 'data' points to user memory that contains the new
		 * values in the elf_fpregset_t format.
		 */
		elf_fpregset_t fpregs;
		struct pt_regs *regs = task_pt_regs(child);

		ret = 0;
		if (copy_from_user(&fpregs, (void *)data, sizeof(elf_fpregset_t))) {
			ret = -EFAULT;
			break;
		}

		if (do_restore_fpregs (&fpregs, regs, child))
			ret = -EIO;
		break;
	}

	case PTRACE_GETFPREGSIZE:
		/* 'data' points to 'unsigned long' set to the size
		 * of elf_fpregset_t
		 */
		ret = put_user(sizeof(elf_fpregset_t), (unsigned long *) data);
		break;

	case PTRACE_DETACH: /* detach a process that was attached. */
		ret = ptrace_detach(child, data);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		goto out;
	}
 out:
	return ret;
}

void do_syscall_trace(void)
{
	/*
	 * The 0x80 provides a way for the tracing parent to distinguish
	 * between a syscall stop and SIGTRAP delivery
	 */
	ptrace_notify(SIGTRAP|((current->ptrace & PT_TRACESYSGOOD) ? 0x80 : 0));

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

#if 0
	if (unlikely(current->audit_context))
		audit_syscall_entry(current, AUDIT_ARCH_XTENSA..);
#endif
}

void do_syscall_trace_leave(struct pt_regs *regs)
{
	if ((test_thread_flag(TIF_SYSCALL_TRACE))
			&& (current->ptrace & PT_PTRACED))
		do_syscall_trace();
}

