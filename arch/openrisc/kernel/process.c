// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OpenRISC process.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 * This file handles the architecture-dependent parts of process handling...
 */

#define __KERNEL_SYSCALLS__
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/elfcore.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init_task.h>
#include <linux/mqueue.h>
#include <linux/fs.h>
#include <linux/reboot.h>

#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/spr_defs.h>

#include <linux/smp.h>

/*
 * Pointer to Current thread info structure.
 *
 * Used at user space -> kernel transitions.
 */
struct thread_info *current_thread_info_set[NR_CPUS] = { &init_thread_info, };

void machine_restart(char *cmd)
{
	do_kernel_restart(cmd);

	/* Give a grace period for failure to restart of 1s */
	mdelay(1000);

	/* Whoops - the platform was unable to reboot. Tell the user! */
	pr_emerg("Reboot failed -- System halted\n");
	while (1);
}

/*
 * Similar to machine_power_off, but don't shut off power.  Add code
 * here to freeze the system for e.g. post-mortem debug purpose when
 * possible.  This halt has nothing to do with the idle halt.
 */
void machine_halt(void)
{
	printk(KERN_INFO "*** MACHINE HALT ***\n");
	__asm__("l.nop 1");
}

/* If or when software power-off is implemented, add code here.  */
void machine_power_off(void)
{
	printk(KERN_INFO "*** MACHINE POWER OFF ***\n");
	__asm__("l.nop 1");
}

/*
 * Send the doze signal to the cpu if available.
 * Make sure, that all interrupts are enabled
 */
void arch_cpu_idle(void)
{
	raw_local_irq_enable();
	if (mfspr(SPR_UPR) & SPR_UPR_PMP)
		mtspr(SPR_PMR, mfspr(SPR_PMR) | SPR_PMR_DME);
}

void (*pm_power_off) (void) = machine_power_off;
EXPORT_SYMBOL(pm_power_off);

/*
 * When a process does an "exec", machine state like FPU and debug
 * registers need to be reset.  This is a hook function for that.
 * Currently we don't have any such state to reset, so this is empty.
 */
void flush_thread(void)
{
}

void show_regs(struct pt_regs *regs)
{
	extern void show_registers(struct pt_regs *regs);

	show_regs_print_info(KERN_DEFAULT);
	/* __PHX__ cleanup this mess */
	show_registers(regs);
}

void release_thread(struct task_struct *dead_task)
{
}

/*
 * Copy the thread-specific (arch specific) info from the current
 * process to the new one p
 */
extern asmlinkage void ret_from_fork(void);

/*
 * copy_thread
 * @clone_flags: flags
 * @usp: user stack pointer or fn for kernel thread
 * @arg: arg to fn for kernel thread; always NULL for userspace thread
 * @p: the newly created task
 * @tls: the Thread Local Storage pointer for the new process
 *
 * At the top of a newly initialized kernel stack are two stacked pt_reg
 * structures.  The first (topmost) is the userspace context of the thread.
 * The second is the kernelspace context of the thread.
 *
 * A kernel thread will not be returning to userspace, so the topmost pt_regs
 * struct can be uninitialized; it _does_ need to exist, though, because
 * a kernel thread can become a userspace thread by doing a kernel_execve, in
 * which case the topmost context will be initialized and used for 'returning'
 * to userspace.
 *
 * The second pt_reg struct needs to be initialized to 'return' to
 * ret_from_fork.  A kernel thread will need to set r20 to the address of
 * a function to call into (with arg in r22); userspace threads need to set
 * r20 to NULL in which case ret_from_fork will just continue a return to
 * userspace.
 *
 * A kernel thread 'fn' may return; this is effectively what happens when
 * kernel_execve is called.  In that case, the userspace pt_regs must have
 * been initialized (which kernel_execve takes care of, see start_thread
 * below); ret_from_fork will then continue its execution causing the
 * 'kernel thread' to return to userspace as a userspace thread.
 */

int
copy_thread(unsigned long clone_flags, unsigned long usp, unsigned long arg,
	    struct task_struct *p, unsigned long tls)
{
	struct pt_regs *userregs;
	struct pt_regs *kregs;
	unsigned long sp = (unsigned long)task_stack_page(p) + THREAD_SIZE;
	unsigned long top_of_kernel_stack;

	top_of_kernel_stack = sp;

	/* Locate userspace context on stack... */
	sp -= STACK_FRAME_OVERHEAD;	/* redzone */
	sp -= sizeof(struct pt_regs);
	userregs = (struct pt_regs *) sp;

	/* ...and kernel context */
	sp -= STACK_FRAME_OVERHEAD;	/* redzone */
	sp -= sizeof(struct pt_regs);
	kregs = (struct pt_regs *)sp;

	if (unlikely(p->flags & (PF_KTHREAD | PF_IO_WORKER))) {
		memset(kregs, 0, sizeof(struct pt_regs));
		kregs->gpr[20] = usp; /* fn, kernel thread */
		kregs->gpr[22] = arg;
	} else {
		*userregs = *current_pt_regs();

		if (usp)
			userregs->sp = usp;

		/*
		 * For CLONE_SETTLS set "tp" (r10) to the TLS pointer.
		 */
		if (clone_flags & CLONE_SETTLS)
			userregs->gpr[10] = tls;

		userregs->gpr[11] = 0;	/* Result from fork() */

		kregs->gpr[20] = 0;	/* Userspace thread */
	}

	/*
	 * _switch wants the kernel stack page in pt_regs->sp so that it
	 * can restore it to thread_info->ksp... see _switch for details.
	 */
	kregs->sp = top_of_kernel_stack;
	kregs->gpr[9] = (unsigned long)ret_from_fork;

	task_thread_info(p)->ksp = (unsigned long)kregs;

	return 0;
}

/*
 * Set up a thread for executing a new program
 */
void start_thread(struct pt_regs *regs, unsigned long pc, unsigned long sp)
{
	unsigned long sr = mfspr(SPR_SR) & ~SPR_SR_SM;

	memset(regs, 0, sizeof(struct pt_regs));

	regs->pc = pc;
	regs->sr = sr;
	regs->sp = sp;
}

extern struct thread_info *_switch(struct thread_info *old_ti,
				   struct thread_info *new_ti);
extern int lwa_flag;

struct task_struct *__switch_to(struct task_struct *old,
				struct task_struct *new)
{
	struct task_struct *last;
	struct thread_info *new_ti, *old_ti;
	unsigned long flags;

	local_irq_save(flags);

	/* current_set is an array of saved current pointers
	 * (one for each cpu). we need them at user->kernel transition,
	 * while we save them at kernel->user transition
	 */
	new_ti = new->stack;
	old_ti = old->stack;

	lwa_flag = 0;

	current_thread_info_set[smp_processor_id()] = new_ti;
	last = (_switch(old_ti, new_ti))->task;

	local_irq_restore(flags);

	return last;
}

/*
 * Write out registers in core dump format, as defined by the
 * struct user_regs_struct
 */
void dump_elf_thread(elf_greg_t *dest, struct pt_regs* regs)
{
	dest[0] = 0; /* r0 */
	memcpy(dest+1, regs->gpr+1, 31*sizeof(unsigned long));
	dest[32] = regs->pc;
	dest[33] = regs->sr;
	dest[34] = 0;
	dest[35] = 0;
}

unsigned long __get_wchan(struct task_struct *p)
{
	/* TODO */

	return 0;
}
