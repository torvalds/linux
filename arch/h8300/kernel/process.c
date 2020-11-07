// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/h8300/kernel/process.c
 *
 * Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  Based on:
 *
 *  linux/arch/m68knommu/kernel/process.c
 *
 *  Copyright (C) 1998  D. Jeff Dionne <jeff@ryeham.ee.ryerson.ca>,
 *                      Kenneth Albanowski <kjahds@kjahds.com>,
 *                      The Silver Hammer Group, Ltd.
 *
 *  linux/arch/m68k/kernel/process.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 *
 *  68060 fixes by Jesper Skov
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>

#include <linux/uaccess.h>
#include <asm/traps.h>
#include <asm/setup.h>

void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);

asmlinkage void ret_from_fork(void);
asmlinkage void ret_from_kernel_thread(void);

/*
 * The idle loop on an H8/300..
 */
void arch_cpu_idle(void)
{
	local_irq_enable();
	__asm__("sleep");
}

void machine_restart(char *__unused)
{
	local_irq_disable();
	__asm__("jmp @@0");
}

void machine_halt(void)
{
	local_irq_disable();
	__asm__("sleep");
	for (;;)
		;
}

void machine_power_off(void)
{
	local_irq_disable();
	__asm__("sleep");
	for (;;)
		;
}

void show_regs(struct pt_regs *regs)
{
	show_regs_print_info(KERN_DEFAULT);

	pr_notice("\n");
	pr_notice("PC: %08lx  Status: %02x\n",
	       regs->pc, regs->ccr);
	pr_notice("ORIG_ER0: %08lx ER0: %08lx ER1: %08lx\n",
	       regs->orig_er0, regs->er0, regs->er1);
	pr_notice("ER2: %08lx ER3: %08lx ER4: %08lx ER5: %08lx\n",
	       regs->er2, regs->er3, regs->er4, regs->er5);
	pr_notice("ER6' %08lx ", regs->er6);
	if (user_mode(regs))
		printk("USP: %08lx\n", rdusp());
	else
		printk("\n");
}

void flush_thread(void)
{
}

int copy_thread(unsigned long clone_flags, unsigned long usp,
		unsigned long topstk, struct task_struct *p, unsigned long tls)
{
	struct pt_regs *childregs;

	childregs = (struct pt_regs *) (THREAD_SIZE + task_stack_page(p)) - 1;

	if (unlikely(p->flags & PF_KTHREAD)) {
		memset(childregs, 0, sizeof(struct pt_regs));
		childregs->retpc = (unsigned long) ret_from_kernel_thread;
		childregs->er4 = topstk; /* arg */
		childregs->er5 = usp; /* fn */
	}  else {
		*childregs = *current_pt_regs();
		childregs->er0 = 0;
		childregs->retpc = (unsigned long) ret_from_fork;
		p->thread.usp = usp ?: rdusp();
	}
	p->thread.ksp = (unsigned long)childregs;

	return 0;
}

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long fp, pc;
	unsigned long stack_page;
	int count = 0;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	stack_page = (unsigned long)p;
	fp = ((struct pt_regs *)p->thread.ksp)->er6;
	do {
		if (fp < stack_page+sizeof(struct thread_info) ||
		    fp >= 8184+stack_page)
			return 0;
		pc = ((unsigned long *)fp)[1];
		if (!in_sched_functions(pc))
			return pc;
		fp = *(unsigned long *) fp;
	} while (count++ < 16);
	return 0;
}

/* generic sys_clone is not enough registers */
asmlinkage int sys_clone(unsigned long __user *args)
{
	unsigned long clone_flags;
	unsigned long  newsp;
	uintptr_t parent_tidptr;
	uintptr_t child_tidptr;
	struct kernel_clone_args kargs = {};

	get_user(clone_flags, &args[0]);
	get_user(newsp, &args[1]);
	get_user(parent_tidptr, &args[2]);
	get_user(child_tidptr, &args[3]);

	kargs.flags		= (lower_32_bits(clone_flags) & ~CSIGNAL);
	kargs.pidfd		= (int __user *)parent_tidptr;
	kargs.child_tid		= (int __user *)child_tidptr;
	kargs.parent_tid	= (int __user *)parent_tidptr;
	kargs.exit_signal	= (lower_32_bits(clone_flags) & CSIGNAL);
	kargs.stack		= newsp;

	return kernel_clone(&kargs);
}
