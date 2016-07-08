/*
 *  linux/arch/cris/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2000-2002  Axis Communications AB
 *
 *  Authors:   Bjorn Wesen (bjornw@axis.com)
 *             Mikael Starvik (starvik@axis.com)
 *
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <arch/svinto.h>
#include <linux/init.h>
#include <arch/system.h>
#include <linux/ptrace.h>

#ifdef CONFIG_ETRAX_GPIO
void etrax_gpio_wake_up_check(void); /* drivers/gpio.c */
#endif

/*
 * We use this if we don't have any better
 * idle routine..
 */
void default_idle(void)
{
#ifdef CONFIG_ETRAX_GPIO
	etrax_gpio_wake_up_check();
#endif
	local_irq_enable();
}

/* if the watchdog is enabled, we can simply disable interrupts and go
 * into an eternal loop, and the watchdog will reset the CPU after 0.1s
 * if on the other hand the watchdog wasn't enabled, we just enable it and wait
 */

void hard_reset_now (void)
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
	/* Since we dont plan to keep on resetting the watchdog,
	   the key can be arbitrary hence three */
	*R_WATCHDOG = IO_FIELD(R_WATCHDOG, key, 3) |
		IO_STATE(R_WATCHDOG, enable, start);
#endif

	while(1) /* waiting for RETRIBUTION! */ ;
}

/*
 * Return saved PC of a blocked thread.
 */
unsigned long thread_saved_pc(struct task_struct *t)
{
	return task_pt_regs(t)->irp;
}

/* setup the child's kernel stack with a pt_regs and switch_stack on it.
 * it will be un-nested during _resume and _ret_from_sys_call when the
 * new thread is scheduled.
 *
 * also setup the thread switching structure which is used to keep
 * thread-specific data during _resumes.
 *
 */
asmlinkage void ret_from_fork(void);
asmlinkage void ret_from_kernel_thread(void);

int copy_thread(unsigned long clone_flags, unsigned long usp,
		unsigned long arg, struct task_struct *p)
{
	struct pt_regs *childregs = task_pt_regs(p);
	struct switch_stack *swstack = ((struct switch_stack *)childregs) - 1;
	
	/* put the pt_regs structure at the end of the new kernel stack page and fix it up
	 * remember that the task_struct doubles as the kernel stack for the task
	 */

	if (unlikely(p->flags & PF_KTHREAD)) {
		memset(swstack, 0,
			sizeof(struct switch_stack) + sizeof(struct pt_regs));
		swstack->r1 = usp;
		swstack->r2 = arg;
		childregs->dccr = 1 << I_DCCR_BITNR;
		swstack->return_ip = (unsigned long) ret_from_kernel_thread;
		p->thread.ksp = (unsigned long) swstack;
		p->thread.usp = 0;
		return 0;
	}
	*childregs = *current_pt_regs();  /* struct copy of pt_regs */

        childregs->r10 = 0;  /* child returns 0 after a fork/clone */

	/* put the switch stack right below the pt_regs */

	swstack->r9 = 0; /* parameter to ret_from_sys_call, 0 == dont restart the syscall */

	/* we want to return into ret_from_sys_call after the _resume */

	swstack->return_ip = (unsigned long) ret_from_fork; /* Will call ret_from_sys_call */
	
	/* fix the user-mode stackpointer */

	p->thread.usp = usp ?: rdusp();

	/* and the kernel-mode one */

	p->thread.ksp = (unsigned long) swstack;

#ifdef DEBUG
	printk("copy_thread: new regs at 0x%p, as shown below:\n", childregs);
	show_registers(childregs);
#endif

	return 0;
}

unsigned long get_wchan(struct task_struct *p)
{
#if 0
	/* YURGH. TODO. */

        unsigned long ebp, esp, eip;
        unsigned long stack_page;
        int count = 0;
        if (!p || p == current || p->state == TASK_RUNNING)
                return 0;
        stack_page = (unsigned long)p;
        esp = p->thread.esp;
        if (!stack_page || esp < stack_page || esp > 8188+stack_page)
                return 0;
        /* include/asm-i386/system.h:switch_to() pushes ebp last. */
        ebp = *(unsigned long *) esp;
        do {
                if (ebp < stack_page || ebp > 8184+stack_page)
                        return 0;
                eip = *(unsigned long *) (ebp+4);
		if (!in_sched_functions(eip))
			return eip;
                ebp = *(unsigned long *) ebp;
        } while (count++ < 16);
#endif
        return 0;
}
#undef last_sched
#undef first_sched

void show_regs(struct pt_regs * regs)
{
	unsigned long usp = rdusp();

	show_regs_print_info(KERN_DEFAULT);

	printk("IRP: %08lx SRP: %08lx DCCR: %08lx USP: %08lx MOF: %08lx\n",
	       regs->irp, regs->srp, regs->dccr, usp, regs->mof );
	printk(" r0: %08lx  r1: %08lx   r2: %08lx  r3: %08lx\n",
	       regs->r0, regs->r1, regs->r2, regs->r3);
	printk(" r4: %08lx  r5: %08lx   r6: %08lx  r7: %08lx\n",
	       regs->r4, regs->r5, regs->r6, regs->r7);
	printk(" r8: %08lx  r9: %08lx  r10: %08lx r11: %08lx\n",
	       regs->r8, regs->r9, regs->r10, regs->r11);
	printk("r12: %08lx r13: %08lx oR10: %08lx\n",
	       regs->r12, regs->r13, regs->orig_r10);
}

