/*
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
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/reboot.h>
#include <linux/init_task.h>
#include <linux/mqueue.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/setup.h>
#include <asm/pgtable.h>

/*
 * Initial task/thread structure. Make this a per-architecture thing,
 * because different architectures tend to have different
 * alignment requirements and potentially different initial
 * setup.
 */
static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
union thread_union init_thread_union __init_task_data
	__attribute__((aligned(THREAD_SIZE))) =
		{ INIT_THREAD_INFO(init_task) };

/* initial task structure */
struct task_struct init_task = INIT_TASK(init_task);

EXPORT_SYMBOL(init_task);

asmlinkage void ret_from_fork(void);


/*
 * Return saved PC from a blocked thread
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

/*
 * The idle loop on an m68k..
 */
static void default_idle(void)
{
	if (!need_resched())
#if defined(MACH_ATARI_ONLY)
		/* block out HSYNC on the atari (falcon) */
		__asm__("stop #0x2200" : : : "cc");
#else
		__asm__("stop #0x2000" : : : "cc");
#endif
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
		while (!need_resched())
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

void (*pm_power_off)(void) = machine_power_off;
EXPORT_SYMBOL(pm_power_off);

void show_regs(struct pt_regs * regs)
{
	printk("\n");
	printk("Format %02x  Vector: %04x  PC: %08lx  Status: %04x    %s\n",
	       regs->format, regs->vector, regs->pc, regs->sr, print_tainted());
	printk("ORIG_D0: %08lx  D0: %08lx  A2: %08lx  A1: %08lx\n",
	       regs->orig_d0, regs->d0, regs->a2, regs->a1);
	printk("A0: %08lx  D5: %08lx  D4: %08lx\n",
	       regs->a0, regs->d5, regs->d4);
	printk("D3: %08lx  D2: %08lx  D1: %08lx\n",
	       regs->d3, regs->d2, regs->d1);
	if (!(regs->sr & PS_S))
		printk("USP: %08lx\n", rdusp());
}

/*
 * Create a kernel thread
 */
int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	int pid;
	mm_segment_t fs;

	fs = get_fs();
	set_fs (KERNEL_DS);

	{
	register long retval __asm__ ("d0");
	register long clone_arg __asm__ ("d1") = flags | CLONE_VM | CLONE_UNTRACED;

	retval = __NR_clone;
	__asm__ __volatile__
	  ("clrl %%d2\n\t"
	   "trap #0\n\t"		/* Linux/m68k system call */
	   "tstl %0\n\t"		/* child or parent */
	   "jne 1f\n\t"			/* parent - jump */
	   "lea %%sp@(%c7),%6\n\t"	/* reload current */
	   "movel %6@,%6\n\t"
	   "movel %3,%%sp@-\n\t"	/* push argument */
	   "jsr %4@\n\t"		/* call fn */
	   "movel %0,%%d1\n\t"		/* pass exit value */
	   "movel %2,%%d0\n\t"		/* exit */
	   "trap #0\n"
	   "1:"
	   : "+d" (retval)
	   : "i" (__NR_clone), "i" (__NR_exit),
	     "r" (arg), "a" (fn), "d" (clone_arg), "r" (current),
	     "i" (-THREAD_SIZE)
	   : "d2");

	pid = retval;
	}

	set_fs (fs);
	return pid;
}
EXPORT_SYMBOL(kernel_thread);

void flush_thread(void)
{
	unsigned long zero = 0;

	current->thread.fs = __USER_DS;
	if (!FPU_IS_EMU)
		asm volatile (".chip 68k/68881\n\t"
			      "frestore %0\n\t"
			      ".chip 68k" : : "m" (zero));
}

/*
 * "m68k_fork()".. By the time we get here, the
 * non-volatile registers have also been saved on the
 * stack. We do some ugly pointer stuff here.. (see
 * also copy_thread)
 */

asmlinkage int m68k_fork(struct pt_regs *regs)
{
	return do_fork(SIGCHLD, rdusp(), regs, 0, NULL, NULL);
}

asmlinkage int m68k_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, rdusp(), regs, 0,
		       NULL, NULL);
}

asmlinkage int m68k_clone(struct pt_regs *regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	int __user *parent_tidptr, *child_tidptr;

	/* syscall2 puts clone_flags in d1 and usp in d2 */
	clone_flags = regs->d1;
	newsp = regs->d2;
	parent_tidptr = (int __user *)regs->d3;
	child_tidptr = (int __user *)regs->d4;
	if (!newsp)
		newsp = rdusp();
	return do_fork(clone_flags, newsp, regs, 0,
		       parent_tidptr, child_tidptr);
}

int copy_thread(unsigned long clone_flags, unsigned long usp,
		 unsigned long unused,
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

	if (!FPU_IS_EMU) {
		/* Copy the current fpu state */
		asm volatile ("fsave %0" : : "m" (p->thread.fpstate[0]) : "memory");

		if (!CPU_IS_060 ? p->thread.fpstate[0] : p->thread.fpstate[2])
		  asm volatile ("fmovemx %/fp0-%/fp7,%0\n\t"
				"fmoveml %/fpiar/%/fpcr/%/fpsr,%1"
				: : "m" (p->thread.fp[0]), "m" (p->thread.fpcntl[0])
				: "memory");
		/* Restore the state in case the fpu was busy */
		asm volatile ("frestore %0" : : "m" (p->thread.fpstate[0]));
	}

	return 0;
}

/* Fill in the fpu structure for a core dump.  */

int dump_fpu (struct pt_regs *regs, struct user_m68kfp_struct *fpu)
{
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
	if (!CPU_IS_060 ? !fpustate[0] : !fpustate[2])
		return 0;

	asm volatile ("fmovem %/fpiar/%/fpcr/%/fpsr,%0"
		:: "m" (fpu->fpcntl[0])
		: "memory");
	asm volatile ("fmovemx %/fp0-%/fp7,%0"
		:: "m" (fpu->fpregs[0])
		: "memory");
	return 1;
}
EXPORT_SYMBOL(dump_fpu);

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(const char __user *name,
			  const char __user *const __user *argv,
			  const char __user *const __user *envp)
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

	stack_page = (unsigned long)task_stack_page(p);
	fp = ((struct switch_stack *)p->thread.ksp)->a6;
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
