/*
 *  Copyright (C) 2000-2003  Axis Communications AB
 *
 *  Authors:   Bjorn Wesen (bjornw@axis.com)
 *             Mikael Starvik (starvik@axis.com)
 *             Tobias Anderberg (tobiasa@axis.com), CRISv32 port.
 *
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/arch/hwregs/reg_rdwr.h>
#include <asm/arch/hwregs/reg_map.h>
#include <asm/arch/hwregs/timer_defs.h>
#include <asm/arch/hwregs/intr_vect_defs.h>

extern void stop_watchdog(void);

#ifdef CONFIG_ETRAX_GPIO
extern void etrax_gpio_wake_up_check(void); /* Defined in drivers/gpio.c. */
#endif

extern int cris_hlt_counter;

/* We use this if we don't have any better idle routine. */
void default_idle(void)
{
	local_irq_disable();
	if (!need_resched() && !cris_hlt_counter) {
	        /* Halt until exception. */
		__asm__ volatile("ei    \n\t"
                                 "halt      ");
	}
	local_irq_enable();
}

/*
 * Free current thread data structures etc..
 */

extern void deconfigure_bp(long pid);
void exit_thread(void)
{
	deconfigure_bp(current->pid);
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
	REG_WR(timer, regi_timer, rw_wd_ctrl, wd_ctrl);
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
	return (unsigned long)user_regs(t->thread_info)->erp;
}

static void
kernel_thread_helper(void* dummy, int (*fn)(void *), void * arg)
{
	fn(arg);
	do_exit(-1); /* Should never be called, return bad exit value. */
}

/* Create a kernel thread. */
int
kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));

        /* Don't use r10 since that is set to 0 in copy_thread. */
	regs.r11 = (unsigned long) fn;
	regs.r12 = (unsigned long) arg;
	regs.erp = (unsigned long) kernel_thread_helper;
	regs.ccs = 1 << (I_CCS_BITNR + CCS_SHIFT);

	/* Create the new process. */
        return do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0, &regs, 0, NULL, NULL);
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

int
copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	unsigned long unused,
	struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs;
	struct switch_stack *swstack;

	/*
	 * Put the pt_regs structure at the end of the new kernel stack page and
	 * fix it up. Note: the task_struct doubles as the kernel stack for the
	 * task.
	 */
	childregs = user_regs(p->thread_info);
	*childregs = *regs;	/* Struct copy of pt_regs. */
        p->set_child_tid = p->clear_child_tid = NULL;
        childregs->r10 = 0;	/* Child returns 0 after a fork/clone. */

	/* Set a new TLS ?
	 * The TLS is in $mof beacuse it is the 5th argument to sys_clone.
	 */
	if (p->mm && (clone_flags & CLONE_SETTLS)) {
		p->thread_info->tls = regs->mof;
	}

	/* Put the switch stack right below the pt_regs. */
	swstack = ((struct switch_stack *) childregs) - 1;

	/* Paramater to ret_from_sys_call. 0 is don't restart the syscall. */
	swstack->r9 = 0;

	/*
	 * We want to return into ret_from_sys_call after the _resume.
	 * ret_from_fork will call ret_from_sys_call.
	 */
	swstack->return_ip = (unsigned long) ret_from_fork;

	/* Fix the user-mode and kernel-mode stackpointer. */
	p->thread.usp = usp;
	p->thread.ksp = (unsigned long) swstack;

	return 0;
}

/*
 * Be aware of the "magic" 7th argument in the four system-calls below.
 * They need the latest stackframe, which is put as the 7th argument by
 * entry.S. The previous arguments are dummies or actually used, but need
 * to be defined to reach the 7th argument.
 *
 * N.B.: Another method to get the stackframe is to use current_regs(). But
 * it returns the latest stack-frame stacked when going from _user mode_ and
 * some of these (at least sys_clone) are called from kernel-mode sometimes
 * (for example during kernel_thread, above) and thus cannot use it. Thus,
 * to be sure not to get any surprises, we use the method for the other calls
 * as well.
 */
asmlinkage int
sys_fork(long r10, long r11, long r12, long r13, long mof, long srp,
	struct pt_regs *regs)
{
	return do_fork(SIGCHLD, rdusp(), regs, 0, NULL, NULL);
}

/* FIXME: Is parent_tid/child_tid really third/fourth argument? Update lib? */
asmlinkage int
sys_clone(unsigned long newusp, unsigned long flags, int *parent_tid, int *child_tid,
	unsigned long tls, long srp, struct pt_regs *regs)
{
	if (!newusp)
		newusp = rdusp();

	return do_fork(flags, newusp, regs, 0, parent_tid, child_tid);
}

/*
 * vfork is a system call in i386 because of register-pressure - maybe
 * we can remove it and handle it in libc but we put it here until then.
 */
asmlinkage int
sys_vfork(long r10, long r11, long r12, long r13, long mof, long srp,
	struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, rdusp(), regs, 0, NULL, NULL);
}

/* sys_execve() executes a new program. */
asmlinkage int
sys_execve(const char *fname, char **argv, char **envp, long r13, long mof, long srp,
	struct pt_regs *regs)
{
	int error;
	char *filename;

	filename = getname(fname);
	error = PTR_ERR(filename);

	if (IS_ERR(filename))
	        goto out;

	error = do_execve(filename, argv, envp, regs);
	putname(filename);
 out:
	return error;
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
