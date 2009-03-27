/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/tick.h>
#include <linux/bitops.h>
#include <asm/system.h>
#include <asm/pgalloc.h>

void show_regs(struct pt_regs *regs)
{
	printk(KERN_INFO " Registers dump: mode=%X\r\n", regs->kernel_mode);
	printk(KERN_INFO " r1=%08lX, r2=%08lX, r3=%08lX, r4=%08lX\n",
				regs->r1, regs->r2, regs->r3, regs->r4);
	printk(KERN_INFO " r5=%08lX, r6=%08lX, r7=%08lX, r8=%08lX\n",
				regs->r5, regs->r6, regs->r7, regs->r8);
	printk(KERN_INFO " r9=%08lX, r10=%08lX, r11=%08lX, r12=%08lX\n",
				regs->r9, regs->r10, regs->r11, regs->r12);
	printk(KERN_INFO " r13=%08lX, r14=%08lX, r15=%08lX, r16=%08lX\n",
				regs->r13, regs->r14, regs->r15, regs->r16);
	printk(KERN_INFO " r17=%08lX, r18=%08lX, r19=%08lX, r20=%08lX\n",
				regs->r17, regs->r18, regs->r19, regs->r20);
	printk(KERN_INFO " r21=%08lX, r22=%08lX, r23=%08lX, r24=%08lX\n",
				regs->r21, regs->r22, regs->r23, regs->r24);
	printk(KERN_INFO " r25=%08lX, r26=%08lX, r27=%08lX, r28=%08lX\n",
				regs->r25, regs->r26, regs->r27, regs->r28);
	printk(KERN_INFO " r29=%08lX, r30=%08lX, r31=%08lX, rPC=%08lX\n",
				regs->r29, regs->r30, regs->r31, regs->pc);
	printk(KERN_INFO " msr=%08lX, ear=%08lX, esr=%08lX, fsr=%08lX\n",
				regs->msr, regs->ear, regs->esr, regs->fsr);
	while (1)
		;
}

void (*pm_idle)(void);
void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);

static int hlt_counter = 1;

void disable_hlt(void)
{
	hlt_counter++;
}
EXPORT_SYMBOL(disable_hlt);

void enable_hlt(void)
{
	hlt_counter--;
}
EXPORT_SYMBOL(enable_hlt);

static int __init nohlt_setup(char *__unused)
{
	hlt_counter = 1;
	return 1;
}
__setup("nohlt", nohlt_setup);

static int __init hlt_setup(char *__unused)
{
	hlt_counter = 0;
	return 1;
}
__setup("hlt", hlt_setup);

void default_idle(void)
{
	if (!hlt_counter) {
		clear_thread_flag(TIF_POLLING_NRFLAG);
		smp_mb__after_clear_bit();
		local_irq_disable();
		while (!need_resched())
			cpu_sleep();
		local_irq_enable();
		set_thread_flag(TIF_POLLING_NRFLAG);
	} else
		while (!need_resched())
			cpu_relax();
}

void cpu_idle(void)
{
	set_thread_flag(TIF_POLLING_NRFLAG);

	/* endless idle loop with no priority at all */
	while (1) {
		void (*idle)(void) = pm_idle;

		if (!idle)
			idle = default_idle;

		tick_nohz_stop_sched_tick(1);
		while (!need_resched())
			idle();
		tick_nohz_restart_sched_tick();

		preempt_enable_no_resched();
		schedule();
		preempt_disable();
		check_pgt_cache();
	}
}

void flush_thread(void)
{
}

/* FIXME - here will be a proposed change -> remove nr parameter */
int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
		unsigned long unused,
		struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs = task_pt_regs(p);
	struct thread_info *ti = task_thread_info(p);

	*childregs = *regs;
	if (user_mode(regs))
		childregs->r1 = usp;
	else
		childregs->r1 = ((unsigned long) ti) + THREAD_SIZE;

	memset(&ti->cpu_context, 0, sizeof(struct cpu_context));
	ti->cpu_context.r1 = (unsigned long)childregs;
	ti->cpu_context.msr = (unsigned long)childregs->msr;
	ti->cpu_context.r15 = (unsigned long)ret_from_fork - 8;

	if (clone_flags & CLONE_SETTLS)
		;

	return 0;
}

/*
 * Return saved PC of a blocked thread.
 */
unsigned long thread_saved_pc(struct task_struct *tsk)
{
	struct cpu_context *ctx =
		&(((struct thread_info *)(tsk->stack))->cpu_context);

	/* Check whether the thread is blocked in resume() */
	if (in_sched_functions(ctx->r15))
		return (unsigned long)ctx->r15;
	else
		return ctx->r14;
}

static void kernel_thread_helper(int (*fn)(void *), void *arg)
{
	fn(arg);
	do_exit(-1);
}

int kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	struct pt_regs regs;
	int ret;

	memset(&regs, 0, sizeof(regs));
	/* store them in non-volatile registers */
	regs.r5 = (unsigned long)fn;
	regs.r6 = (unsigned long)arg;
	local_save_flags(regs.msr);
	regs.pc = (unsigned long)kernel_thread_helper;
	regs.kernel_mode = 1;

	ret = do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0,
			&regs, 0, NULL, NULL);

	return ret;
}

unsigned long get_wchan(struct task_struct *p)
{
/* TBD (used by procfs) */
	return 0;
}
