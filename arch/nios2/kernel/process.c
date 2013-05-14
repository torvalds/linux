/*
 * Architecture-dependent parts of process handling.
 *
 * Copyright (C) 2013 Altera Corporation
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2009 Wind River Systems Inc
 *   Implemented by fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com
 * Copyright (C) 2004 Microtronix Datacom Ltd
 *
 * based on arch/m68knommu/kernel/process.c which is:
 *
 * Copyright (C) 2000-2002 David McCullough <davidm@snapgear.com>
 * Copyright (C) 1995 Hamish Macdonald
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/export.h>
#include <linux/sched.h>
#include <linux/tick.h>
#include <linux/uaccess.h>

#include <asm/unistd.h>
#include <asm/traps.h>
#include <asm/cacheflush.h>
#include <asm/cpuinfo.h>

asmlinkage void ret_from_fork(void);

void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);

void default_idle(void)
{
	local_irq_disable();
	if (!need_resched()) {
		local_irq_enable();
		__asm__("nop");
	} else
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
	while (1) {
		tick_nohz_idle_enter();
		rcu_idle_enter();
		while (!need_resched())
			idle();
		rcu_idle_exit();
		tick_nohz_idle_exit();

		schedule_preempt_disabled();
	}
}

/*
 * The development boards have no way to pull a board reset. Just jump to the
 * cpu reset address and let the boot loader or the code in head.S take care of
 * resetting peripherals.
 */
void machine_restart(char *__unused)
{
	pr_notice("Machine restart (%08x)...\n", cpuinfo.reset_addr);
	local_irq_disable();
	__asm__ __volatile__ (
	"jmp	%0\n\t"
	:
	: "r" (cpuinfo.reset_addr)
	: "r4");
}

void machine_halt(void)
{
	pr_notice("Machine halt...\n");
	local_irq_disable();
	for (;;)
		;
}

/*
 * There is no way to power off the development boards. So just spin for now. If
 * we ever have a way of resetting a board using a GPIO we should add that here.
 */
void machine_power_off(void)
{
	pr_notice("Machine power off...\n");
	local_irq_disable();
	for (;;)
		;
}

void show_regs(struct pt_regs *regs)
{
	pr_notice("\n");

	pr_notice("r1: %08lx r2: %08lx r3: %08lx r4: %08lx\n",
		regs->r1,  regs->r2,  regs->r3,  regs->r4);

	pr_notice("r5: %08lx r6: %08lx r7: %08lx r8: %08lx\n",
		regs->r5,  regs->r6,  regs->r7,  regs->r8);

	pr_notice("r9: %08lx r10: %08lx r11: %08lx r12: %08lx\n",
		regs->r9,  regs->r10, regs->r11, regs->r12);

	pr_notice("r13: %08lx r14: %08lx r15: %08lx\n",
		regs->r13, regs->r14, regs->r15);

	pr_notice("ra: %08lx fp:  %08lx sp: %08lx gp: %08lx\n",
		regs->ra,  regs->fp,  regs->sp,  regs->gp);

	pr_notice("ea: %08lx estatus: %08lx\n",
		regs->ea,  regs->estatus);
#ifndef CONFIG_MMU
	pr_notice("status_extension: %08lx\n", regs->status_extension);
#endif
}

#ifdef CONFIG_MMU
static void kernel_thread_helper(void *arg, int (*fn)(void *))
{
	do_exit(fn(arg));
}
#endif

/*
 * Create a kernel thread
 */
int kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
#ifdef CONFIG_MMU
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));
	regs.r4 = (unsigned long) arg;
	regs.r5 = (unsigned long) fn;
	regs.ea = (unsigned long) kernel_thread_helper;
	regs.estatus = STATUS_PIE;

	return do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0, &regs, 0,
		NULL, NULL);
