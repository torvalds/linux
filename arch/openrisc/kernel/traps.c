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
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/kernel.h>
#include <linux/extable.h>
#include <linux/kmod.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/unwinder.h>
#include <asm/sections.h>

int kstack_depth_to_print = 0x180;
int lwa_flag;
unsigned long __user *lwa_addr;

void print_trace(void *data, unsigned long addr, int reliable)
{
	pr_emerg("[<%p>] %s%pS\n", (void *) addr, reliable ? "" : "? ",
	       (void *) addr);
}

/* displays a short stack trace */
void show_stack(struct task_struct *task, unsigned long *esp)
{
	if (esp == NULL)
		esp = (unsigned long *)&esp;

	pr_emerg("Call trace:\n");
	unwind_stack(NULL, esp, print_trace);
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

	esp = (unsigned long)(regs->sp);
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
	force_sig_fault(SIGTRAP, TRAP_TRACE, (void __user *)address, current);

	regs->pc += 4;
}

asmlinkage void do_unaligned_access(struct pt_regs *regs, unsigned long address)
{
	if (user_mode(regs)) {
		/* Send a SIGBUS */
		force_sig_fault(SIGBUS, BUS_ADRALN, (void __user *)address, current);
	} else {
		printk("KERNEL: Unaligned Access 0x%.8lx\n", address);
		show_registers(regs);
		die("Die:", regs, address);
	}

}

asmlinkage void do_bus_fault(struct pt_regs *regs, unsigned long address)
{
	if (user_mode(regs)) {
		/* Send a SIGBUS */
		force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *)address, current);
	} else {		/* Kernel mode */
		printk("KERNEL: Bus error (SIGBUS) 0x%.8lx\n", address);
		show_registers(regs);
		die("Die:", regs, address);
	}
}

static inline int in_delay_slot(struct pt_regs *regs)
{
#ifdef CONFIG_OPENRISC_NO_SPR_SR_DSX
	/* No delay slot flag, do the old way */
	unsigned int op, insn;

	insn = *((unsigned int *)regs->pc);
	op = insn >> 26;
	switch (op) {
	case 0x00: /* l.j */
	case 0x01: /* l.jal */
	case 0x03: /* l.bnf */
	case 0x04: /* l.bf */
	case 0x11: /* l.jr */
	case 0x12: /* l.jalr */
		return 1;
	default:
		return 0;
	}
#else
	return regs->sr & SPR_SR_DSX;
#endif
}

static inline void adjust_pc(struct pt_regs *regs, unsigned long address)
{
	int displacement;
	unsigned int rb, op, jmp;

	if (unlikely(in_delay_slot(regs))) {
		/* In delay slot, instruction at pc is a branch, simulate it */
		jmp = *((unsigned int *)regs->pc);

		displacement = sign_extend32(((jmp) & 0x3ffffff) << 2, 27);
		rb = (jmp & 0x0000ffff) >> 11;
		op = jmp >> 26;

		switch (op) {
		case 0x00: /* l.j */
			regs->pc += displacement;
			return;
		case 0x01: /* l.jal */
			regs->pc += displacement;
			regs->gpr[9] = regs->pc + 8;
			return;
		case 0x03: /* l.bnf */
			if (regs->sr & SPR_SR_F)
				regs->pc += 8;
			else
				regs->pc += displacement;
			return;
		case 0x04: /* l.bf */
			if (regs->sr & SPR_SR_F)
				regs->pc += displacement;
			else
				regs->pc += 8;
			return;
		case 0x11: /* l.jr */
			regs->pc = regs->gpr[rb];
			return;
		case 0x12: /* l.jalr */
			regs->pc = regs->gpr[rb];
			regs->gpr[9] = regs->pc + 8;
			return;
		default:
			break;
		}
	} else {
		regs->pc += 4;
	}
}

static inline void simulate_lwa(struct pt_regs *regs, unsigned long address,
				unsigned int insn)
{
	unsigned int ra, rd;
	unsigned long value;
	unsigned long orig_pc;
	long imm;

	const struct exception_table_entry *entry;

	orig_pc = regs->pc;
	adjust_pc(regs, address);

	ra = (insn >> 16) & 0x1f;
	rd = (insn >> 21) & 0x1f;
	imm = (short)insn;
	lwa_addr = (unsigned long __user *)(regs->gpr[ra] + imm);

	if ((unsigned long)lwa_addr & 0x3) {
		do_unaligned_access(regs, address);
		return;
	}

	if (get_user(value, lwa_addr)) {
		if (user_mode(regs)) {
			force_sig(SIGSEGV, current);
			return;
		}

		if ((entry = search_exception_tables(orig_pc))) {
			regs->pc = entry->fixup;
			return;
		}

		/* kernel access in kernel space, load it directly */
		value = *((unsigned long *)lwa_addr);
	}

	lwa_flag = 1;
	regs->gpr[rd] = value;
}

static inline void simulate_swa(struct pt_regs *regs, unsigned long address,
				unsigned int insn)
{
	unsigned long __user *vaddr;
	unsigned long orig_pc;
	unsigned int ra, rb;
	long imm;

	const struct exception_table_entry *entry;

	orig_pc = regs->pc;
	adjust_pc(regs, address);

	ra = (insn >> 16) & 0x1f;
	rb = (insn >> 11) & 0x1f;
	imm = (short)(((insn & 0x2200000) >> 10) | (insn & 0x7ff));
	vaddr = (unsigned long __user *)(regs->gpr[ra] + imm);

	if (!lwa_flag || vaddr != lwa_addr) {
		regs->sr &= ~SPR_SR_F;
		return;
	}

	if ((unsigned long)vaddr & 0x3) {
		do_unaligned_access(regs, address);
		return;
	}

	if (put_user(regs->gpr[rb], vaddr)) {
		if (user_mode(regs)) {
			force_sig(SIGSEGV, current);
			return;
		}

		if ((entry = search_exception_tables(orig_pc))) {
			regs->pc = entry->fixup;
			return;
		}

		/* kernel access in kernel space, store it directly */
		*((unsigned long *)vaddr) = regs->gpr[rb];
	}

	lwa_flag = 0;
	regs->sr |= SPR_SR_F;
}

#define INSN_LWA	0x1b
#define INSN_SWA	0x33

asmlinkage void do_illegal_instruction(struct pt_regs *regs,
				       unsigned long address)
{
	unsigned int op;
	unsigned int insn = *((unsigned int *)address);

	op = insn >> 26;

	switch (op) {
	case INSN_LWA:
		simulate_lwa(regs, address, insn);
		return;

	case INSN_SWA:
		simulate_swa(regs, address, insn);
		return;

	default:
		break;
	}

	if (user_mode(regs)) {
		/* Send a SIGILL */
		force_sig_fault(SIGILL, ILL_ILLOPC, (void __user *)address, current);
	} else {		/* Kernel mode */
		printk("KERNEL: Illegal instruction (SIGILL) 0x%.8lx\n",
		       address);
		show_registers(regs);
		die("Die:", regs, address);
	}
}
