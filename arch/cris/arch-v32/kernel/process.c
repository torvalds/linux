/*
 *  Copyright (C) 2000-2003  Axis Communications AB
 *
 *  Authors:   Bjorn Wesen (bjornw@axis.com)
 *             Mikael Starvik (starvik@axis.com)
 *             Tobias Anderberg (tobiasa@axis.com), CRISv32 port.
 *
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <hwregs/reg_rdwr.h>
#include <hwregs/reg_map.h>
#include <hwregs/timer_defs.h>
#include <hwregs/intr_vect_defs.h>
#include <linux/ptrace.h>

extern void stop_watchdog(void);

/* We use this if we don't have any better idle routine. */
void default_idle(void)
{
	local_irq_enable();
	/* Halt until exception. */
	__asm__ volatile("halt");
}

/*
 * Free current thread data structures etc..
 */

extern void deconfigure_bp(long pid);
void exit_thread(struct task_struct *tsk)
{
	deconfigure_bp(tsk->pid);
}

/*
 * If the watchdog is enabled, disable interrupts and enter an infinite loop.
 * The watchdog will reset the CPU after 0.1s. If the watchdog isn't enabled
 * then enable it and wait.
 */
extern void arch_enable_nmi(void);

void
hard_reset_now(void)
{
	/*
	 * Don't declare this variable elsewhere.  We don't want any other
	 * code to know about it than the watchdog handler in entry.S and
	 * this code, implementing hard reset through the watchdog.
	 */
#if defined(CONFIG_ETRAX_WATCHDOG)
	extern int cause_of_death;
#endif

	printk("*** HARD RESET ***\n");
	local_irq_disable();

#if defined(CONFIG_ETRAX_WATCHDOG)
	cause_of_death = 0xbedead;
#else
{
	reg_timer_rw_wd_ctrl wd_ctrl = {0};

	stop_watchdog();

	wd_ctrl.key = 16;	/* Arbitrary key. */
	wd_ctrl.cnt = 1;	/* Minimum time. */
	wd_ctrl.cmd = regk_timer_start;

        arch_enable_nmi();
	REG_WR(timer, regi_timer0, rw_wd_ctrl, wd_ctrl);
}
#endif

	while (1)
		; /* Wait for reset. */
}

/*
 * Return saved PC of a blocked thread.
 */
unsigned long thread_saved_pc(struct task_struct *t)
{
	return task_pt_regs(t)->erp;
}

/*
 * Setup the child's kernel stack with a pt_regs and call switch_stack() on it.
 * It will be unnested during _resume and _ret_from_sys_call when the new thread
 * is scheduled.
 *
 * Also setup the thread switching structure which is used to keep
 * thread-specific data during _resumes.
 */

extern asmlinkage void ret_from_fork(void);
extern asmlinkage void ret_from_kernel_thread(void);

int
copy_thread(unsigned long clone_flags, unsigned long usp,
	unsigned long arg, struct task_struct *p)
{
	struct pt_regs *childregs = task_pt_regs(p);
	struct switch_stack *swstack = ((struct switch_stack *) childregs) - 1;

	/*
	 * Put the pt_regs structure at the end of the new kernel stack page and
	 * fix it up. Note: the task_struct doubles as the kernel stack for the
	 * task.
	 */
	if (unlikely(p->flags & PF_KTHREAD)) {
		memset(swstack, 0,
			sizeof(struct switch_stack) + sizeof(struct pt_regs));
		swstack->r1 = usp;
		swstack->r2 = arg;
		childregs->ccs = 1 << (I_CCS_BITNR + CCS_SHIFT);
		swstack->return_ip = (unsigned long) ret_from_kernel_thread;
		p->thread.ksp = (unsigned long) swstack;
		p->thread.usp = 0;
		return 0;
	}
	*childregs = *current_pt_regs();	/* Struct copy of pt_regs. */
        childregs->r10 = 0;	/* Child returns 0 after a fork/clone. */

	/* Set a new TLS ?
	 * The TLS is in $mof because it is the 5th argument to sys_clone.
	 */
	if (p->mm && (clone_flags & CLONE_SETTLS)) {
		task_thread_info(p)->tls = childregs->mof;
	}

	/* Put the switch stack right below the pt_regs. */

	/* Parameter to ret_from_sys_call. 0 is don't restart the syscall. */
	swstack->r9 = 0;

	/*
	 * We want to return into ret_from_sys_call after the _resume.
	 * ret_from_fork will call ret_from_sys_call.
	 */
	swstack->return_ip = (unsigned long) ret_from_fork;

	/* Fix the user-mode and kernel-mode stackpointer. */
	p->thread.usp = usp ?: rdusp();
	p->thread.ksp = (unsigned long) swstack;

	return 0;
}

unsigned long
get_wchan(struct task_struct *p)
{
	/* TODO */
	return 0;
}
#undef last_sched
#undef first_sched

void show_regs(struct pt_regs * regs)
{
	unsigned long usp = rdusp();

	show_regs_print_info(KERN_DEFAULT);

        printk("ERP: %08lx SRP: %08lx  CCS: %08lx USP: %08lx MOF: %08lx\n",
		regs->erp, regs->srp, regs->ccs, usp, regs->mof);

	printk(" r0: %08lx  r1: %08lx   r2: %08lx  r3: %08lx\n",
		regs->r0, regs->r1, regs->r2, regs->r3);

	printk(" r4: %08lx  r5: %08lx   r6: %08lx  r7: %08lx\n",
		regs->r4, regs->r5, regs->r6, regs->r7);

	printk(" r8: %08lx  r9: %08lx  r10: %08lx r11: %08lx\n",
		regs->r8, regs->r9, regs->r10, regs->r11);

	printk("r12: %08lx r13: %08lx oR10: %08lx\n",
		regs->r12, regs->r13, regs->orig_r10);
}