#else /* !CONFIG_MMU */
	long retval;
	long clone_arg = flags | CLONE_VM;
	mm_segment_t fs;

	fs = get_fs();
	set_fs(KERNEL_DS);

	__asm__ __volatile(
		"movi    r2,%6\n\t"		/* TRAP_ID_SYSCALL          */
		"movi    r3,%1\n\t"		/* __NR_clone               */
		"mov     r4,%5\n\t"		/* (clone_arg               */
						/*   (flags | CLONE_VM))    */
		"movia   r5,-1\n\t"		/* usp: -1                  */
		"trap\n\t"			/* sys_clone                */
		"\n\t"
		"cmpeq   r4,r3,zero\n\t"	/* 2nd return value in r3   */
		"bne     r4,zero,1f\n\t"	/* 0: parent, just return.  */
						/* See copy_thread, called  */
						/*  by do_fork, called by   */
						/*  nios2_clone, called by  */
						/*  sys_clone, called by    */
						/*  syscall trap handler.   */

		"mov     r4,%4\n\t"		/* fn's parameter (arg)     */
		"\n\t"
		"callr   %3\n\t"		/* Call function (fn)       */
		"\n\t"
		"mov     r4,r2\n\t"		/* fn's rtn code//;dgt2;tmp;*/
		"movi    r2,%6\n\t"		/* TRAP_ID_SYSCALL          */
		"movi    r3,%2\n\t"		/* __NR_exit                */
		"trap\n\t"			/* sys_exit()               */

		/* Not reached by child */
		"1:\n\t"
		"mov     %0,r2\n\t"		/* error rtn code (retval)  */

		:   "=r" (retval)		/* %0                       */

		:   "i" (__NR_clone)		/* %1                       */
		  , "i" (__NR_exit)		/* %2                       */
		  , "r" (fn)			/* %3                       */
		  , "r" (arg)			/* %4                       */
		  , "r" (clone_arg)		/* %5  (flags | CLONE_VM)   */
		  , "i" (TRAP_ID_SYSCALL)	/* %6                       */

		:   "r2", "r3", "r4", "r5", "ra"/* Clobbered                */
	);

	set_fs(fs);
	return retval;
#endif /* CONFIG_MMU */
}
EXPORT_SYMBOL(kernel_thread);

void flush_thread(void)
{
	set_fs(USER_DS);
}

int copy_thread(unsigned long clone_flags,
		unsigned long usp, unsigned long topstk,
		struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs;
	struct switch_stack *childstack, *stack;

	childregs = task_pt_regs(p);

	/* Save pointer to registers in thread_struct */
	p->thread.kregs = childregs;

	/* Copy registers */
	*childregs = *regs;
#ifndef CONFIG_MMU
	childregs->r2 = 0;	/* Redundant? See return values below */
#endif

	/* Copy stacktop and copy the top entrys from parent to child */
	stack = ((struct switch_stack *) regs) - 1;
	childstack = ((struct switch_stack *) childregs) - 1;
	*childstack = *stack;
	childstack->ra = (unsigned long) ret_from_fork;

#ifdef CONFIG_MMU
	if (childregs->estatus & ESTATUS_EU)
		childregs->sp = usp;
	else
		childregs->sp = (unsigned long) childstack;
#else
	if (usp == -1)
		p->thread.kregs->sp = (unsigned long) childstack;
	else
		p->thread.kregs->sp = usp;
#endif /* CONFIG_MMU */

	/* Store the kernel stack in thread_struct */
	p->thread.ksp = (unsigned long) childstack;

#ifdef CONFIG_MMU
	/* Initialize tls register. */
	if (clone_flags & CLONE_SETTLS)
		childstack->r23 = regs->r7;
#endif

	/* Set the return value for the child. */
	childregs->r2 = 0;
#ifdef CONFIG_MMU
	childregs->r7 = 0;
#else
	childregs->r3 = 1;	/* kernel_thread parent test */
#endif

	/* Set the return value for the parent. */
	regs->r2 = p->pid;
#ifdef CONFIG_MMU
	regs->r7 = 0;	/* No error */
#else
	regs->r3 = 0;	/* kernel_thread parent test */
#endif

	return 0;
}

/*
 *	Generic dumping code. Used for panic and debug.
 */
