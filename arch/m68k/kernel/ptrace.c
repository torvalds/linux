/*
 *  linux/arch/m68k/kernel/ptrace.c
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
#define TRACE_BITS 0xC000
#define T1_BIT 0x8000
#define T0_BIT 0x4000

/* Find the stack offset for a register, relative to thread.esp0. */
#define PT_REG(reg)	((long)&((struct pt_regs *)0)->reg)
#define SW_REG(reg)	((long)&((struct switch_stack *)0)->reg \
			 - sizeof(struct switch_stack))
/* Mapping from PT_xxx to the stack offset at which the register is
   saved.  Notice that usp has no stack-slot and needs to be treated
   specially (see get_reg/put_reg below). */
static const int regoff[] = {
	[0]	= PT_REG(d1),
	[1]	= PT_REG(d2),
	[2]	= PT_REG(d3),
	[3]	= PT_REG(d4),
	[4]	= PT_REG(d5),
	[5]	= SW_REG(d6),
	[6]	= SW_REG(d7),
	[7]	= PT_REG(a0),
	[8]	= PT_REG(a1),
	[9]	= PT_REG(a2),
	[10]	= SW_REG(a3),
	[11]	= SW_REG(a4),
	[12]	= SW_REG(a5),
	[13]	= SW_REG(a6),
	[14]	= PT_REG(d0),
	[15]	= -1,
	[16]	= PT_REG(orig_d0),
	[17]	= PT_REG(sr),
	[18]	= PT_REG(pc),
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
	/* Need to take stkadj into account. */
	if (regno == PT_SR || regno == PT_PC) {
		long stkadj = *(long *)(task->thread.esp0 + PT_REG(stkadj));
		addr = (unsigned long *) ((unsigned long)addr + stkadj);
		/* The sr is actually a 16 bit register.  */
		if (regno == PT_SR)
			return *(unsigned short *)addr;
	}
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
		addr = (unsigned long *)(task->thread.esp0 + regoff[regno]);
	else
		return -1;
	/* Need to take stkadj into account. */
	if (regno == PT_SR || regno == PT_PC) {
		long stkadj = *(long *)(task->thread.esp0 + PT_REG(stkadj));
		addr = (unsigned long *) ((unsigned long)addr + stkadj);
		/* The sr is actually a 16 bit register.  */
		if (regno == PT_SR) {
			*(unsigned short *)addr = data;
			return 0;
		}
	}
	*addr = data;
	return 0;
}

/*
 * Make sure the single step bit is not set.
 */
static inline void singlestep_disable(struct task_struct *child)
{
	unsigned long tmp = get_reg(child, PT_SR) & ~TRACE_BITS;
	put_reg(child, PT_SR, tmp);
	clear_tsk_thread_flag(child, TIF_DELAYED_TRACE);
}

/*
 * Called by kernel/ptrace.c when detaching..
 */
void ptrace_disable(struct task_struct *child)
{
	singlestep_disable(child);
}

void user_enable_single_step(struct task_struct *child)
{
	unsigned long tmp = get_reg(child, PT_SR) & ~TRACE_BITS;
	put_reg(child, PT_SR, tmp | T1_BIT);
	set_tsk_thread_flag(child, TIF_DELAYED_TRACE);
}

void user_enable_block_step(struct task_struct *child)
{
	unsigned long tmp = get_reg(child, PT_SR) & ~TRACE_BITS;
	put_reg(child, PT_SR, tmp | T0_BIT);
}

void user_disable_single_step(struct task_struct *child)
{
	singlestep_disable(child);
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	unsigned long tmp;
	int i, ret = 0;

	switch (request) {
	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR:
		if (addr & 3)
			goto out_eio;
		addr >>= 2;	/* temporary hack. */

		if (addr >= 0 && addr < 19) {
			tmp = get_reg(child, addr);
		} else if (addr >= 21 && addr < 49) {
			tmp = child->thread.fp[addr - 21];
			/* Convert internal fpu reg representation
			 * into long double format
			 */
			if (FPU_IS_EMU && (addr < 45) && !(addr % 3))
				tmp = ((tmp & 0xffff0000) << 15) |
				      ((tmp & 0x0000ffff) << 16);
		} else
			goto out_eio;
		ret = put_user(tmp, (unsigned long *)data);
		break;

	case PTRACE_POKEUSR:	/* write the word at location addr in the USER area */
		if (addr & 3)
			goto out_eio;
		addr >>= 2;	/* temporary hack. */

		if (addr == PT_SR) {
			data &= SR_MASK;
			data |= get_reg(child, PT_SR) & ~SR_MASK;
		}
		if (addr >= 0 && addr < 19) {
			if (put_reg(child, addr, data))
				goto out_eio;
		} else if (addr >= 21 && addr < 48) {
			/* Convert long double format
			 * into internal fpu reg representation
			 */
			if (FPU_IS_EMU && (addr < 45) && !(addr % 3)) {
				data = (unsigned long)data << 15;
				data = (data & 0xffff0000) |
				       ((data & 0x0000ffff) >> 1);
			}
			child->thread.fp[addr - 21] = data;
		} else
			goto out_eio;
		break;

	case PTRACE_GETREGS:	/* Get all gp regs from the child. */
		for (i = 0; i < 19; i++) {
			tmp = get_reg(child, i);
			ret = put_user(tmp, (unsigned long *)data);
			if (ret)
				break;
			data += sizeof(long);
		}
		break;

	case PTRACE_SETREGS:	/* Set all gp regs in the child. */
		for (i = 0; i < 19; i++) {
			ret = get_user(tmp, (unsigned long *)data);
			if (ret)
				break;
			if (i == PT_SR) {
				tmp &= SR_MASK;
				tmp |= get_reg(child, PT_SR) & ~SR_MASK;
			}
			put_reg(child, i, tmp);
			data += sizeof(long);
		}
		break;

	case PTRACE_GETFPREGS:	/* Get the child FPU state. */
		if (copy_to_user((void *)data, &child->thread.fp,
				 sizeof(struct user_m68kfp_struct)))
			ret = -EFAULT;
		break;

	case PTRACE_SETFPREGS:	/* Set the child FPU state. */
		if (copy_from_user(&child->thread.fp, (void *)data,
				   sizeof(struct user_m68kfp_struct)))
			ret = -EFAULT;
		break;

	case PTRACE_GET_THREAD_AREA:
		ret = put_user(task_thread_info(child)->tp_value,
			       (unsigned long __user *)data);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
out_eio:
	return -EIO;
}

asmlinkage void syscall_trace(void)
{
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
