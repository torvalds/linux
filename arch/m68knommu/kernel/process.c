/*
 *  linux/arch/m68knommu/kernel/process.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 *
 *  68060 fixes by Jesper Skov
 *
 *  uClinux changes
 *  Copyright (C) 2000-2002, David McCullough <davidm@snapgear.com>
 */

/*
 * This file handles the architecture-dependent parts of process handling..
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
#include <linux/reboot.h>
#include <linux/fs.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/setup.h>
#include <asm/pgtable.h>

asmlinkage void ret_from_fork(void);

/*
 * The following aren't currently used.
 */
void (*pm_idle)(void);
EXPORT_SYMBOL(pm_idle);

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

/*
 * The idle loop on an m68knommu..
 */
static void default_idle(void)
{
	local_irq_disable();
 	while (!need_resched()) {
		/* This stop will re-enable interrupts */
 		__asm__("stop #0x2000" : : : "cc");
		local_irq_disable();
	}
	local_irq_enable();
}

void (*idle)(void) = default_idle;

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
		idle();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

void machine_restart(char * __unused)
{
	if (mach_reset)
		mach_reset();
	for (;;);
}

void machine_halt(void)
{
	if (mach_halt)
		mach_halt();
	for (;;);
}

void machine_power_off(void)
{
	if (mach_power_off)
		mach_power_off();
	for (;;);
}

void show_regs(struct pt_regs * regs)
{
	printk(KERN_NOTICE "\n");
	printk(KERN_NOTICE "Format %02x  Vector: %04x  PC: %08lx  Status: %04x    %s\n",
	       regs->format, regs->vector, regs->pc, regs->sr, print_tainted());
	printk(KERN_NOTICE "ORIG_D0: %08lx  D0: %08lx  A2: %08lx  A1: %08lx\n",
	       regs->orig_d0, regs->d0, regs->a2, regs->a1);
	printk(KERN_NOTICE "A0: %08lx  D5: %08lx  D4: %08lx\n",
	       regs->a0, regs->d5, regs->d4);
	printk(KERN_NOTICE "D3: %08lx  D2: %08lx  D1: %08lx\n",
	       regs->d3, regs->d2, regs->d1);
	if (!(regs->sr & PS_S))
		printk(KERN_NOTICE "USP: %08lx\n", rdusp());
}

/*
 * Create a kernel thread
 */
int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	int retval;
	long clone_arg = flags | CLONE_VM;
	mm_segment_t fs;

	fs = get_fs();
	set_fs(KERNEL_DS);

	__asm__ __volatile__ (
			"movel	%%sp, %%d2\n\t"
			"movel	%5, %%d1\n\t"
			"movel	%1, %%d0\n\t"
			"trap	#0\n\t"
			"cmpl	%%sp, %%d2\n\t"
			"jeq	1f\n\t"
			"movel	%3, %%sp@-\n\t"
			"jsr	%4@\n\t"
			"movel	%2, %%d0\n\t"
			"trap	#0\n"
			"1:\n\t"
			"movel	%%d0, %0\n"
		: "=d" (retval)
		: "i" (__NR_clone),
		  "i" (__NR_exit),
		  "a" (arg),
		  "a" (fn),
		  "a" (clone_arg)
		: "cc", "%d0", "%d1", "%d2");

	set_fs(fs);
	return retval;
}

void flush_thread(void)
{
#ifdef CONFIG_FPU
	unsigned long zero = 0;
#endif
	set_fs(USER_DS);
	current->thread.fs = __USER_DS;
#ifdef CONFIG_FPU
	if (!FPU_IS_EMU)
		asm volatile (".chip 68k/68881\n\t"
			      "frestore %0@\n\t"
			      ".chip 68k" : : "a" (&zero));
#endif
}

/*
 * "m68k_fork()".. By the time we get here, the
 * non-volatile registers have also been saved on the
 * stack. We do some ugly pointer stuff here.. (see
 * also copy_thread)
 */

asmlinkage int m68k_fork(struct pt_regs *regs)
{
	/* fork almost works, enough to trick you into looking elsewhere :-( */
	return(-EINVAL);
}

asmlinkage int m68k_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, rdusp(), regs, 0, NULL, NULL);
}

asmlinkage int m68k_clone(struct pt_regs *regs)
{
	unsigned long clone_flags;
	unsigned long newsp;

	/* syscall2 puts clone_flags in d1 and usp in d2 */
	clone_flags = regs->d1;
	newsp = regs->d2;
	if (!newsp)
		newsp = rdusp();
        return do_fork(clone_flags, newsp, regs, 0, NULL, NULL);
}

int copy_thread(unsigned long clone_flags,
		unsigned long usp, unsigned long topstk,
		struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	struct switch_stack * childstack, *stack;
	unsigned long *retp;

	childregs = (struct pt_regs *) (task_stack_page(p) + THREAD_SIZE) - 1;

	*childregs = *regs;
	childregs->d0 = 0;

	retp = ((unsigned long *) regs);
	stack = ((struct switch_stack *) retp) - 1;

	childstack = ((struct switch_stack *) childregs) - 1;
	*childstack = *stack;
	childstack->retpc = (unsigned long)ret_from_fork;

	p->thread.usp = usp;
	p->thread.ksp = (unsigned long)childstack;

	if (clone_flags & CLONE_SETTLS)
		task_thread_info(p)->tp_value = regs->d5;

	/*
	 * Must save the current SFC/DFC value, NOT the value when
	 * the parent was last descheduled - RGH  10-08-96
	 */
	p->thread.fs = get_fs().seg;

#ifdef CONFIG_FPU
	if (!FPU_IS_EMU) {
		/* Copy the current fpu state */
		asm volatile ("fsave %0" : : "m" (p->thread.fpstate[0]) : "memory");

		if (p->thread.fpstate[0])
		  asm volatile ("fmovemx %/fp0-%/fp7,%0\n\t"
				"fmoveml %/fpiar/%/fpcr/%/fpsr,%1"
				: : "m" (p->thread.fp[0]), "m" (p->thread.fpcntl[0])
				: "memory");
		/* Restore the state in case the fpu was busy */
		asm volatile ("frestore %0" : : "m" (p->thread.fpstate[0]));
	}
#endif

	return 0;
}

