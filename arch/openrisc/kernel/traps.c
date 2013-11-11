/*
 * OpenRISC traps.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *  Here we handle the break vectors not used by the system call
 *  mechanism, as well as some general stack/register dumping
 *  things.
 *
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/kallsyms.h>
#include <asm/uaccess.h>

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/pgtable.h>

extern char _etext, _stext;

int kstack_depth_to_print = 0x180;

static inline int valid_stack_ptr(struct thread_info *tinfo, void *p)
{
	return p > (void *)tinfo && p < (void *)tinfo + THREAD_SIZE - 3;
}

void show_trace(struct task_struct *task, unsigned long *stack)
{
	struct thread_info *context;
	unsigned long addr;

	context = (struct thread_info *)
	    ((unsigned long)stack & (~(THREAD_SIZE - 1)));

	while (valid_stack_ptr(context, stack)) {
		addr = *stack++;
		if (__kernel_text_address(addr)) {
			printk(" [<%08lx>]", addr);
			print_symbol(" %s", addr);
			printk("\n");
		}
	}
	printk(" =======================\n");
}

/* displays a short stack trace */
void show_stack(struct task_struct *task, unsigned long *esp)
{
	unsigned long addr, *stack;
	int i;

	if (esp == NULL)
		esp = (unsigned long *)&esp;

	stack = esp;

	printk("Stack dump [0x%08lx]:\n", (unsigned long)esp);
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (kstack_end(stack))
			break;
		if (__get_user(addr, stack)) {
			/* This message matches "failing address" marked
			   s390 in ksymoops, so lines containing it will
			   not be filtered out by ksymoops.  */
			printk("Failing address 0x%lx\n", (unsigned long)stack);
			break;
		}
		stack++;

		printk("sp + %02d: 0x%08lx\n", i * 4, addr);
	}
	printk("\n");

	show_trace(task, esp);

	return;
}

void show_trace_task(struct task_struct *tsk)
{
	/*
	 * TODO: SysRq-T trace dump...
	 */
}

