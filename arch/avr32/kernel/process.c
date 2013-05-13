/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/fs.h>
#include <linux/pm.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/reboot.h>
#include <linux/tick.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>

#include <asm/sysreg.h>
#include <asm/ocd.h>
#include <asm/syscalls.h>

#include <mach/pm.h>

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

/*
 * This file handles the architecture-dependent parts of process handling..
 */

void arch_cpu_idle(void)
{
	cpu_enter_idle();
}

void machine_halt(void)
{
	/*
	 * Enter Stop mode. The 32 kHz oscillator will keep running so
	 * the RTC will keep the time properly and the system will
	 * boot quickly.
	 */
	asm volatile("sleep 3\n\t"
		     "sub pc, -2");
}

void machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();
}

void machine_restart(char *cmd)
{
	ocd_write(DC, (1 << OCD_DC_DBE_BIT));
	ocd_write(DC, (1 << OCD_DC_RES_BIT));
	while (1) ;
}

/*
 * Free current thread data structures etc
 */
void exit_thread(void)
{
	ocd_disable(current);
}

void flush_thread(void)
{
	/* nothing to do */
}

void release_thread(struct task_struct *dead_task)
{
	/* do nothing */
}

static void dump_mem(const char *str, const char *log_lvl,
		     unsigned long bottom, unsigned long top)
{
	unsigned long p;
	int i;

	printk("%s%s(0x%08lx to 0x%08lx)\n", log_lvl, str, bottom, top);

	for (p = bottom & ~31; p < top; ) {
		printk("%s%04lx: ", log_lvl, p & 0xffff);

		for (i = 0; i < 8; i++, p += 4) {
			unsigned int val;

			if (p < bottom || p >= top)
				printk("         ");
			else {
				if (__get_user(val, (unsigned int __user *)p)) {
					printk("\n");
					goto out;
				}
				printk("%08x ", val);
			}
		}
		printk("\n");
	}

out:
	return;
}

static inline int valid_stack_ptr(struct thread_info *tinfo, unsigned long p)
{
	return (p > (unsigned long)tinfo)
		&& (p < (unsigned long)tinfo + THREAD_SIZE - 3);
}

#ifdef CONFIG_FRAME_POINTER
static void show_trace_log_lvl(struct task_struct *tsk, unsigned long *sp,
			       struct pt_regs *regs, const char *log_lvl)
{
	unsigned long lr, fp;
	struct thread_info *tinfo;

	if (regs)
		fp = regs->r7;
	else if (tsk == current)
		asm("mov %0, r7" : "=r"(fp));
	else
		fp = tsk->thread.cpu_context.r7;

	/*
	 * Walk the stack as long as the frame pointer (a) is within
	 * the kernel stack of the task, and (b) it doesn't move
	 * downwards.
	 */
	tinfo = task_thread_info(tsk);
	printk("%sCall trace:\n", log_lvl);
	while (valid_stack_ptr(tinfo, fp)) {
		unsigned long new_fp;

		lr = *(unsigned long *)fp;
#ifdef CONFIG_KALLSYMS
		printk("%s [<%08lx>] ", log_lvl, lr);
#else
		printk(" [<%08lx>] ", lr);
#endif
		print_symbol("%s\n", lr);

		new_fp = *(unsigned long *)(fp + 4);
		if (new_fp <= fp)
			break;
		fp = new_fp;
	}
	printk("\n");
}
#else
static void show_trace_log_lvl(struct task_struct *tsk, unsigned long *sp,
			       struct pt_regs *regs, const char *log_lvl)
{
	unsigned long addr;

	printk("%sCall trace:\n", log_lvl);

	while (!kstack_end(sp)) {
		addr = *sp++;
		if (kernel_text_address(addr)) {
#ifdef CONFIG_KALLSYMS
			printk("%s [<%08lx>] ", log_lvl, addr);
#else
			printk(" [<%08lx>] ", addr);
#endif
			print_symbol("%s\n", addr);
		}
	}
	printk("\n");
}
#endif

void show_stack_log_lvl(struct task_struct *tsk, unsigned long sp,
			struct pt_regs *regs, const char *log_lvl)
{
	struct thread_info *tinfo;

	if (sp == 0) {
		if (tsk)
			sp = tsk->thread.cpu_context.ksp;
		else
			sp = (unsigned long)&tinfo;
	}
	if (!tsk)
		tsk = current;

	tinfo = task_thread_info(tsk);

	if (valid_stack_ptr(tinfo, sp)) {
		dump_mem("Stack: ", log_lvl, sp,
			 THREAD_SIZE + (unsigned long)tinfo);
		show_trace_log_lvl(tsk, (unsigned long *)sp, regs, log_lvl);
	}
}

void show_stack(struct task_struct *tsk, unsigned long *stack)
{
	show_stack_log_lvl(tsk, (unsigned long)stack, NULL, "");
}

static const char *cpu_modes[] = {
	"Application", "Supervisor", "Interrupt level 0", "Interrupt level 1",
	"Interrupt level 2", "Interrupt level 3", "Exception", "NMI"
};

