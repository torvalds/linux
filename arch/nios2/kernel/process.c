/*
 * Architecture-dependent parts of process handling.
 *
 * Copyright (C) 2013 Altera Corporation
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2009 Wind River Systems Inc
 *   Implemented by fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com
 * Copyright (C) 2004 Microtronix Datacom Ltd
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
#include <asm/cpuinfo.h>

asmlinkage void ret_from_fork(void);
asmlinkage void ret_from_kernel_thread(void);

void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);

void arch_cpu_idle(void)
{
	local_irq_enable();
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
	show_regs_print_info(KERN_DEFAULT);

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
}

void flush_thread(void)
{
}

int copy_thread(unsigned long clone_flags,
		unsigned long usp, unsigned long arg, struct task_struct *p)
{
	struct pt_regs *childregs = task_pt_regs(p);
	struct pt_regs *regs;
	struct switch_stack *stack;
	struct switch_stack *childstack =
		((struct switch_stack *)childregs) - 1;

	if (unlikely(p->flags & PF_KTHREAD)) {
		memset(childstack, 0,
			sizeof(struct switch_stack) + sizeof(struct pt_regs));

		childstack->r16 = usp;		/* fn */
		childstack->r17 = arg;
		childstack->ra = (unsigned long) ret_from_kernel_thread;
		childregs->estatus = STATUS_PIE;
		childregs->sp = (unsigned long) childstack;

		p->thread.ksp = (unsigned long) childstack;
		p->thread.kregs = childregs;
		return 0;
	}

	regs = current_pt_regs();
	*childregs = *regs;
	childregs->r2 = 0;	/* Set the return value for the child. */
	childregs->r7 = 0;

	stack = ((struct switch_stack *) regs) - 1;
	*childstack = *stack;
	childstack->ra = (unsigned long)ret_from_fork;
	p->thread.kregs = childregs;
	p->thread.ksp = (unsigned long) childstack;

	if (usp)
		childregs->sp = usp;

	/* Initialize tls register. */
	if (clone_flags & CLONE_SETTLS)
		childstack->r23 = regs->r8;

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
	pr_emerg("SR: %08lx    SP: %08lx\n",
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
	regs->estatus = ESTATUS_EPIE | ESTATUS_EU;
	regs->ea = pc;
	regs->sp = sp;
}

#include <linux/elfcore.h>

/* Fill in the FPU structure for a core dump. */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *r)
{
	return 0; /* Nios2 has no FPU and thus no FPU registers */
}
