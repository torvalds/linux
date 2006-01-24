/*
 *  linux/arch/ppc64/kernel/ptrace-common.h
 *
 *    Copyright (c) 2002 Stephen Rothwell, IBM Coproration
 *    Extracted from ptrace.c and ptrace32.c
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
 * this archive for more details.
 */

#ifndef _PPC64_PTRACE_COMMON_H
#define _PPC64_PTRACE_COMMON_H

#include <linux/config.h>
#include <asm/system.h>

/*
 * Set of msr bits that gdb can change on behalf of a process.
 */
#define MSR_DEBUGCHANGE	(MSR_FE0 | MSR_SE | MSR_BE | MSR_FE1)

/*
 * Get contents of register REGNO in task TASK.
 */
static inline unsigned long get_reg(struct task_struct *task, int regno)
{
	unsigned long tmp = 0;

	/*
	 * Put the correct FP bits in, they might be wrong as a result
	 * of our lazy FP restore.
	 */
	if (regno == PT_MSR) {
		tmp = ((unsigned long *)task->thread.regs)[PT_MSR];
		tmp |= task->thread.fpexc_mode;
	} else if (regno < (sizeof(struct pt_regs) / sizeof(unsigned long))) {
		tmp = ((unsigned long *)task->thread.regs)[regno];
	}

	return tmp;
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int put_reg(struct task_struct *task, int regno,
			  unsigned long data)
{
	if (regno < PT_SOFTE) {
		if (regno == PT_MSR)
			data = (data & MSR_DEBUGCHANGE)
				| (task->thread.regs->msr & ~MSR_DEBUGCHANGE);
		((unsigned long *)task->thread.regs)[regno] = data;
		return 0;
	}
	return -EIO;
}

static inline void set_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;
	if (regs != NULL)
		regs->msr |= MSR_SE;
	set_tsk_thread_flag(task, TIF_SINGLESTEP);
}

static inline void clear_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;
	if (regs != NULL)
		regs->msr &= ~MSR_SE;
	clear_tsk_thread_flag(task, TIF_SINGLESTEP);
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
static inline int get_vrregs(unsigned long __user *data,
			     struct task_struct *task)
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
static inline int set_vrregs(struct task_struct *task,
			     unsigned long __user *data)
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
#endif

static inline int ptrace_set_debugreg(struct task_struct *task,
				      unsigned long addr, unsigned long data)
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

#endif /* _PPC64_PTRACE_COMMON_H */
