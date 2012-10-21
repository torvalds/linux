/* process.c: FRV specific parts of process handling
 *
 * Copyright (C) 2003-5 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from arch/m68k/kernel/process.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
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
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/elf.h>
#include <linux/reboot.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/rcupdate.h>

#include <asm/asm-offsets.h>
#include <asm/uaccess.h>
#include <asm/setup.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>
#include <asm/gdb-stub.h>
#include <asm/mb-regs.h>

#include "local.h"

asmlinkage void ret_from_fork(void);
asmlinkage void ret_from_kernel_thread(void);

#include <asm/pgalloc.h>

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

static void core_sleep_idle(void)
{
#ifdef LED_DEBUG_SLEEP
	/* Show that we're sleeping... */
	__set_LEDS(0x55aa);
#endif
	frv_cpu_core_sleep();
#ifdef LED_DEBUG_SLEEP
	/* ... and that we woke up */
	__set_LEDS(0);
#endif
	mb();
}

void (*idle)(void) = core_sleep_idle;

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle(void)
{
	/* endless idle loop with no priority at all */
	while (1) {
		rcu_idle_enter();
		while (!need_resched()) {
			check_pgt_cache();

			if (!frv_dma_inprogress && idle)
				idle();
		}
		rcu_idle_exit();

		schedule_preempt_disabled();
	}
}

void machine_restart(char * __unused)
{
	unsigned long reset_addr;
#ifdef CONFIG_GDBSTUB
	gdbstub_exit(0);
#endif

	if (PSR_IMPLE(__get_PSR()) == PSR_IMPLE_FR551)
		reset_addr = 0xfefff500;
	else
		reset_addr = 0xfeff0500;

	/* Software reset. */
	asm volatile("      dcef @(gr0,gr0),1 ! membar !"
		     "      sti     %1,@(%0,0) !"
		     "      nop ! nop ! nop ! nop ! nop ! "
		     "      nop ! nop ! nop ! nop ! nop ! "
		     "      nop ! nop ! nop ! nop ! nop ! "
		     "      nop ! nop ! nop ! nop ! nop ! "
		     : : "r" (reset_addr), "r" (1) );

	for (;;)
		;
}

void machine_halt(void)
{
#ifdef CONFIG_GDBSTUB
	gdbstub_exit(0);
#endif

	for (;;);
}

void machine_power_off(void)
{
#ifdef CONFIG_GDBSTUB
	gdbstub_exit(0);
#endif

	for (;;);
}

void flush_thread(void)
{
	/* nothing */
}

inline unsigned long user_stack(const struct pt_regs *regs)
{
	while (regs->next_frame)
		regs = regs->next_frame;
	return user_mode(regs) ? regs->sp : 0;
}

/*
 * set up the kernel stack and exception frames for a new process
 */
int copy_thread(unsigned long clone_flags,
		unsigned long usp, unsigned long arg,
		struct task_struct *p, struct pt_regs *unused)
{
	struct pt_regs *childregs;

	childregs = (struct pt_regs *)
		(task_stack_page(p) + THREAD_SIZE - FRV_FRAME0_SIZE);

	/* set up the userspace frame (the only place that the USP is stored) */
	*childregs = *current_pt_regs();

	p->thread.frame	 = childregs;
	p->thread.curr	 = p;
	p->thread.sp	 = (unsigned long) childregs;
	p->thread.fp	 = 0;
	p->thread.lr	 = 0;
	p->thread.frame0 = childregs;

	if (unlikely(p->flags & PF_KTHREAD)) {
		childregs->gr9 = usp; /* function */
		childregs->gr8 = arg;
		p->thread.pc = (unsigned long) ret_from_kernel_thread;
		save_user_regs(p->thread.user);
		return 0;
	}
	if (usp)
		childregs->sp = usp;
	childregs->next_frame	= NULL;

	p->thread.pc = (unsigned long) ret_from_fork;

	/* the new TLS pointer is passed in as arg #5 to sys_clone() */
	if (clone_flags & CLONE_SETTLS)
		childregs->gr29 = childregs->gr12;

	save_user_regs(p->thread.user);

	return 0;
} /* end copy_thread() */

unsigned long get_wchan(struct task_struct *p)
{
	struct pt_regs *regs0;
	unsigned long fp, pc;
	unsigned long stack_limit;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	stack_limit = (unsigned long) (p + 1);
	fp = p->thread.fp;
	regs0 = p->thread.frame0;

	do {
		if (fp < stack_limit || fp >= (unsigned long) regs0 || fp & 3)
			return 0;

		pc = ((unsigned long *) fp)[2];

		/* FIXME: This depends on the order of these functions. */
		if (!in_sched_functions(pc))
			return pc;

		fp = *(unsigned long *) fp;
	} while (count++ < 16);

	return 0;
}

unsigned long thread_saved_pc(struct task_struct *tsk)
{
	/* Check whether the thread is blocked in resume() */
	if (in_sched_functions(tsk->thread.pc))
		return ((unsigned long *)tsk->thread.fp)[2];
	else
		return tsk->thread.pc;
}

int elf_check_arch(const struct elf32_hdr *hdr)
{
	unsigned long hsr0 = __get_HSR(0);
	unsigned long psr = __get_PSR();

	if (hdr->e_machine != EM_FRV)
		return 0;

	switch (hdr->e_flags & EF_FRV_GPR_MASK) {
	case EF_FRV_GPR64:
		if ((hsr0 & HSR0_GRN) == HSR0_GRN_32)
			return 0;
	case EF_FRV_GPR32:
	case 0:
		break;
	default:
		return 0;
	}

	switch (hdr->e_flags & EF_FRV_FPR_MASK) {
	case EF_FRV_FPR64:
		if ((hsr0 & HSR0_FRN) == HSR0_FRN_32)
			return 0;
	case EF_FRV_FPR32:
	case EF_FRV_FPR_NONE:
	case 0:
		break;
	default:
		return 0;
	}

	if ((hdr->e_flags & EF_FRV_MULADD) == EF_FRV_MULADD)
		if (PSR_IMPLE(psr) != PSR_IMPLE_FR405 &&
		    PSR_IMPLE(psr) != PSR_IMPLE_FR451)
			return 0;

	switch (hdr->e_flags & EF_FRV_CPU_MASK) {
	case EF_FRV_CPU_GENERIC:
		break;
	case EF_FRV_CPU_FR300:
	case EF_FRV_CPU_SIMPLE:
	case EF_FRV_CPU_TOMCAT:
	default:
		return 0;
	case EF_FRV_CPU_FR400:
		if (PSR_IMPLE(psr) != PSR_IMPLE_FR401 &&
		    PSR_IMPLE(psr) != PSR_IMPLE_FR405 &&
		    PSR_IMPLE(psr) != PSR_IMPLE_FR451 &&
		    PSR_IMPLE(psr) != PSR_IMPLE_FR551)
			return 0;
		break;
	case EF_FRV_CPU_FR450:
		if (PSR_IMPLE(psr) != PSR_IMPLE_FR451)
			return 0;
		break;
	case EF_FRV_CPU_FR500:
		if (PSR_IMPLE(psr) != PSR_IMPLE_FR501)
			return 0;
		break;
	case EF_FRV_CPU_FR550:
		if (PSR_IMPLE(psr) != PSR_IMPLE_FR551)
			return 0;
		break;
	}

	return 1;
}

int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpregs)
{
	memcpy(fpregs,
	       &current->thread.user->f,
	       sizeof(current->thread.user->f));
	return 1;
}
