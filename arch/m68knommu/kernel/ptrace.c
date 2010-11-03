/*
 *  linux/arch/m68knommu/kernel/ptrace.c
 *
 *  Copyright (C) 1994 by Hamish Macdonald
 *  Taken from linux/kernel/ptrace.c and modified for M680x0.
 *  linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of
 * this archive for more details.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/signal.h>
#include <linux/tracehook.h>

#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/* determines which bits in the SR the user has access to. */
/* 1 = access 0 = no access */
#define SR_MASK 0x001f

/* sets the trace bits. */
#define TRACE_BITS 0x8000

/* Find the stack offset for a register, relative to thread.esp0. */
#define PT_REG(reg)	((long)&((struct pt_regs *)0)->reg)
#define SW_REG(reg)	((long)&((struct switch_stack *)0)->reg \
			 - sizeof(struct switch_stack))
/* Mapping from PT_xxx to the stack offset at which the register is
   saved.  Notice that usp has no stack-slot and needs to be treated
   specially (see get_reg/put_reg below). */
static int regoff[] = {
	PT_REG(d1), PT_REG(d2), PT_REG(d3), PT_REG(d4),
	PT_REG(d5), SW_REG(d6), SW_REG(d7), PT_REG(a0),
	PT_REG(a1), PT_REG(a2), SW_REG(a3), SW_REG(a4),
	SW_REG(a5), SW_REG(a6), PT_REG(d0), -1,
	PT_REG(orig_d0), PT_REG(sr), PT_REG(pc),
};

/*
 * Get contents of register REGNO in task TASK.
 */
static inline long get_reg(struct task_struct *task, int regno)
{
	unsigned long *addr;

	if (regno == PT_USP)
		addr = &task->thread.usp;
	else if (regno < ARRAY_SIZE(regoff))
		addr = (unsigned long *)(task->thread.esp0 + regoff[regno]);
	else
		return 0;
	return *addr;
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int put_reg(struct task_struct *task, int regno,
			  unsigned long data)
{
	unsigned long *addr;

	if (regno == PT_USP)
		addr = &task->thread.usp;
	else if (regno < ARRAY_SIZE(regoff))
		addr = (unsigned long *) (task->thread.esp0 + regoff[regno]);
	else
		return -1;
	*addr = data;
	return 0;
}

void user_enable_single_step(struct task_struct *task)
{
	unsigned long srflags;
	srflags = get_reg(task, PT_SR) | (TRACE_BITS << 16);
	put_reg(task, PT_SR, srflags);
}

void user_disable_single_step(struct task_struct *task)
{
	unsigned long srflags;
	srflags = get_reg(task, PT_SR) & ~(TRACE_BITS << 16);
	put_reg(task, PT_SR, srflags);
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure the single step bit is not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* make sure the single step bit is not set. */
	user_disable_single_step(child);
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret;
	int regno = addr >> 2;
	unsigned long __user *datap = (unsigned long __user *) data;

	switch (request) {
		/* read the word at location addr in the USER area. */
		case PTRACE_PEEKUSR: {
			unsigned long tmp;
			
			ret = -EIO;
			if ((addr & 3) || addr > sizeof(struct user) - 3)
				break;
			
			tmp = 0;  /* Default return condition */
			ret = -EIO;
			if (regno < 19) {
				tmp = get_reg(child, regno);
				if (regno == PT_SR)
					tmp >>= 16;
			} else if (regno >= 21 && regno < 49) {
				tmp = child->thread.fp[regno - 21];
			} else if (regno == 49) {
				tmp = child->mm->start_code;
			} else if (regno == 50) {
				tmp = child->mm->start_data;
			} else if (regno == 51) {
				tmp = child->mm->end_code;
			} else
				break;
			ret = put_user(tmp, datap);
			break;
		}

		case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
			ret = -EIO;
			if ((addr & 3) || addr > sizeof(struct user) - 3)
				break;

			if (regno == PT_SR) {
				data &= SR_MASK;
				data <<= 16;
				data |= get_reg(child, PT_SR) & ~(SR_MASK << 16);
			}
			if (regno < 19) {
				if (put_reg(child, regno, data))
					break;
				ret = 0;
				break;
			}
			if (regno >= 21 && regno < 48)
			{
				child->thread.fp[regno - 21] = data;
				ret = 0;
			}
			break;

		case PTRACE_GETREGS: { /* Get all gp regs from the child. */
		  	int i;
			unsigned long tmp;
			for (i = 0; i < 19; i++) {
			    tmp = get_reg(child, i);
			    if (i == PT_SR)
				tmp >>= 16;
			    if (put_user(tmp, datap)) {
				ret = -EFAULT;
				break;
			    }
			    datap++;
			}
			ret = 0;
			break;
		}

		case PTRACE_SETREGS: { /* Set all gp regs in the child. */
			int i;
			unsigned long tmp;
			for (i = 0; i < 19; i++) {
			    if (get_user(tmp, datap)) {
				ret = -EFAULT;
				break;
			    }
			    if (i == PT_SR) {
				tmp &= SR_MASK;
				tmp <<= 16;
				tmp |= get_reg(child, PT_SR) & ~(SR_MASK << 16);
			    }
			    put_reg(child, i, tmp);
			    datap++;
			}
			ret = 0;
			break;
		}

#ifdef PTRACE_GETFPREGS
		case PTRACE_GETFPREGS: { /* Get the child FPU state. */
			ret = 0;
			if (copy_to_user(datap, &child->thread.fp,
					 sizeof(struct user_m68kfp_struct)))
				ret = -EFAULT;
			break;
		}
#endif

#ifdef PTRACE_SETFPREGS
		case PTRACE_SETFPREGS: { /* Set the child FPU state. */
			ret = 0;
			if (copy_from_user(&child->thread.fp, datap,
					   sizeof(struct user_m68kfp_struct)))
				ret = -EFAULT;
			break;
		}
#endif

	case PTRACE_GET_THREAD_AREA:
		ret = put_user(task_thread_info(child)->tp_value, datap);
		break;

		default:
			ret = ptrace_request(child, request, addr, data);
			break;
	}
	return ret;
}

asmlinkage int syscall_trace_enter(void)
{
	int ret = 0;

	if (test_thread_flag(TIF_SYSCALL_TRACE))
		ret = tracehook_report_syscall_entry(task_pt_regs(current));
	return ret;
}

asmlinkage void syscall_trace_leave(void)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(task_pt_regs(current), 0);
}
