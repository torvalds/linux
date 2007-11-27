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
#include <linux/ptrace.h>
#include <linux/reboot.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>

#include <asm/sysreg.h>
#include <asm/ocd.h>

void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);

extern void cpu_idle_sleep(void);

/*
 * This file handles the architecture-dependent parts of process handling..
 */

void cpu_idle(void)
{
	/* endless idle loop with no priority at all */
	while (1) {
		while (!need_resched())
			cpu_idle_sleep();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
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
}

void machine_restart(char *cmd)
{
	ocd_write(DC, (1 << OCD_DC_DBE_BIT));
	ocd_write(DC, (1 << OCD_DC_RES_BIT));
	while (1) ;
}

/*
 * PC is actually discarded when returning from a system call -- the
 * return address must be stored in LR. This function will make sure
 * LR points to do_exit before starting the thread.
 *
 * Also, when returning from fork(), r12 is 0, so we must copy the
 * argument as well.
 *
 *  r0 : The argument to the main thread function
 *  r1 : The address of do_exit
 *  r2 : The address of the main thread function
 */
asmlinkage extern void kernel_thread_helper(void);
__asm__("	.type	kernel_thread_helper, @function\n"
	"kernel_thread_helper:\n"
	"	mov	r12, r0\n"
	"	mov	lr, r2\n"
	"	mov	pc, r1\n"
	"	.size	kernel_thread_helper, . - kernel_thread_helper");

int kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));

	regs.r0 = (unsigned long)arg;
	regs.r1 = (unsigned long)fn;
	regs.r2 = (unsigned long)do_exit;
	regs.lr = (unsigned long)kernel_thread_helper;
	regs.pc = (unsigned long)kernel_thread_helper;
	regs.sr = MODE_SUPERVISOR;

	return do_fork(flags | CLONE_VM | CLONE_UNTRACED,
		       0, &regs, 0, NULL, NULL);
}
EXPORT_SYMBOL(kernel_thread);

/*
 * Free current thread data structures etc
 */
void exit_thread(void)
{
	/* nothing to do */
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

void dump_stack(void)
{
	unsigned long stack;

	show_trace_log_lvl(current, &stack, NULL, "");
}
EXPORT_SYMBOL(dump_stack);

static const char *cpu_modes[] = {
	"Application", "Supervisor", "Interrupt level 0", "Interrupt level 1",
	"Interrupt level 2", "Interrupt level 3", "Exception", "NMI"
};

void show_regs_log_lvl(struct pt_regs *regs, const char *log_lvl)
{
	unsigned long sp = regs->sp;
	unsigned long lr = regs->lr;
	unsigned long mode = (regs->sr & MODE_MASK) >> MODE_SHIFT;

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
	printk("%sProcess: %s [%d] (task: %p thread: %p)\n",
	       log_lvl, current->comm, current->pid, current,
	       task_thread_info(current));
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

int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
		unsigned long unused,
		struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs;

	childregs = ((struct pt_regs *)(THREAD_SIZE + (unsigned long)task_stack_page(p))) - 1;
	*childregs = *regs;

	if (user_mode(regs))
		childregs->sp = usp;
	else
		childregs->sp = (unsigned long)task_stack_page(p) + THREAD_SIZE;

	childregs->r12 = 0; /* Set return value for child */

	p->thread.cpu_context.sr = MODE_SUPERVISOR | SR_GM;
	p->thread.cpu_context.ksp = (unsigned long)childregs;
	p->thread.cpu_context.pc = (unsigned long)ret_from_fork;

	return 0;
}

/* r12-r8 are dummy parameters to force the compiler to use the stack */
asmlinkage int sys_fork(struct pt_regs *regs)
{
	return do_fork(SIGCHLD, regs->sp, regs, 0, NULL, NULL);
}

asmlinkage int sys_clone(unsigned long clone_flags, unsigned long newsp,
			 unsigned long parent_tidptr,
			 unsigned long child_tidptr, struct pt_regs *regs)
{
	if (!newsp)
		newsp = regs->sp;
	return do_fork(clone_flags, newsp, regs, 0,
		       (int __user *)parent_tidptr,
		       (int __user *)child_tidptr);
}

asmlinkage int sys_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->sp, regs,
		       0, NULL, NULL);
}

asmlinkage int sys_execve(char __user *ufilename, char __user *__user *uargv,
			  char __user *__user *uenvp, struct pt_regs *regs)
{
	int error;
	char *filename;

	filename = getname(ufilename);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;

	error = do_execve(filename, uargv, uenvp, regs);
	if (error == 0)
		current->ptrace &= ~PT_DTRACE;
	putname(filename);

out:
	return error;
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