void show_regs_log_lvl(struct pt_regs *regs, const char *log_lvl)
{
	unsigned long sp = regs->sp;
	unsigned long lr = regs->lr;
	unsigned long mode = (regs->sr & MODE_MASK) >> MODE_SHIFT;

	show_regs_print_info(log_lvl);

	if (!user_mode(regs)) {
		sp = (unsigned long)regs + FRAME_SIZE_FULL;

		printk("%s", log_lvl);
		print_symbol("PC is at %s\n", instruction_pointer(regs));
		printk("%s", log_lvl);
		print_symbol("LR is at %s\n", lr);
	}

	printk("%spc : [<%08lx>]    lr : [<%08lx>]    %s\n"
	       "%ssp : %08lx  r12: %08lx  r11: %08lx\n",
	       log_lvl, instruction_pointer(regs), lr, print_tainted(),
	       log_lvl, sp, regs->r12, regs->r11);
	printk("%sr10: %08lx  r9 : %08lx  r8 : %08lx\n",
	       log_lvl, regs->r10, regs->r9, regs->r8);
	printk("%sr7 : %08lx  r6 : %08lx  r5 : %08lx  r4 : %08lx\n",
	       log_lvl, regs->r7, regs->r6, regs->r5, regs->r4);
	printk("%sr3 : %08lx  r2 : %08lx  r1 : %08lx  r0 : %08lx\n",
	       log_lvl, regs->r3, regs->r2, regs->r1, regs->r0);
	printk("%sFlags: %c%c%c%c%c\n", log_lvl,
	       regs->sr & SR_Q ? 'Q' : 'q',
	       regs->sr & SR_V ? 'V' : 'v',
	       regs->sr & SR_N ? 'N' : 'n',
	       regs->sr & SR_Z ? 'Z' : 'z',
	       regs->sr & SR_C ? 'C' : 'c');
	printk("%sMode bits: %c%c%c%c%c%c%c%c%c%c\n", log_lvl,
	       regs->sr & SR_H ? 'H' : 'h',
	       regs->sr & SR_J ? 'J' : 'j',
	       regs->sr & SR_DM ? 'M' : 'm',
	       regs->sr & SR_D ? 'D' : 'd',
	       regs->sr & SR_EM ? 'E' : 'e',
	       regs->sr & SR_I3M ? '3' : '.',
	       regs->sr & SR_I2M ? '2' : '.',
	       regs->sr & SR_I1M ? '1' : '.',
	       regs->sr & SR_I0M ? '0' : '.',
	       regs->sr & SR_GM ? 'G' : 'g');
	printk("%sCPU Mode: %s\n", log_lvl, cpu_modes[mode]);
}

void show_regs(struct pt_regs *regs)
{
	unsigned long sp = regs->sp;

	if (!user_mode(regs))
		sp = (unsigned long)regs + FRAME_SIZE_FULL;

	show_regs_log_lvl(regs, "");
	show_trace_log_lvl(current, (unsigned long *)sp, regs, "");
}
EXPORT_SYMBOL(show_regs);

/* Fill in the fpu structure for a core dump. This is easy -- we don't have any */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu)
{
	/* Not valid */
	return 0;
}

asmlinkage void ret_from_fork(void);
asmlinkage void ret_from_kernel_thread(void);
asmlinkage void syscall_return(void);

int copy_thread(unsigned long clone_flags, unsigned long usp,
		unsigned long arg,
		struct task_struct *p)
{
	struct pt_regs *childregs = task_pt_regs(p);

	if (unlikely(p->flags & PF_KTHREAD)) {
		memset(childregs, 0, sizeof(struct pt_regs));
		p->thread.cpu_context.r0 = arg;
		p->thread.cpu_context.r1 = usp; /* fn */
		p->thread.cpu_context.r2 = syscall_return;
		p->thread.cpu_context.pc = (unsigned long)ret_from_kernel_thread;
		childregs->sr = MODE_SUPERVISOR;
	} else {
		*childregs = *current_pt_regs();
		if (usp)
			childregs->sp = usp;
		childregs->r12 = 0; /* Set return value for child */
		p->thread.cpu_context.pc = (unsigned long)ret_from_fork;
	}

	p->thread.cpu_context.sr = MODE_SUPERVISOR | SR_GM;
	p->thread.cpu_context.ksp = (unsigned long)childregs;

	clear_tsk_thread_flag(p, TIF_DEBUG);
	if ((clone_flags & CLONE_PTRACE) && test_thread_flag(TIF_DEBUG))
		ocd_enable(p);

	return 0;
}

/*
 * This function is supposed to answer the question "who called
 * schedule()?"
 */
unsigned long get_wchan(struct task_struct *p)
{
	unsigned long pc;
	unsigned long stack_page;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	stack_page = (unsigned long)task_stack_page(p);
	BUG_ON(!stack_page);

	/*
	 * The stored value of PC is either the address right after
	 * the call to __switch_to() or ret_from_fork.
	 */
	pc = thread_saved_pc(p);
	if (in_sched_functions(pc)) {
#ifdef CONFIG_FRAME_POINTER
		unsigned long fp = p->thread.cpu_context.r7;
		BUG_ON(fp < stack_page || fp > (THREAD_SIZE + stack_page));
		pc = *(unsigned long *)fp;
#else
		/*
		 * We depend on the frame size of schedule here, which
		 * is actually quite ugly. It might be possible to
		 * determine the frame size automatically at build
		 * time by doing this:
		 *   - compile sched.c
		 *   - disassemble the resulting sched.o
		 *   - look for 'sub sp,??' shortly after '<schedule>:'
		 */
		unsigned long sp = p->thread.cpu_context.ksp + 16;
		BUG_ON(sp < stack_page || sp > (THREAD_SIZE + stack_page));
		pc = *(unsigned long *)sp;
#endif
	}

	return pc;
}
