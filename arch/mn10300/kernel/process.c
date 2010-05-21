/* MN10300  Process handling code
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/percpu.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>
#include <asm/fpu.h>
#include <asm/reset-regs.h>
#include <asm/gdb-stub.h>
#include "internal.h"

/*
 * power management idle function, if any..
 */
void (*pm_idle)(void);
EXPORT_SYMBOL(pm_idle);

/*
 * return saved PC of a blocked thread.
 */
unsigned long thread_saved_pc(struct task_struct *tsk)
{
	return ((unsigned long *) tsk->thread.sp)[3];
}

/*
 * power off function, if any
 */
void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

/*
 * we use this if we don't have any better idle routine
 */
static void default_idle(void)
{
	local_irq_disable();
	if (!need_resched())
		safe_halt();
	else
		local_irq_enable();
}

/*
 * the idle thread
 * - there's no useful work to be done, so just try to conserve power and have
 *   a low exit latency (ie sit in a loop waiting for somebody to say that
 *   they'd like to reschedule)
 */
void cpu_idle(void)
{
	int cpu = smp_processor_id();

	/* endless idle loop with no priority at all */
	for (;;) {
		while (!need_resched()) {
			void (*idle)(void);

			smp_rmb();
			idle = pm_idle;
			if (!idle)
				idle = default_idle;

			irq_stat[cpu].idle_timestamp = jiffies;
			idle();
		}

		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

void release_segments(struct mm_struct *mm)
{
}

void machine_restart(char *cmd)
{
#ifdef CONFIG_GDBSTUB
	gdbstub_exit(0);
#endif

#ifdef mn10300_unit_hard_reset
	mn10300_unit_hard_reset();
#else
	mn10300_proc_hard_reset();
#endif
}

void machine_halt(void)
{
#ifdef CONFIG_GDBSTUB
	gdbstub_exit(0);
#endif
}

void machine_power_off(void)
{
#ifdef CONFIG_GDBSTUB
	gdbstub_exit(0);
#endif
}

void show_regs(struct pt_regs *regs)
{
}

/*
 * create a kernel thread
 */
int kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));

	regs.a2 = (unsigned long) fn;
	regs.d2 = (unsigned long) arg;
	regs.pc = (unsigned long) kernel_thread_helper;
	local_save_flags(regs.epsw);
	regs.epsw |= EPSW_IE | EPSW_IM_7;

	/* Ok, create the new process.. */
	return do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0, &regs, 0,
		       NULL, NULL);
}
EXPORT_SYMBOL(kernel_thread);

/*
 * free current thread data structures etc..
 */
void exit_thread(void)
{
	exit_fpu();
}

void flush_thread(void)
{
	flush_fpu();
}

void release_thread(struct task_struct *dead_task)
{
}

/*
 * we do not have to muck with descriptors here, that is
 * done in switch_mm() as needed.
 */
void copy_segments(struct task_struct *p, struct mm_struct *new_mm)
{
}

/*
 * this gets called before we allocate a new thread and copy the current task
 * into it so that we can store lazy state into memory
 */
void prepare_to_copy(struct task_struct *tsk)
{
	unlazy_fpu(tsk);
}

/*
 * set up the kernel stack for a new thread and copy arch-specific thread
 * control information
 */
int copy_thread(unsigned long clone_flags,
		unsigned long c_usp, unsigned long ustk_size,
		struct task_struct *p, struct pt_regs *kregs)
{
	struct pt_regs *c_uregs, *c_kregs, *uregs;
	unsigned long c_ksp;

	uregs = current->thread.uregs;

	c_ksp = (unsigned long) task_stack_page(p) + THREAD_SIZE;

	/* allocate the userspace exception frame and set it up */
	c_ksp -= sizeof(struct pt_regs);
	c_uregs = (struct pt_regs *) c_ksp;

	p->thread.uregs = c_uregs;
	*c_uregs = *uregs;
	c_uregs->sp = c_usp;
	c_uregs->epsw &= ~EPSW_FE; /* my FPU */

	c_ksp -= 12; /* allocate function call ABI slack */

	/* the new TLS pointer is passed in as arg #5 to sys_clone() */
	if (clone_flags & CLONE_SETTLS)
		c_uregs->e2 = __frame->d3;

	/* set up the return kernel frame if called from kernel_thread() */
	c_kregs = c_uregs;
	if (kregs != uregs) {
		c_ksp -= sizeof(struct pt_regs);
		c_kregs = (struct pt_regs *) c_ksp;
		*c_kregs = *kregs;
		c_kregs->sp = c_usp;
		c_kregs->next = c_uregs;
#ifdef CONFIG_MN10300_CURRENT_IN_E2
		c_kregs->e2 = (unsigned long) p; /* current */
#endif

		c_ksp -= 12; /* allocate function call ABI slack */
	}

	/* set up things up so the scheduler can start the new task */
	p->thread.__frame = c_kregs;
	p->thread.a3	= (unsigned long) c_kregs;
	p->thread.sp	= c_ksp;
	p->thread.pc	= (unsigned long) ret_from_fork;
	p->thread.wchan	= (unsigned long) ret_from_fork;
	p->thread.usp	= c_usp;

	return 0;
}

/*
 * clone a process
 * - tlsptr is retrieved by copy_thread() from __frame->d3
 */
asmlinkage long sys_clone(unsigned long clone_flags, unsigned long newsp,
			  int __user *parent_tidptr, int __user *child_tidptr,
			  int __user *tlsptr)
{
	return do_fork(clone_flags, newsp ?: __frame->sp, __frame, 0,
		       parent_tidptr, child_tidptr);
}

asmlinkage long sys_fork(void)
{
	return do_fork(SIGCHLD, __frame->sp, __frame, 0, NULL, NULL);
}

asmlinkage long sys_vfork(void)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, __frame->sp, __frame,
		       0, NULL, NULL);
}

asmlinkage long sys_execve(char __user *name,
			   char __user * __user *argv,
			   char __user * __user *envp)
{
	char *filename;
	int error;

	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		return error;
	error = do_execve(filename, argv, envp, __frame);
	putname(filename);
	return error;
}

unsigned long get_wchan(struct task_struct *p)
{
	return p->thread.wchan;
}