void dump(struct pt_regs *fp)
{
	unsigned long	*sp;
	unsigned char	*tp;
	int		i;

	pr_emerg("\nCURRENT PROCESS:\n\n");
	pr_emerg("COMM=%s PID=%d\n", current->comm, current->pid);

	if (current->mm) {
		pr_emerg("TEXT=%08x-%08x DATA=%08x-%08x BSS=%08x-%08x\n",
			(int) current->mm->start_code,
			(int) current->mm->end_code,
			(int) current->mm->start_data,
			(int) current->mm->end_data,
			(int) current->mm->end_data,
			(int) current->mm->brk);
		pr_emerg("USER-STACK=%08x  KERNEL-STACK=%08x\n\n",
			(int) current->mm->start_stack,
			(int)(((unsigned long) current) + THREAD_SIZE));
	}

	pr_emerg("PC: %08lx\n", fp->ea);
	pr_emerg(KERN_EMERG "SR: %08lx    SP: %08lx\n",
		(long) fp->estatus, (long) fp);

	pr_emerg("r1: %08lx    r2: %08lx    r3: %08lx\n",
		fp->r1, fp->r2, fp->r3);

	pr_emerg("r4: %08lx    r5: %08lx    r6: %08lx    r7: %08lx\n",
		fp->r4, fp->r5, fp->r6, fp->r7);
	pr_emerg("r8: %08lx    r9: %08lx    r10: %08lx    r11: %08lx\n",
		fp->r8, fp->r9, fp->r10, fp->r11);
	pr_emerg("r12: %08lx  r13: %08lx    r14: %08lx    r15: %08lx\n",
		fp->r12, fp->r13, fp->r14, fp->r15);
	pr_emerg("or2: %08lx   ra: %08lx     fp: %08lx    sp: %08lx\n",
		fp->orig_r2, fp->ra, fp->fp, fp->sp);
	pr_emerg("\nUSP: %08x   TRAPFRAME: %08x\n",
		(unsigned int) fp->sp, (unsigned int) fp);

	pr_emerg("\nCODE:");
	tp = ((unsigned char *) fp->ea) - 0x20;
	for (sp = (unsigned long *) tp, i = 0; (i < 0x40);  i += 4) {
		if ((i % 0x10) == 0)
			pr_emerg("\n%08x: ", (int) (tp + i));
		pr_emerg("%08x ", (int) *sp++);
	}
	pr_emerg("\n");

	pr_emerg("\nKERNEL STACK:");
	tp = ((unsigned char *) fp) - 0x40;
	for (sp = (unsigned long *) tp, i = 0; (i < 0xc0); i += 4) {
		if ((i % 0x10) == 0)
			pr_emerg("\n%08x: ", (int) (tp + i));
		pr_emerg("%08x ", (int) *sp++);
	}
	pr_emerg("\n");
	pr_emerg("\n");

	pr_emerg("\nUSER STACK:");
	tp = (unsigned char *) (fp->sp - 0x10);
	for (sp = (unsigned long *) tp, i = 0; (i < 0x80); i += 4) {
		if ((i % 0x10) == 0)
			pr_emerg("\n%08x: ", (int) (tp + i));
		pr_emerg("%08x ", (int) *sp++);
	}
	pr_emerg("\n\n");
}

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long fp, pc;
	unsigned long stack_page;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	stack_page = (unsigned long)p;
	fp = ((struct switch_stack *)p->thread.ksp)->fp;	/* ;dgt2 */
	do {
		if (fp < stack_page+sizeof(struct task_struct) ||
			fp >= 8184+stack_page)	/* ;dgt2;tmp */
			return 0;
		pc = ((unsigned long *)fp)[1];
		if (!in_sched_functions(pc))
			return pc;
		fp = *(unsigned long *) fp;
	} while (count++ < 16);		/* ;dgt2;tmp */
	return 0;
}

/*
 * Do necessary setup to start up a newly executed thread.
 * Will startup in user mode (status_extension = 0).
 */
void start_thread(struct pt_regs *regs, unsigned long pc, unsigned long sp)
{
	memset((void *) regs, 0, sizeof(struct pt_regs));
#ifdef CONFIG_MMU
	regs->estatus = ESTATUS_EPIE | ESTATUS_EU;
#else
	/* No user mode setting on NOMMU, at least for now */
	regs->estatus = ESTATUS_EPIE;
#endif /* CONFIG_MMU */
	regs->ea = pc;
	regs->sp = sp;
}

#ifdef CONFIG_MMU
#include <linux/elfcore.h>

/* Fill in the FPU structure for a core dump. */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *r)
{
	return 0; /* Nios2 has no FPU and thus no FPU registers */
}
#endif /* CONFIG_MMU */
