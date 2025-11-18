/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/cpu.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/pm.h>
#include <linux/tick.h>
#include <linux/bitops.h>
#include <linux/ptrace.h>
#include <asm/cacheflush.h>

void show_regs(struct pt_regs *regs)
{
	show_regs_print_info(KERN_INFO);

	pr_info(" Registers dump: mode=%X\r\n", regs->pt_mode);
	pr_info(" r1=%08lX, r2=%08lX, r3=%08lX, r4=%08lX\n",
				regs->r1, regs->r2, regs->r3, regs->r4);
	pr_info(" r5=%08lX, r6=%08lX, r7=%08lX, r8=%08lX\n",
				regs->r5, regs->r6, regs->r7, regs->r8);
	pr_info(" r9=%08lX, r10=%08lX, r11=%08lX, r12=%08lX\n",
				regs->r9, regs->r10, regs->r11, regs->r12);
	pr_info(" r13=%08lX, r14=%08lX, r15=%08lX, r16=%08lX\n",
				regs->r13, regs->r14, regs->r15, regs->r16);
	pr_info(" r17=%08lX, r18=%08lX, r19=%08lX, r20=%08lX\n",
				regs->r17, regs->r18, regs->r19, regs->r20);
	pr_info(" r21=%08lX, r22=%08lX, r23=%08lX, r24=%08lX\n",
				regs->r21, regs->r22, regs->r23, regs->r24);
	pr_info(" r25=%08lX, r26=%08lX, r27=%08lX, r28=%08lX\n",
				regs->r25, regs->r26, regs->r27, regs->r28);
	pr_info(" r29=%08lX, r30=%08lX, r31=%08lX, rPC=%08lX\n",
				regs->r29, regs->r30, regs->r31, regs->pc);
	pr_info(" msr=%08lX, ear=%08lX, esr=%08lX, fsr=%08lX\n",
				regs->msr, regs->ear, regs->esr, regs->fsr);
}

void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);

void flush_thread(void)
{
}

int copy_thread(struct task_struct *p, const struct kernel_clone_args *args)
{
	u64 clone_flags = args->flags;
	unsigned long usp = args->stack;
	unsigned long tls = args->tls;
	struct pt_regs *childregs = task_pt_regs(p);
	struct thread_info *ti = task_thread_info(p);

	if (unlikely(args->fn)) {
		/* if we're creating a new kernel thread then just zeroing all
		 * the registers. That's OK for a brand new thread.*/
		memset(childregs, 0, sizeof(struct pt_regs));
		memset(&ti->cpu_context, 0, sizeof(struct cpu_context));
		ti->cpu_context.r1  = (unsigned long)childregs;
		ti->cpu_context.r20 = (unsigned long)args->fn;
		ti->cpu_context.r19 = (unsigned long)args->fn_arg;
		childregs->pt_mode = 1;
		local_save_flags(childregs->msr);
		ti->cpu_context.msr = childregs->msr & ~MSR_IE;
		ti->cpu_context.r15 = (unsigned long)ret_from_kernel_thread - 8;
		return 0;
	}
	*childregs = *current_pt_regs();
	if (usp)
		childregs->r1 = usp;

	memset(&ti->cpu_context, 0, sizeof(struct cpu_context));
	ti->cpu_context.r1 = (unsigned long)childregs;
	childregs->msr |= MSR_UMS;

	/* we should consider the fact that childregs is a copy of the parent
	 * regs which were saved immediately after entering the kernel state
	 * before enabling VM. This MSR will be restored in switch_to and
	 * RETURN() and we want to have the right machine state there
	 * specifically this state must have INTs disabled before and enabled
	 * after performing rtbd
	 * compose the right MSR for RETURN(). It will work for switch_to also
	 * excepting for VM and UMS
	 * don't touch UMS , CARRY and cache bits
	 * right now MSR is a copy of parent one */
	childregs->msr &= ~MSR_EIP;
	childregs->msr |= MSR_IE;
	childregs->msr &= ~MSR_VM;
	childregs->msr |= MSR_VMS;
	childregs->msr |= MSR_EE; /* exceptions will be enabled*/

	ti->cpu_context.msr = (childregs->msr|MSR_VM);
	ti->cpu_context.msr &= ~MSR_UMS; /* switch_to to kernel mode */
	ti->cpu_context.msr &= ~MSR_IE;
	ti->cpu_context.r15 = (unsigned long)ret_from_fork - 8;

	/*
	 *  r21 is the thread reg, r10 is 6th arg to clone
	 *  which contains TLS area
	 */
	if (clone_flags & CLONE_SETTLS)
		childregs->r21 = tls;

	return 0;
}

unsigned long __get_wchan(struct task_struct *p)
{
/* TBD (used by procfs) */
	return 0;
}

/* Set up a thread for executing a new program */
void start_thread(struct pt_regs *regs, unsigned long pc, unsigned long usp)
{
	regs->pc = pc;
	regs->r1 = usp;
	regs->pt_mode = 0;
	regs->msr |= MSR_UMS;
	regs->msr &= ~MSR_VM;
}

#include <linux/elfcore.h>
/*
 * Set up a thread for executing a new program
 */
int elf_core_copy_task_fpregs(struct task_struct *t, elf_fpregset_t *fpu)
{
	return 0; /* MicroBlaze has no separate FPU registers */
}

void arch_cpu_idle(void)
{
}
