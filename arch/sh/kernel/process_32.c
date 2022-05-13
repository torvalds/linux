// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/kernel/process.c
 *
 * This file handles the architecture-dependent parts of process handling..
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  SuperH version:  Copyright (C) 1999, 2000  Niibe Yutaka & Kaz Kojima
 *		     Copyright (C) 2006 Lineo Solutions Inc. support SH4A UBC
 *		     Copyright (C) 2002 - 2008  Paul Mundt
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <linux/elfcore.h>
#include <linux/fs.h>
#include <linux/ftrace.h>
#include <linux/hw_breakpoint.h>
#include <linux/prefetch.h>
#include <linux/stackprotector.h>
#include <linux/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/fpu.h>
#include <asm/syscalls.h>
#include <asm/switch_to.h>

void show_regs(struct pt_regs * regs)
{
	pr_info("\n");
	show_regs_print_info(KERN_DEFAULT);

	pr_info("PC is at %pS\n", (void *)instruction_pointer(regs));
	pr_info("PR is at %pS\n", (void *)regs->pr);

	pr_info("PC  : %08lx SP  : %08lx SR  : %08lx ", regs->pc,
		regs->regs[15], regs->sr);
#ifdef CONFIG_MMU
	pr_cont("TEA : %08x\n", __raw_readl(MMU_TEA));
#else
	pr_cont("\n");
#endif

	pr_info("R0  : %08lx R1  : %08lx R2  : %08lx R3  : %08lx\n",
		regs->regs[0], regs->regs[1], regs->regs[2], regs->regs[3]);
	pr_info("R4  : %08lx R5  : %08lx R6  : %08lx R7  : %08lx\n",
		regs->regs[4], regs->regs[5], regs->regs[6], regs->regs[7]);
	pr_info("R8  : %08lx R9  : %08lx R10 : %08lx R11 : %08lx\n",
		regs->regs[8], regs->regs[9], regs->regs[10], regs->regs[11]);
	pr_info("R12 : %08lx R13 : %08lx R14 : %08lx\n",
		regs->regs[12], regs->regs[13], regs->regs[14]);
	pr_info("MACH: %08lx MACL: %08lx GBR : %08lx PR  : %08lx\n",
		regs->mach, regs->macl, regs->gbr, regs->pr);

	show_trace(NULL, (unsigned long *)regs->regs[15], regs, KERN_DEFAULT);
	show_code(regs);
}

void start_thread(struct pt_regs *regs, unsigned long new_pc,
		  unsigned long new_sp)
{
	regs->pr = 0;
	regs->sr = SR_FD;
	regs->pc = new_pc;
	regs->regs[15] = new_sp;

	free_thread_xstate(current);
}
EXPORT_SYMBOL(start_thread);

void flush_thread(void)
{
	struct task_struct *tsk = current;

	flush_ptrace_hw_breakpoint(tsk);

#if defined(CONFIG_SH_FPU)
	/* Forget lazy FPU state */
	clear_fpu(tsk, task_pt_regs(tsk));
	clear_used_math();
#endif
}

void release_thread(struct task_struct *dead_task)
{
	/* do nothing */
}

asmlinkage void ret_from_fork(void);
asmlinkage void ret_from_kernel_thread(void);

int copy_thread(unsigned long clone_flags, unsigned long usp, unsigned long arg,
		struct task_struct *p, unsigned long tls)
{
	struct thread_info *ti = task_thread_info(p);
	struct pt_regs *childregs;

#if defined(CONFIG_SH_DSP)
	struct task_struct *tsk = current;

	if (is_dsp_enabled(tsk)) {
		/* We can use the __save_dsp or just copy the struct:
		 * __save_dsp(p);
		 * p->thread.dsp_status.status |= SR_DSP
		 */
		p->thread.dsp_status = tsk->thread.dsp_status;
	}
#endif

	memset(p->thread.ptrace_bps, 0, sizeof(p->thread.ptrace_bps));

	childregs = task_pt_regs(p);
	p->thread.sp = (unsigned long) childregs;
	if (unlikely(p->flags & (PF_KTHREAD | PF_IO_WORKER))) {
		memset(childregs, 0, sizeof(struct pt_regs));
		p->thread.pc = (unsigned long) ret_from_kernel_thread;
		childregs->regs[4] = arg;
		childregs->regs[5] = usp;
		childregs->sr = SR_MD;
#if defined(CONFIG_SH_FPU)
		childregs->sr |= SR_FD;
#endif
		ti->status &= ~TS_USEDFPU;
		p->thread.fpu_counter = 0;
		return 0;
	}
	*childregs = *current_pt_regs();

	if (usp)
		childregs->regs[15] = usp;

	if (clone_flags & CLONE_SETTLS)
		childregs->gbr = tls;

	childregs->regs[0] = 0; /* Set return value for child */
	p->thread.pc = (unsigned long) ret_from_fork;
	return 0;
}

/*
 *	switch_to(x,y) should switch tasks from x to y.
 *
 */
__notrace_funcgraph struct task_struct *
__switch_to(struct task_struct *prev, struct task_struct *next)
{
	struct thread_struct *next_t = &next->thread;

#if defined(CONFIG_STACKPROTECTOR) && !defined(CONFIG_SMP)
	__stack_chk_guard = next->stack_canary;
#endif

	unlazy_fpu(prev, task_pt_regs(prev));

	/* we're going to use this soon, after a few expensive things */
	if (next->thread.fpu_counter > 5)
		prefetch(next_t->xstate);

#ifdef CONFIG_MMU
	/*
	 * Restore the kernel mode register
	 *	k7 (r7_bank1)
	 */
	asm volatile("ldc	%0, r7_bank"
		     : /* no output */
		     : "r" (task_thread_info(next)));
#endif

	/*
	 * If the task has used fpu the last 5 timeslices, just do a full
	 * restore of the math state immediately to avoid the trap; the
	 * chances of needing FPU soon are obviously high now
	 */
	if (next->thread.fpu_counter > 5)
		__fpu_state_restore();

	return prev;
}

unsigned long __get_wchan(struct task_struct *p)
{
	unsigned long pc;

	/*
	 * The same comment as on the Alpha applies here, too ...
	 */
	pc = thread_saved_pc(p);

#ifdef CONFIG_FRAME_POINTER
	if (in_sched_functions(pc)) {
		unsigned long schedule_frame = (unsigned long)p->thread.sp;
		return ((unsigned long *)schedule_frame)[21];
	}
#endif

	return pc;
}
