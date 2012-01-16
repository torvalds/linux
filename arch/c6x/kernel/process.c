/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2006, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/init_task.h>
#include <linux/tick.h>
#include <linux/mqueue.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>

#include <asm/syscalls.h>

/* hooks for board specific support */
void	(*c6x_restart)(void);
void	(*c6x_halt)(void);

extern asmlinkage void ret_from_fork(void);

static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);

/*
 * Initial thread structure.
 */
union thread_union init_thread_union __init_task_data =	{
	INIT_THREAD_INFO(init_task)
};

/*
 * Initial task structure.
 */
struct task_struct init_task = INIT_TASK(init_task);
EXPORT_SYMBOL(init_task);

/*
 * power off function, if any
 */
void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

static void c6x_idle(void)
{
	unsigned long tmp;

	/*
	 * Put local_irq_enable and idle in same execute packet
	 * to make them atomic and avoid race to idle with
	 * interrupts enabled.
	 */
	asm volatile ("   mvc .s2 CSR,%0\n"
		      "   or  .d2 1,%0,%0\n"
		      "   mvc .s2 %0,CSR\n"
		      "|| idle\n"
		      : "=b"(tmp));
}

/*
 * The idle loop for C64x
 */
void cpu_idle(void)
{
	/* endless idle loop with no priority at all */
	while (1) {
		tick_nohz_idle_enter();
		rcu_idle_enter();
		while (1) {
			local_irq_disable();
			if (need_resched()) {
				local_irq_enable();
				break;
			}
			c6x_idle(); /* enables local irqs */
		}
		rcu_idle_exit();
		tick_nohz_idle_exit();

		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

static void halt_loop(void)
{
	printk(KERN_EMERG "System Halted, OK to turn off power\n");
	local_irq_disable();
	while (1)
		asm volatile("idle\n");
}

void machine_restart(char *__unused)
{
	if (c6x_restart)
		c6x_restart();
	halt_loop();
}

void machine_halt(void)
{
	if (c6x_halt)
		c6x_halt();
	halt_loop();
}

void machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();
	halt_loop();
}

static void kernel_thread_helper(int dummy, void *arg, int (*fn)(void *))
{
	do_exit(fn(arg));
}

/*
 * Create a kernel thread
 */
int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	struct pt_regs regs;

	/*
	 * copy_thread sets a4 to zero (child return from fork)
	 * so we can't just set things up to directly return to
	 * fn.
	 */
	memset(&regs, 0, sizeof(regs));
	regs.b4 = (unsigned long) arg;
	regs.a6 = (unsigned long) fn;
	regs.pc = (unsigned long) kernel_thread_helper;
	local_save_flags(regs.csr);
	regs.csr |= 1;
	regs.tsr = 5; /* Set GEE and GIE in TSR */

	/* Ok, create the new process.. */
	return do_fork(flags | CLONE_VM | CLONE_UNTRACED, -1, &regs,
		       0, NULL, NULL);
}
EXPORT_SYMBOL(kernel_thread);

void flush_thread(void)
{
}

void exit_thread(void)
{
}

SYSCALL_DEFINE1(c6x_clone, struct pt_regs *, regs)
{
	unsigned long clone_flags;
	unsigned long newsp;

	/* syscall puts clone_flags in A4 and usp in B4 */
	clone_flags = regs->orig_a4;
	if (regs->b4)
		newsp = regs->b4;
	else
		newsp = regs->sp;

	return do_fork(clone_flags, newsp, regs, 0, (int __user *)regs->a6,
		       (int __user *)regs->b6);
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
void start_thread(struct pt_regs *regs, unsigned int pc, unsigned long usp)
{
	/*
	 * The binfmt loader will setup a "full" stack, but the C6X
	 * operates an "empty" stack. So we adjust the usp so that
	 * argc doesn't get destroyed if an interrupt is taken before
	 * it is read from the stack.
	 *
	 * NB: Library startup code needs to match this.
	 */
	usp -= 8;

	set_fs(USER_DS);
	regs->pc  = pc;
	regs->sp  = usp;
	regs->tsr |= 0x40; /* set user mode */
	current->thread.usp = usp;
}

/*
 * Copy a new thread context in its stack.
 */
int copy_thread(unsigned long clone_flags, unsigned long usp,
		unsigned long ustk_size,
		struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs;

	childregs = task_pt_regs(p);

	*childregs = *regs;
	childregs->a4 = 0;

	if (usp == -1)
		/* case of  __kernel_thread: we return to supervisor space */
		childregs->sp = (unsigned long)(childregs + 1);
	else
		/* Otherwise use the given stack */
		childregs->sp = usp;

	/* Set usp/ksp */
	p->thread.usp = childregs->sp;
	/* switch_to uses stack to save/restore 14 callee-saved regs */
	thread_saved_ksp(p) = (unsigned long)childregs - 8;
	p->thread.pc = (unsigned int) ret_from_fork;
	p->thread.wchan	= (unsigned long) ret_from_fork;
#ifdef __DSBT__
	{
		unsigned long dp;

		asm volatile ("mv .S2 b14,%0\n" : "=b"(dp));

		thread_saved_dp(p) = dp;
		if (usp == -1)
			childregs->dp = dp;
	}
#endif
	return 0;
}

/*
 * c6x_execve() executes a new program.
 */
SYSCALL_DEFINE4(c6x_execve, const char __user *, name,
		const char __user *const __user *, argv,
		const char __user *const __user *, envp,
		struct pt_regs *, regs)
{
	int error;
	char *filename;

	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;

	error = do_execve(filename, argv, envp, regs);
	putname(filename);
out:
	return error;
}

unsigned long get_wchan(struct task_struct *p)
{
	return p->thread.wchan;
}