void show_registers(struct pt_regs *regs)
{
	int i;
	int in_kernel = 1;
	unsigned long esp;

	esp = (unsigned long)(&regs->sp);
	if (user_mode(regs))
		in_kernel = 0;

	printk("CPU #: %d\n"
	       "   PC: %08lx    SR: %08lx    SP: %08lx\n",
	       smp_processor_id(), regs->pc, regs->sr, regs->sp);
	printk("GPR00: %08lx GPR01: %08lx GPR02: %08lx GPR03: %08lx\n",
	       0L, regs->gpr[1], regs->gpr[2], regs->gpr[3]);
	printk("GPR04: %08lx GPR05: %08lx GPR06: %08lx GPR07: %08lx\n",
	       regs->gpr[4], regs->gpr[5], regs->gpr[6], regs->gpr[7]);
	printk("GPR08: %08lx GPR09: %08lx GPR10: %08lx GPR11: %08lx\n",
	       regs->gpr[8], regs->gpr[9], regs->gpr[10], regs->gpr[11]);
	printk("GPR12: %08lx GPR13: %08lx GPR14: %08lx GPR15: %08lx\n",
	       regs->gpr[12], regs->gpr[13], regs->gpr[14], regs->gpr[15]);
	printk("GPR16: %08lx GPR17: %08lx GPR18: %08lx GPR19: %08lx\n",
	       regs->gpr[16], regs->gpr[17], regs->gpr[18], regs->gpr[19]);
	printk("GPR20: %08lx GPR21: %08lx GPR22: %08lx GPR23: %08lx\n",
	       regs->gpr[20], regs->gpr[21], regs->gpr[22], regs->gpr[23]);
	printk("GPR24: %08lx GPR25: %08lx GPR26: %08lx GPR27: %08lx\n",
	       regs->gpr[24], regs->gpr[25], regs->gpr[26], regs->gpr[27]);
	printk("GPR28: %08lx GPR29: %08lx GPR30: %08lx GPR31: %08lx\n",
	       regs->gpr[28], regs->gpr[29], regs->gpr[30], regs->gpr[31]);
	printk("  RES: %08lx oGPR11: %08lx\n",
	       regs->gpr[11], regs->orig_gpr11);

	printk("Process %s (pid: %d, stackpage=%08lx)\n",
	       current->comm, current->pid, (unsigned long)current);
	/*
	 * When in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (in_kernel) {

		printk("\nStack: ");
		show_stack(NULL, (unsigned long *)esp);

		printk("\nCode: ");
		if (regs->pc < PAGE_OFFSET)
			goto bad;

		for (i = -24; i < 24; i++) {
			unsigned char c;
			if (__get_user(c, &((unsigned char *)regs->pc)[i])) {
bad:
				printk(" Bad PC value.");
				break;
			}

			if (i == 0)
				printk("(%02x) ", c);
			else
				printk("%02x ", c);
		}
	}
	printk("\n");
}

void nommu_dump_state(struct pt_regs *regs,
		      unsigned long ea, unsigned long vector)
{
	int i;
	unsigned long addr, stack = regs->sp;

	printk("\n\r[nommu_dump_state] :: ea %lx, vector %lx\n\r", ea, vector);

	printk("CPU #: %d\n"
	       "   PC: %08lx    SR: %08lx    SP: %08lx\n",
	       0, regs->pc, regs->sr, regs->sp);
	printk("GPR00: %08lx GPR01: %08lx GPR02: %08lx GPR03: %08lx\n",
	       0L, regs->gpr[1], regs->gpr[2], regs->gpr[3]);
	printk("GPR04: %08lx GPR05: %08lx GPR06: %08lx GPR07: %08lx\n",
	       regs->gpr[4], regs->gpr[5], regs->gpr[6], regs->gpr[7]);
	printk("GPR08: %08lx GPR09: %08lx GPR10: %08lx GPR11: %08lx\n",
	       regs->gpr[8], regs->gpr[9], regs->gpr[10], regs->gpr[11]);
	printk("GPR12: %08lx GPR13: %08lx GPR14: %08lx GPR15: %08lx\n",
	       regs->gpr[12], regs->gpr[13], regs->gpr[14], regs->gpr[15]);
	printk("GPR16: %08lx GPR17: %08lx GPR18: %08lx GPR19: %08lx\n",
	       regs->gpr[16], regs->gpr[17], regs->gpr[18], regs->gpr[19]);
	printk("GPR20: %08lx GPR21: %08lx GPR22: %08lx GPR23: %08lx\n",
	       regs->gpr[20], regs->gpr[21], regs->gpr[22], regs->gpr[23]);
	printk("GPR24: %08lx GPR25: %08lx GPR26: %08lx GPR27: %08lx\n",
	       regs->gpr[24], regs->gpr[25], regs->gpr[26], regs->gpr[27]);
	printk("GPR28: %08lx GPR29: %08lx GPR30: %08lx GPR31: %08lx\n",
	       regs->gpr[28], regs->gpr[29], regs->gpr[30], regs->gpr[31]);
	printk("  RES: %08lx oGPR11: %08lx\n",
	       regs->gpr[11], regs->orig_gpr11);

	printk("Process %s (pid: %d, stackpage=%08lx)\n",
	       ((struct task_struct *)(__pa(current)))->comm,
	       ((struct task_struct *)(__pa(current)))->pid,
	       (unsigned long)current);

	printk("\nStack: ");
	printk("Stack dump [0x%08lx]:\n", (unsigned long)stack);
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (((long)stack & (THREAD_SIZE - 1)) == 0)
			break;
		stack++;

		printk("%lx :: sp + %02d: 0x%08lx\n", stack, i * 4,
		       *((unsigned long *)(__pa(stack))));
	}
	printk("\n");

	printk("Call Trace:   ");
	i = 1;
	while (((long)stack & (THREAD_SIZE - 1)) != 0) {
		addr = *((unsigned long *)__pa(stack));
		stack++;

		if (kernel_text_address(addr)) {
			if (i && ((i % 6) == 0))
				printk("\n ");
			printk(" [<%08lx>]", addr);
			i++;
		}
	}
	printk("\n");

	printk("\nCode: ");

	for (i = -24; i < 24; i++) {
		unsigned char c;
		c = ((unsigned char *)(__pa(regs->pc)))[i];

		if (i == 0)
			printk("(%02x) ", c);
		else
			printk("%02x ", c);
	}
	printk("\n");
}

/* This is normally the 'Oops' routine */
void die(const char *str, struct pt_regs *regs, long err)
{

	console_verbose();
	printk("\n%s#: %04lx\n", str, err & 0xffff);
	show_registers(regs);
#ifdef CONFIG_JUMP_UPON_UNHANDLED_EXCEPTION
	printk("\n\nUNHANDLED_EXCEPTION: entering infinite loop\n");

	/* shut down interrupts */
	local_irq_disable();

	__asm__ __volatile__("l.nop   1");
	do {} while (1);
#endif
	do_exit(SIGSEGV);
}

/* This is normally the 'Oops' routine */
void die_if_kernel(const char *str, struct pt_regs *regs, long err)
{
	if (user_mode(regs))
		return;

	die(str, regs, err);
}

void unhandled_exception(struct pt_regs *regs, int ea, int vector)
{
	printk("Unable to handle exception at EA =0x%x, vector 0x%x",
	       ea, vector);
	die("Oops", regs, 9);
}

void __init trap_init(void)
{
	/* Nothing needs to be done */
}

asmlinkage void do_trap(struct pt_regs *regs, unsigned long address)
{
	siginfo_t info;
	memset(&info, 0, sizeof(info));
	info.si_signo = SIGTRAP;
	info.si_code = TRAP_TRACE;
	info.si_addr = (void *)address;
	force_sig_info(SIGTRAP, &info, current);

	regs->pc += 4;
}

asmlinkage void do_unaligned_access(struct pt_regs *regs, unsigned long address)
{
	siginfo_t info;

	if (user_mode(regs)) {
		/* Send a SIGSEGV */
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		/* info.si_code has been set above */
		info.si_addr = (void *)address;
		force_sig_info(SIGSEGV, &info, current);
	} else {
		printk("KERNEL: Unaligned Access 0x%.8lx\n", address);
		show_registers(regs);
		die("Die:", regs, address);
	}

}

asmlinkage void do_bus_fault(struct pt_regs *regs, unsigned long address)
{
	siginfo_t info;

	if (user_mode(regs)) {
		/* Send a SIGBUS */
		info.si_signo = SIGBUS;
		info.si_errno = 0;
		info.si_code = BUS_ADRERR;
		info.si_addr = (void *)address;
		force_sig_info(SIGBUS, &info, current);
	} else {		/* Kernel mode */
		printk("KERNEL: Bus error (SIGBUS) 0x%.8lx\n", address);
		show_registers(regs);
		die("Die:", regs, address);
	}
}

asmlinkage void do_illegal_instruction(struct pt_regs *regs,
				       unsigned long address)
{
	siginfo_t info;

	if (user_mode(regs)) {
		/* Send a SIGILL */
		info.si_signo = SIGILL;
		info.si_errno = 0;
		info.si_code = ILL_ILLOPC;
		info.si_addr = (void *)address;
		force_sig_info(SIGBUS, &info, current);
	} else {		/* Kernel mode */
		printk("KERNEL: Illegal instruction (SIGILL) 0x%.8lx\n",
		       address);
		show_registers(regs);
		die("Die:", regs, address);
	}
}