/* Fill in the fpu structure for a core dump.  */

int dump_fpu(struct pt_regs *regs, struct user_m68kfp_struct *fpu)
{
#ifdef CONFIG_FPU
	char fpustate[216];

	if (FPU_IS_EMU) {
		int i;

		memcpy(fpu->fpcntl, current->thread.fpcntl, 12);
		memcpy(fpu->fpregs, current->thread.fp, 96);
		/* Convert internal fpu reg representation
		 * into long double format
		 */
		for (i = 0; i < 24; i += 3)
			fpu->fpregs[i] = ((fpu->fpregs[i] & 0xffff0000) << 15) |
			                 ((fpu->fpregs[i] & 0x0000ffff) << 16);
		return 1;
	}

	/* First dump the fpu context to avoid protocol violation.  */
	asm volatile ("fsave %0" :: "m" (fpustate[0]) : "memory");
	if (!fpustate[0])
		return 0;

	asm volatile ("fmovem %/fpiar/%/fpcr/%/fpsr,%0"
		:: "m" (fpu->fpcntl[0])
		: "memory");
	asm volatile ("fmovemx %/fp0-%/fp7,%0"
		:: "m" (fpu->fpregs[0])
		: "memory");
#endif
	return 1;
}

/*
 *	Generic dumping code. Used for panic and debug.
 */
void dump(struct pt_regs *fp)
{
	unsigned long	*sp;
	unsigned char	*tp;
	int		i;

	printk(KERN_EMERG "\nCURRENT PROCESS:\n\n");
	printk(KERN_EMERG "COMM=%s PID=%d\n", current->comm, current->pid);

	if (current->mm) {
		printk(KERN_EMERG "TEXT=%08x-%08x DATA=%08x-%08x BSS=%08x-%08x\n",
			(int) current->mm->start_code,
			(int) current->mm->end_code,
			(int) current->mm->start_data,
			(int) current->mm->end_data,
			(int) current->mm->end_data,
			(int) current->mm->brk);
		printk(KERN_EMERG "USER-STACK=%08x KERNEL-STACK=%08x\n\n",
			(int) current->mm->start_stack,
			(int)(((unsigned long) current) + THREAD_SIZE));
	}

	printk(KERN_EMERG "PC: %08lx\n", fp->pc);
	printk(KERN_EMERG "SR: %08lx    SP: %08lx\n", (long) fp->sr, (long) fp);
	printk(KERN_EMERG "d0: %08lx    d1: %08lx    d2: %08lx    d3: %08lx\n",
		fp->d0, fp->d1, fp->d2, fp->d3);
	printk(KERN_EMERG "d4: %08lx    d5: %08lx    a0: %08lx    a1: %08lx\n",
		fp->d4, fp->d5, fp->a0, fp->a1);
	printk(KERN_EMERG "\nUSP: %08x   TRAPFRAME: %p\n",
		(unsigned int) rdusp(), fp);

	printk(KERN_EMERG "\nCODE:");
	tp = ((unsigned char *) fp->pc) - 0x20;
	for (sp = (unsigned long *) tp, i = 0; (i < 0x40);  i += 4) {
		if ((i % 0x10) == 0)
			printk(KERN_EMERG "%p: ", tp + i);
		printk("%08x ", (int) *sp++);
	}
	printk(KERN_EMERG "\n");

	printk(KERN_EMERG "KERNEL STACK:");
	tp = ((unsigned char *) fp) - 0x40;
	for (sp = (unsigned long *) tp, i = 0; (i < 0xc0); i += 4) {
		if ((i % 0x10) == 0)
			printk(KERN_EMERG "%p: ", tp + i);
		printk("%08x ", (int) *sp++);
	}
	printk(KERN_EMERG "\n");

	printk(KERN_EMERG "USER STACK:");
	tp = (unsigned char *) (rdusp() - 0x10);
	for (sp = (unsigned long *) tp, i = 0; (i < 0x80); i += 4) {
		if ((i % 0x10) == 0)
			printk(KERN_EMERG "%p: ", tp + i);
		printk("%08x ", (int) *sp++);
	}
	printk(KERN_EMERG "\n");
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(const char *name,
			  const char *const *argv,
			  const char *const *envp)
{
	int error;
	char * filename;
	struct pt_regs *regs = (struct pt_regs *) &name;

	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		return error;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
	return error;
}

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long fp, pc;
	unsigned long stack_page;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	stack_page = (unsigned long)p;
	fp = ((struct switch_stack *)p->thread.ksp)->a6;
	do {
		if (fp < stack_page+sizeof(struct thread_info) ||
		    fp >= THREAD_SIZE-8+stack_page)
			return 0;
		pc = ((unsigned long *)fp)[1];
		if (!in_sched_functions(pc))
			return pc;
		fp = *(unsigned long *) fp;
	} while (count++ < 16);
	return 0;
}

/*
 * Return saved PC of a blocked thread.
 */
unsigned long thread_saved_pc(struct task_struct *tsk)
{
	struct switch_stack *sw = (struct switch_stack *)tsk->thread.ksp;

	/* Check whether the thread is blocked in resume() */
	if (in_sched_functions(sw->retpc))
		return ((unsigned long *)sw->a6)[1];
	else
		return sw->retpc;
}

