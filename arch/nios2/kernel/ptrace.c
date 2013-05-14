/*
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 *
 * based on arch/m68knommu/kernel/ptrace.c
 *
 * Copyright (C) 1994 by Hamish Macdonald
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>

#include <linux/uaccess.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/* determines which bits in the SR the user has access to. */
/* 1 = access 0 = no access */
#define SR_MASK 0x00000000

/* Find the stack offset for a register, relative to thread.ksp. */
#define PT_REG(reg)	((long)&((struct pt_regs *)0)->reg)
#define SW_REG(reg)	((long)&((struct switch_stack *)0)->reg \
			 - sizeof(struct switch_stack))

/* Mapping from PT_xxx to the stack offset at which the register is
 * saved.
 */
static int regoff[] = {
		-1, PT_REG(r1), PT_REG(r2), PT_REG(r3),
	PT_REG(r4), PT_REG(r5), PT_REG(r6), PT_REG(r7),
	PT_REG(r8), PT_REG(r9), PT_REG(r10), PT_REG(r11),
	PT_REG(r12), PT_REG(r13), PT_REG(r14), PT_REG(r15),  /* reg 15 */
	SW_REG(r16), SW_REG(r17), SW_REG(r18), SW_REG(r19),
	SW_REG(r20), SW_REG(r21), SW_REG(r22), SW_REG(r23),
		-1,          -1, PT_REG(gp), PT_REG(sp),
	PT_REG(fp), PT_REG(ea),          -1, PT_REG(ra),  /* reg 31 */
	PT_REG(ea),          -1,          -1,          -1,  /* use ea for pc */
		-1,          -1,          -1,          -1,
		-1,          -1,          -1,          -1   /* reg 43 */
};

/*
 * Get contents of register REGNO in task TASK.
 */
static inline long get_reg(struct task_struct *task, int regno)
{
	unsigned long *addr;

	if (regno >= ARRAY_SIZE(regoff) || regoff[regno] == -1)
		return 0;

	addr = (unsigned long *)((char *)task->thread.kregs + regoff[regno]);
	return *addr;
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int put_reg(struct task_struct *task, int regno,
			  unsigned long data)
{
	unsigned long *addr;

	if (regno >= ARRAY_SIZE(regoff) || regoff[regno] == -1)
		return -1;

	addr = (unsigned long *)((char *)task->thread.kregs + regoff[regno]);
	*addr = data;
	return 0;
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Nothing special to do here, no processor debug support.
 */
void ptrace_disable(struct task_struct *child)
{
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	unsigned long tmp;
	unsigned int i;
	int ret;

	switch (request) {
	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR:
		pr_debug("PEEKUSR: addr=0x%08lx\n", addr);
		ret = -EIO;
		if (addr & 3)
			break;

		addr = addr >> 2; /* temporary hack. */
		ret = -EIO;
		if (addr < ARRAY_SIZE(regoff))
			tmp = get_reg(child, addr);
		else if (addr == PT_TEXT_ADDR / 4)
			tmp = child->mm->start_code;
		else if (addr == PT_DATA_ADDR / 4)
			tmp = child->mm->start_data;
		else if (addr == PT_TEXT_END_ADDR / 4)
			tmp = child->mm->end_code;
		else
			break;
		ret = put_user(tmp, (unsigned long *) data);
		pr_debug("PEEKUSR: rdword=0x%08lx\n", tmp);
		break;
	/* write the word at location addr in the USER area */
	case PTRACE_POKEUSR:
		pr_debug("POKEUSR: addr=0x%08lx, data=0x%08lx\n", addr, data);
		ret = -EIO;
		if (addr & 3)
			break;

		addr = addr >> 2; /* temporary hack. */

		if (addr == PTR_ESTATUS) {
			data &= SR_MASK;
			data |= get_reg(child, PTR_ESTATUS) & ~(SR_MASK);
		}
		if (addr < ARRAY_SIZE(regoff)) {
			if (put_reg(child, addr, data))
				break;
			ret = 0;
			break;
		}
		break;
	 /* Get all gp regs from the child. */
	case PTRACE_GETREGS:
		pr_debug("GETREGS\n");
		for (i = 0; i < ARRAY_SIZE(regoff); i++) {
			tmp = get_reg(child, i);
			if (put_user(tmp, (unsigned long *) data)) {
				ret = -EFAULT;
				break;
			}
			data += sizeof(long);
		}
		ret = 0;
		break;
	/* Set all gp regs in the child. */
	case PTRACE_SETREGS:
		pr_debug("SETREGS\n");
		for (i = 0; i < ARRAY_SIZE(regoff); i++) {
			if (get_user(tmp, (unsigned long *) data)) {
				ret = -EFAULT;
				break;
			}
			if (i == PTR_ESTATUS) {
				tmp &= SR_MASK;
				tmp |= get_reg(child, PTR_ESTATUS) & ~(SR_MASK);
			}
			put_reg(child, i, tmp);
			data += sizeof(long);
		}
		ret = 0;
		break;
	default:
		ret = ptrace_request(child, request, addr, data);
	}

	return ret;
}

asmlinkage void syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;
	current->exit_code = SIGTRAP;
	current->state = TASK_STOPPED;
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
