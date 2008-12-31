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
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <arch/svinto.h>
#include <linux/init.h>

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
}

/*
 * Free current thread data structures etc..
 */

void exit_thread(void)
{
	/* Nothing needs to be done.  */
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
#if defined(CONFIG_ETRAX_WATCHDOG) && !defined(CONFIG_SVINTO_SIM)
	extern int cause_of_death;
#endif

	printk("*** HARD RESET ***\n");
	local_irq_disable();

#if defined(CONFIG_ETRAX_WATCHDOG) && !defined(CONFIG_SVINTO_SIM)
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

static void kernel_thread_helper(void* dummy, int (*fn)(void *), void * arg)
{
  fn(arg);
  do_exit(-1); /* Should never be called, return bad exit value */
}

/*
 * Create a kernel thread
 */
int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));

        /* Don't use r10 since that is set to 0 in copy_thread */
	regs.r11 = (unsigned long)fn;
	regs.r12 = (unsigned long)arg;
	regs.irp = (unsigned long)kernel_thread_helper;
	regs.dccr = 1 << I_DCCR_BITNR;

	/* Ok, create the new process.. */
        return do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0, &regs, 0, NULL, NULL);
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

int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
		unsigned long unused,
		struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs * childregs;
	struct switch_stack *swstack;
	
	/* put the pt_regs structure at the end of the new kernel stack page and fix it up
	 * remember that the task_struct doubles as the kernel stack for the task
	 */

	childregs = task_pt_regs(p);
        
	*childregs = *regs;  /* struct copy of pt_regs */
        
        p->set_child_tid = p->clear_child_tid = NULL;

        childregs->r10 = 0;  /* child returns 0 after a fork/clone */
	
	/* put the switch stack right below the pt_regs */

	swstack = ((struct switch_stack *)childregs) - 1;

	swstack->r9 = 0; /* parameter to ret_from_sys_call, 0 == dont restart the syscall */

	/* we want to return into ret_from_sys_call after the _resume */

	swstack->return_ip = (unsigned long) ret_from_fork; /* Will call ret_from_sys_call */
	
	/* fix the user-mode stackpointer */

	p->thread.usp = usp;	

	/* and the kernel-mode one */

	p->thread.ksp = (unsigned long) swstack;

#ifdef DEBUG
	printk("copy_thread: new regs at 0x%p, as shown below:\n", childregs);
	show_registers(childregs);
#endif

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

asmlinkage int sys_fork(long r10, long r11, long r12, long r13, long mof, long srp,
			struct pt_regs *regs)
{
	return do_fork(SIGCHLD, rdusp(), regs, 0, NULL, NULL);
}

/* if newusp is 0, we just grab the old usp */
/* FIXME: Is parent_tid/child_tid really third/fourth argument? Update lib? */
asmlinkage int sys_clone(unsigned long newusp, unsigned long flags,
			 int* parent_tid, int* child_tid, long mof, long srp,
			 struct pt_regs *regs)
{
	if (!newusp)
		newusp = rdusp();
	return do_fork(flags, newusp, regs, 0, parent_tid, child_tid);
}

/* vfork is a system call in i386 because of register-pressure - maybe
 * we can remove it and handle it in libc but we put it here until then.
 */

asmlinkage int sys_vfork(long r10, long r11, long r12, long r13, long mof, long srp,
			 struct pt_regs *regs)
{
        return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, rdusp(), regs, 0, NULL, NULL);
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(const char *fname, char **argv, char **envp,
			  long r13, long mof, long srp, 
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

