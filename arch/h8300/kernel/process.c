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

#include <asm/uaccess.h>
#include <asm/traps.h>
#include <asm/setup.h>
#include <asm/pgtable.h>

void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);

asmlinkage void ret_from_fork(void);

/*
 * The idle loop on an H8/300..
 */
#if !defined(CONFIG_H8300H_SIM) && !defined(CONFIG_H8S_SIM)
static void default_idle(void)
{
	local_irq_disable();
	if (!need_resched()) {
		local_irq_enable();
		/* XXX: race here! What if need_resched() gets set now? */
		__asm__("sleep");
	} else
		local_irq_enable();
}
#else
static void default_idle(void)
{
	cpu_relax();
}
#endif
void (*idle)(void) = default_idle;

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle(void)
{
	while (1) {
		rcu_idle_enter();
		while (!need_resched())
			idle();
		rcu_idle_exit();
		schedule_preempt_disabled();
	}
}

void machine_restart(char * __unused)
{
	local_irq_disable();
	__asm__("jmp @@0"); 
}

void machine_halt(void)
{
	local_irq_disable();
	__asm__("sleep");
	for (;;);
}

void machine_power_off(void)
{
	local_irq_disable();
	__asm__("sleep");
	for (;;);
}

void show_regs(struct pt_regs * regs)
{
	printk("\nPC: %08lx  Status: %02x",
	       regs->pc, regs->ccr);
	printk("\nORIG_ER0: %08lx ER0: %08lx ER1: %08lx",
	       regs->orig_er0, regs->er0, regs->er1);
	printk("\nER2: %08lx ER3: %08lx ER4: %08lx ER5: %08lx",
	       regs->er2, regs->er3, regs->er4, regs->er5);
	printk("\nER6' %08lx ",regs->er6);
	if (user_mode(regs))
		printk("USP: %08lx\n", rdusp());
	else
		printk("\n");
}

/*
 * Create a kernel thread
 */
int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	long retval;
	long clone_arg;
	mm_segment_t fs;

	fs = get_fs();
	set_fs (KERNEL_DS);
	clone_arg = flags | CLONE_VM;
	__asm__("mov.l sp,er3\n\t"
		"sub.l er2,er2\n\t"
		"mov.l %2,er1\n\t"
		"mov.l %1,er0\n\t"
		"trapa #0\n\t"
		"cmp.l sp,er3\n\t"
		"beq 1f\n\t"
		"mov.l %4,er0\n\t"
		"mov.l %3,er1\n\t"
		"jsr @er1\n\t"
		"mov.l %5,er0\n\t"
		"trapa #0\n"
		"1:\n\t"
		"mov.l er0,%0"
		:"=r"(retval)
		:"i"(__NR_clone),"g"(clone_arg),"g"(fn),"g"(arg),"i"(__NR_exit)
		:"er0","er1","er2","er3");
	set_fs (fs);
	return retval;
}

void flush_thread(void)
{
}

/*
 * "h8300_fork()".. By the time we get here, the
 * non-volatile registers have also been saved on the
 * stack. We do some ugly pointer stuff here.. (see
 * also copy_thread)
 */

asmlinkage int h8300_fork(struct pt_regs *regs)
{
	return -EINVAL;
}

asmlinkage int h8300_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, rdusp(), regs, 0, NULL, NULL);
}

asmlinkage int h8300_clone(struct pt_regs *regs)
{
	unsigned long clone_flags;
	unsigned long newsp;

	/* syscall2 puts clone_flags in er1 and usp in er2 */
	clone_flags = regs->er1;
	newsp = regs->er2;
	if (!newsp)
		newsp  = rdusp();
	return do_fork(clone_flags, newsp, regs, 0, NULL, NULL);

}

int copy_thread(unsigned long clone_flags,
                unsigned long usp, unsigned long topstk,
		 struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;

	childregs = (struct pt_regs *) (THREAD_SIZE + task_stack_page(p)) - 1;

	*childregs = *regs;
	childregs->retpc = (unsigned long) ret_from_fork;
	childregs->er0 = 0;

	p->thread.usp = usp;
	p->thread.ksp = (unsigned long)childregs;

	return 0;
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(const char *name,
			  const char *const *argv,
			  const char *const *envp,
			  int dummy, ...)
{
	int error;
	struct filename *filename;
	struct pt_regs *regs = (struct pt_regs *) ((unsigned char *)&dummy-4);

	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		return error;
	error = do_execve(filename->name, argv, envp, regs);
	putname(filename);
	return error;
}

unsigned long thread_saved_pc(struct task_struct *tsk)
{
	return ((struct pt_regs *)tsk->thread.esp0)->pc;
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
