// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/tick.h>
#include <linux/ptrace.h>
#include <linux/uaccess.h>

#include <asm/unistd.h>
#include <asm/processor.h>
#include <asm/csr.h>
#include <asm/string.h>
#include <asm/switch_to.h>

extern asmlinkage void ret_from_fork(void);
extern asmlinkage void ret_from_kernel_thread(void);

void arch_cpu_idle(void)
{
	wait_for_interrupt();
	local_irq_enable();
}

void show_regs(struct pt_regs *regs)
{
	show_regs_print_info(KERN_DEFAULT);

	pr_cont("sepc: " REG_FMT " ra : " REG_FMT " sp : " REG_FMT "\n",
		regs->sepc, regs->ra, regs->sp);
	pr_cont(" gp : " REG_FMT " tp : " REG_FMT " t0 : " REG_FMT "\n",
		regs->gp, regs->tp, regs->t0);
	pr_cont(" t1 : " REG_FMT " t2 : " REG_FMT " s0 : " REG_FMT "\n",
		regs->t1, regs->t2, regs->s0);
	pr_cont(" s1 : " REG_FMT " a0 : " REG_FMT " a1 : " REG_FMT "\n",
		regs->s1, regs->a0, regs->a1);
	pr_cont(" a2 : " REG_FMT " a3 : " REG_FMT " a4 : " REG_FMT "\n",
		regs->a2, regs->a3, regs->a4);
	pr_cont(" a5 : " REG_FMT " a6 : " REG_FMT " a7 : " REG_FMT "\n",
		regs->a5, regs->a6, regs->a7);
	pr_cont(" s2 : " REG_FMT " s3 : " REG_FMT " s4 : " REG_FMT "\n",
		regs->s2, regs->s3, regs->s4);
	pr_cont(" s5 : " REG_FMT " s6 : " REG_FMT " s7 : " REG_FMT "\n",
		regs->s5, regs->s6, regs->s7);
	pr_cont(" s8 : " REG_FMT " s9 : " REG_FMT " s10: " REG_FMT "\n",
		regs->s8, regs->s9, regs->s10);
	pr_cont(" s11: " REG_FMT " t3 : " REG_FMT " t4 : " REG_FMT "\n",
		regs->s11, regs->t3, regs->t4);
	pr_cont(" t5 : " REG_FMT " t6 : " REG_FMT "\n",
		regs->t5, regs->t6);

	pr_cont("sstatus: " REG_FMT " sbadaddr: " REG_FMT " scause: " REG_FMT "\n",
		regs->sstatus, regs->sbadaddr, regs->scause);
}

void start_thread(struct pt_regs *regs, unsigned long pc,
	unsigned long sp)
{
	regs->sstatus = SR_SPIE;
	if (has_fpu) {
		regs->sstatus |= SR_FS_INITIAL;
		/*
		 * Restore the initial value to the FP register
		 * before starting the user program.
		 */
		fstate_restore(current, regs);
	}
	regs->sepc = pc;
	regs->sp = sp;
	set_fs(USER_DS);
}

void flush_thread(void)
{
#ifdef CONFIG_FPU
	/*
	 * Reset FPU state and context
	 *	frm: round to nearest, ties to even (IEEE default)
	 *	fflags: accrued exceptions cleared
	 */
	fstate_off(current, task_pt_regs(current));
	memset(&current->thread.fstate, 0, sizeof(current->thread.fstate));
#endif
}

int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src)
{
	fstate_save(src, task_pt_regs(src));
	*dst = *src;
	return 0;
}

int copy_thread(unsigned long clone_flags, unsigned long usp,
	unsigned long arg, struct task_struct *p)
{
	struct pt_regs *childregs = task_pt_regs(p);

	/* p->thread holds context to be restored by __switch_to() */
	if (unlikely(p->flags & PF_KTHREAD)) {
		/* Kernel thread */
		const register unsigned long gp __asm__ ("gp");
		memset(childregs, 0, sizeof(struct pt_regs));
		childregs->gp = gp;
		childregs->sstatus = SR_SPP | SR_SPIE; /* Supervisor, irqs on */

		p->thread.ra = (unsigned long)ret_from_kernel_thread;
		p->thread.s[0] = usp; /* fn */
		p->thread.s[1] = arg;
	} else {
		*childregs = *(current_pt_regs());
		if (usp) /* User fork */
			childregs->sp = usp;
		if (clone_flags & CLONE_SETTLS)
			childregs->tp = childregs->a5;
		childregs->a0 = 0; /* Return value of fork() */
		p->thread.ra = (unsigned long)ret_from_fork;
	}
	p->thread.sp = (unsigned long)childregs; /* kernel sp */
	return 0;
}
