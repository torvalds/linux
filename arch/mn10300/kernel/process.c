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
#include <linux/rcupdate.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>
#include <asm/fpu.h>
#include <asm/reset-regs.h>
#include <asm/gdb-stub.h>
#include "internal.h"

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
 * On SMP it's slightly faster (but much more power-consuming!)
 * to poll the ->work.need_resched flag instead of waiting for the
 * cross-CPU IPI to arrive. Use this option with caution.
 *
 * tglx: No idea why this depends on HOTPLUG_CPU !?!
 */
#if !defined(CONFIG_SMP) || defined(CONFIG_HOTPLUG_CPU)
void arch_cpu_idle(void)
{
	safe_halt();
}
#endif

void release_segments(struct mm_struct *mm)
{
}

void machine_restart(char *cmd)
{
#ifdef CONFIG_KERNEL_DEBUGGER
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
#ifdef CONFIG_KERNEL_DEBUGGER
	gdbstub_exit(0);
#endif
}

void machine_power_off(void)
{
#ifdef CONFIG_KERNEL_DEBUGGER
	gdbstub_exit(0);
#endif
}

void show_regs(struct pt_regs *regs)
{
	show_regs_print_info(KERN_DEFAULT);
}

/*
 * free current thread data structures etc..
 */
void exit_thread(void)
{
	exit_fpu(current);
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
 * this gets called so that we can store lazy state into memory and copy the
 * current task into the new thread.
 */
int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src)
{
	unlazy_fpu(src);
	*dst = *src;
	return 0;
}

/*
 * set up the kernel stack for a new thread and copy arch-specific thread
 * control information
 */
int copy_thread(unsigned long clone_flags,
		unsigned long c_usp, unsigned long ustk_size,
		struct task_struct *p)
{
	struct thread_info *ti = task_thread_info(p);
	struct pt_regs *c_regs;
	unsigned long c_ksp;

	c_ksp = (unsigned long) task_stack_page(p) + THREAD_SIZE;

	/* allocate the userspace exception frame and set it up */
	c_ksp -= sizeof(struct pt_regs);
	c_regs = (struct pt_regs *) c_ksp;
	c_ksp -= 12; /* allocate function call ABI slack */

	/* set up things up so the scheduler can start the new task */
	p->thread.uregs = c_regs;
	ti->frame	= c_regs;
	p->thread.a3	= (unsigned long) c_regs;
	p->thread.sp	= c_ksp;
	p->thread.wchan	= p->thread.pc;
	p->thread.usp	= c_usp;

	if (unlikely(p->flags & PF_KTHREAD)) {
		memset(c_regs, 0, sizeof(struct pt_regs));
		c_regs->a0 = c_usp; /* function */
		c_regs->d0 = ustk_size; /* argument */
		local_save_flags(c_regs->epsw);
		c_regs->epsw |= EPSW_IE | EPSW_IM_7;
		p->thread.pc	= (unsigned long) ret_from_kernel_thread;
		return 0;
	}
	*c_regs = *current_pt_regs();
	if (c_usp)
		c_regs->sp = c_usp;
	c_regs->epsw &= ~EPSW_FE; /* my FPU */

	/* the new TLS pointer is passed in as arg #5 to sys_clone() */
	if (clone_flags & CLONE_SETTLS)
		c_regs->e2 = current_frame()->d3;

	p->thread.pc	= (unsigned long) ret_from_fork;

	return 0;
}

unsigned long get_wchan(struct task_struct *p)
{
	return p->thread.wchan;
}
