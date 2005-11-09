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
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/config.h>
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
#define TRACE_BITS 0x8000

/* Find the stack offset for a register, relative to thread.esp0. */
#define PT_REG(reg)	((long)&((struct pt_regs *)0)->reg)
#define SW_REG(reg)	((long)&((struct switch_stack *)0)->reg \
			 - sizeof(struct switch_stack))
/* Mapping from PT_xxx to the stack offset at which the register is
   saved.  Notice that usp has no stack-slot and needs to be treated
   specially (see get_reg/put_reg below). */
static int regoff[] = {
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
	else if (regno < sizeof(regoff)/sizeof(regoff[0]))
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
	else if (regno < sizeof(regoff)/sizeof(regoff[0]))
		addr = (unsigned long *)(task->thread.esp0 + regoff[regno]);
	else
		return -1;
	*addr = data;
	return 0;
}

/*
 * Make sure the single step bit is not set.
 */
static inline void singlestep_disable(struct task_struct *child)
{
	unsigned long tmp = get_reg(child, PT_SR) & ~(TRACE_BITS << 16);
	put_reg(child, PT_SR, tmp);
	child->thread.work.delayed_trace = 0;
}

/*
 * Called by kernel/ptrace.c when detaching..
 */
void ptrace_disable(struct task_struct *child)
{
	singlestep_disable(child);
	child->thread.work.syscall_trace = 0;
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	unsigned long tmp;
	int i, ret = 0;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKTEXT:	/* read word at location addr. */
	case PTRACE_PEEKDATA:
		i = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		if (i != sizeof(tmp))
			goto out_eio;
		ret = put_user(tmp, (unsigned long *)data);
		break;

	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR:
		if (addr & 3)
			goto out_eio;
		addr >>= 2;	/* temporary hack. */

		if (addr >= 0 && addr < 19) {
			tmp = get_reg(child, addr);
			if (addr == PT_SR)
				tmp >>= 16;
		} else if (addr >= 21 && addr < 49) {
			tmp = child->thread.fp[addr - 21];
			/* Convert internal fpu reg representation
			 * into long double format
			 */
			if (FPU_IS_EMU && (addr < 45) && !(addr % 3))
				tmp = ((tmp & 0xffff0000) << 15) |
				      ((tmp & 0x0000ffff) << 16);
		} else
			break;
		ret = put_user(tmp, (unsigned long *)data);
		break;

	/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT:	/* write the word at location addr. */
	case PTRACE_POKEDATA:
		if (access_process_vm(child, addr, &data, sizeof(data), 1) != sizeof(data))
			goto out_eio;
		break;

	case PTRACE_POKEUSR:	/* write the word at location addr in the USER area */
		if (addr & 3)
			goto out_eio;
		addr >>= 2;	/* temporary hack. */

		if (addr == PT_SR) {
			data &= SR_MASK;
			data <<= 16;
			data |= get_reg(child, PT_SR) & ~(SR_MASK << 16);
		} else if (addr >= 0 && addr < 19) {
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

	case PTRACE_SYSCALL:	/* continue and stop at next (return from) syscall */
	case PTRACE_CONT:	/* restart after signal. */
		if (!valid_signal(data))
			goto out_eio;

		if (request == PTRACE_SYSCALL)
			child->thread.work.syscall_trace = ~0;
		else
			child->thread.work.syscall_trace = 0;
		child->exit_code = data;
		singlestep_disable(child);
		wake_up_process(child);
		break;

	/*
	 * make the child exit.  Best I can do is send it a sigkill.
	 * perhaps it should be put in the status that it wants to
	 * exit.
	 */
	case PTRACE_KILL:
		if (child->exit_state == EXIT_ZOMBIE) /* already dead */
			break;
		child->exit_code = SIGKILL;
		singlestep_disable(child);
		wake_up_process(child);
		break;

	case PTRACE_SINGLESTEP:	/* set the trap flag. */
		if (!valid_signal(data))
			goto out_eio;

		child->thread.work.syscall_trace = 0;
		tmp = get_reg(child, PT_SR) | (TRACE_BITS << 16);
		put_reg(child, PT_SR, tmp);
		child->thread.work.delayed_trace = 1;

		child->exit_code = data;
		/* give it a chance to run. */
		wake_up_process(child);
		break;

	case PTRACE_DETACH:	/* detach a process that was attached. */
		ret = ptrace_detach(child, data);
		break;

	case PTRACE_GETREGS:	/* Get all gp regs from the child. */
		for (i = 0; i < 19; i++) {
			tmp = get_reg(child, i);
			if (i == PT_SR)
				tmp >>= 16;
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
				tmp <<= 16;
				tmp |= get_reg(child, PT_SR) & ~(SR_MASK << 16);
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
	if (!current->thread.work.delayed_trace &&
	    !current->thread.work.syscall_trace)
		return;
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
